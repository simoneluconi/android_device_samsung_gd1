/*
 * hdmid: the micro-HDMI output.
 *
 * Android's display stack has no external display on the Exynos 4412 (the
 * hwcomposer has none, and libhdmi is built only for the 4210), so this daemon
 * owns the TV output and shows what it is handed, never a copy of the screen:
 * the photo being viewed in GC Gallery, or the camera's live view, which the
 * camera HAL hands over frame by frame (video layer, /dev/video20). Without
 * either it shows a short notice that HDMI works in those two apps.
 *
 * It sleeps on the kernel's uevent socket and on its control socket, and does
 * nothing in between. On plug-in (the hdmi switch) it reads the sink's EDID
 * over DDC (/dev/i2c-5), picks the mode (persist.vendor.hdmi.mode, "auto" by
 * default: the best mode the sink lists) and starts the s5p-tvout output
 * (/dev/video16) with mixer graphic layer 0 (/dev/graphics/fb10). On unplug it
 * frees everything, which stops the output. The output is also off while the
 * camera's own screen is off.
 *
 * Pictures arrive over /dev/socket/hdmid (stream, hdmid_proto.h) already
 * scaled for the mode, in Android's ARGB_8888 memory order. They are copied,
 * red and blue swapped for the mixer, into physically contiguous ION memory
 * (the exynos contig heap, 16 MB in all) and shown centred, on the mixer's
 * black background. Two
 * buffers when both fit, so a new photo replaces the old at a vsync; one
 * otherwise, blanked while it is rewritten.
 *
 * Properties: persist.vendor.hdmi.mode (auto or a mode name, hdmi_modes.h)
 * is the user's choice; vendor.hdmi.state, vendor.hdmi.mode and
 * vendor.hdmi.sink report what is going on.
 */

#define LOG_TAG "hdmid"
#define _GNU_SOURCE /* pipe2 */

#include <cutils/properties.h>
#include <cutils/sockets.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <linux/netlink.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "edid.h"
#include "font5x7.h"
#include "hdmi_modes.h"
#include "hdmid_proto.h"

/* Old (3.4) ION userspace ABI, 32-bit */
struct ion_allocation_data { size_t len; size_t align; unsigned int flags; void *handle; };
struct ion_fd_data { void *handle; int fd; };
struct ion_handle_data { void *handle; };
struct ion_custom_data { unsigned int cmd; unsigned long arg; };
struct ion_phys_data { int fd_buffer; unsigned long phys; size_t size; };
#define ION_IOC_MAGIC 'I'
#define ION_IOC_ALLOC _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_FREE _IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)
#define ION_IOC_SHARE _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_CUSTOM _IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data)
#define ION_HEAP_EXYNOS_CONTIG_MASK (1 << 4)
/* Write-combined mapping: a cached one can keep the CPU's writes from the mixer */
#define ION_EXYNOS_NONCACHE_MASK (1 << 30)
#define ION_EXYNOS_CUSTOM_PHYS 1

/* s5p-tvout private ABI (drivers/media/video/samsung/tvout) */
struct s5ptvfb_user_window { int x; int y; };
#define S5PTVFB_WIN_POSITION _IOW('F', 213, struct s5ptvfb_user_window)
#define S5PTVFB_WIN_SET_ADDR _IOW('F', 219, uint32_t)
struct s5ptvfb_user_scaling { int ver; int hor; };
#define S5PTVFB_SCALING _IOW('F', 222, struct s5ptvfb_user_scaling)
/* Output 2 is HDMI RGB (output 1, YCbCr, showed black as purple on a test TV);
 * 3 is DVI, for a sink without the HDMI data block: no InfoFrames, no audio. */
#define TV_OUTPUT_HDMI_RGB 2
#define TV_OUTPUT_DVI 3

/* The video layer (s5p-tvout "Video Overlay"): the camera's live view */
#define TV_VIDEO "/dev/video20"
struct vo_src { void *base_y; void *base_c; struct v4l2_pix_format pix_fmt; };

#define HDMI_STATE "/sys/class/switch/hdmi/state"
#define DDC_BUS "/dev/i2c-5"
#define PROP_MODE "persist.vendor.hdmi.mode"
#define GALLERY_PACKAGE "com.simoneluconi.gcgallery"
#define AID_CAMERASERVER 1047   /* the camera HAL (camera provider service) */

struct buf {
    void *handle;
    int fd;
    unsigned long phys;
    size_t size;
    void *map;
};

static struct {
    int ion;
    int tv, fb;                 /* -1 while the output is off */
    const struct hdmi_mode *mode;
    int output;
    int automatic;              /* the mode came from "auto" */
    struct edid_info edid;
    struct buf photo[2];        /* full screen each, while the gallery shows photos */
    int nphoto;                 /* 2, or 1 when the second did not fit */
    int front;                  /* photo[] being shown, or -1 */
    struct { int lw, lh, x, y, w, h; } layout[2];   /* what each photo buffer holds */
    struct buf idle;
    int layer_w, layer_h, layer_x, layer_y, layer_scaled;   /* layer_w 0: not set up */
    int layer_on;               /* unblanked */
    struct client { int fd; uid_t uid; } gallery, camera;   /* fd -1: none */
    int vo;                     /* the video layer, open while camera frames come; -1 */
    struct { uint32_t w, h, fourcc; } video;   /* its source format */
    const struct client *video_owner;          /* who sends the frames */
} st = {.ion = -1, .tv = -1, .fb = -1, .front = -1, .gallery = {-1, 0}, .camera = {-1, 0}, .vo = -1};

static const struct buf no_buf = {NULL, -1, 0, 0, MAP_FAILED};

/* --- ION */

static void buf_free(struct buf *b) {
    if (b->map != MAP_FAILED) munmap(b->map, b->size);
    if (b->fd >= 0) close(b->fd);
    if (b->handle) {
        struct ion_handle_data hd = {b->handle};
        ioctl(st.ion, ION_IOC_FREE, &hd);
    }
    *b = no_buf;
}

static int buf_alloc(struct buf *b, size_t size) {
    *b = no_buf;
    size = (size + 4095) & ~(size_t)4095;
    struct ion_allocation_data alloc = {size, 4096, ION_HEAP_EXYNOS_CONTIG_MASK | ION_EXYNOS_NONCACHE_MASK, NULL};
    if (ioctl(st.ion, ION_IOC_ALLOC, &alloc) < 0) return -1;
    b->handle = alloc.handle;
    b->size = size;
    struct ion_fd_data share = {alloc.handle, -1};
    if (ioctl(st.ion, ION_IOC_SHARE, &share) < 0) goto fail;
    b->fd = share.fd;
    struct ion_phys_data phys = {b->fd, 0, 0};
    struct ion_custom_data custom = {ION_EXYNOS_CUSTOM_PHYS, (unsigned long)&phys};
    if (ioctl(st.ion, ION_IOC_CUSTOM, &custom) < 0) goto fail;
    b->phys = phys.phys;
    b->map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, b->fd, 0);
    if (b->map == MAP_FAILED) goto fail;
    return 0;
fail:
    ALOGE("ION buffer of %zu bytes: %m", size);
    buf_free(b);
    return -1;
}

/* --- Mixer graphic layer 0 */

static void layer_blank(void) {
    if (st.fb >= 0) ioctl(st.fb, FBIOBLANK, FB_BLANK_POWERDOWN);
    st.layer_on = 0;
}

/*
 * The layer is w x h pixels at (x, y), doubled in both directions by the mixer
 * when scaled. With blank it stays off until the next layer_show(); without,
 * the new size, scale and address all land in the same frame.
 */
static int layer_config(int w, int h, int x, int y, int scaled, int blank) {
    if (w == st.layer_w && h == st.layer_h && x == st.layer_x && y == st.layer_y && scaled == st.layer_scaled)
        return 0;
    struct fb_var_screeninfo var;
    if (ioctl(st.fb, FBIOGET_VSCREENINFO, &var) < 0) { ALOGE("FBIOGET_VSCREENINFO: %m"); return -1; }
    var.xres = var.xres_virtual = w;
    var.yres = var.yres_virtual = h;
    var.xoffset = var.yoffset = 0;
    var.bits_per_pixel = 32;
    var.blue.offset = 0;    var.blue.length = 8;
    var.green.offset = 8;   var.green.length = 8;
    var.red.offset = 16;    var.red.length = 8;
    var.transp.offset = 24; var.transp.length = 8;
    var.activate = FB_ACTIVATE_FORCE;
    if (blank) layer_blank();
    /* This also clears the scale bits (it rewrites the layer's size register) */
    if (ioctl(st.fb, FBIOPUT_VSCREENINFO, &var) < 0) { ALOGE("FBIOPUT_VSCREENINFO: %m"); return -1; }
    /* Also at 1x: the driver keeps the scale it was last given and applies it
     * again when it reprograms the layer (only ORing bits in, so 1x itself is a
     * no-op on the register) */
    struct s5ptvfb_user_scaling sc = {scaled, scaled};   /* VERTICAL_X2 = HORIZONTAL_X2 = 1 */
    if (ioctl(st.fb, S5PTVFB_SCALING, &sc) < 0) { ALOGE("SCALING: %m"); return -1; }
    struct s5ptvfb_user_window pos = {x, y};
    if (ioctl(st.fb, S5PTVFB_WIN_POSITION, &pos) < 0) { ALOGE("WIN_POSITION: %m"); return -1; }
    st.layer_w = w;
    st.layer_h = h;
    st.layer_x = x;
    st.layer_y = y;
    st.layer_scaled = scaled;
    return 0;
}

/*
 * Shows b in the layer as configured. The layer is enabled only when it was
 * off: enabling reprograms it (s5p_mixer_ctrl_set_reg), and that rewrites the
 * size register after setting the scale bits, so the 2x scale is lost and must
 * be set again.
 */
static int layer_show(const struct buf *b) {
    if (ioctl(st.fb, S5PTVFB_WIN_SET_ADDR, (uint32_t)b->phys) < 0) { ALOGE("WIN_SET_ADDR: %m"); return -1; }
    if (st.layer_on) return 0;
    if (ioctl(st.fb, FBIOBLANK, FB_BLANK_UNBLANK) < 0) { ALOGE("FBIOBLANK: %m"); return -1; }
    st.layer_on = 1;
    if (st.layer_scaled) {
        struct s5ptvfb_user_scaling x2 = {1, 1};
        if (ioctl(st.fb, S5PTVFB_SCALING, &x2) < 0) { ALOGE("SCALING: %m"); return -1; }
    }
    return 0;
}

static void free_photos(void) {
    buf_free(&st.photo[0]);
    buf_free(&st.photo[1]);
    st.nphoto = 0;
    st.front = -1;
}

/*
 * Two full-screen buffers, allocated together into an empty heap so they fit
 * (2 x 8.3 MB of the 16 MB at 1080p; freeing and reallocating them at
 * changing sizes fragmented the heap). They stay until the gallery goes.
 */
static int alloc_photos(void) {
    if (st.nphoto) return 0;
    size_t size = (size_t)st.mode->w * st.mode->h * 4;
    /* The notice may be on screen: off before its memory goes */
    layer_blank();
    buf_free(&st.idle);
    if (buf_alloc(&st.photo[0], size) < 0) return -1;
    st.nphoto = buf_alloc(&st.photo[1], size) == 0 ? 2 : 1;
    if (st.nphoto == 1) ALOGW("room for one photo buffer only: updates blank the layer");
    memset(st.layout, 0, sizeof(st.layout));
    st.front = -1;
    return 0;
}

/* --- The notice shown without the gallery */

static int italian(void) {
    char v[PROPERTY_VALUE_MAX];
    property_get("persist.sys.locale", v, "");
    if (!v[0]) property_get("ro.product.locale", v, "");
    return !strncmp(v, "it", 2);
}

static void show_idle(void) {
    /* The camera's frames are on screen: the notice waits for them to stop */
    if (st.tv < 0 || st.vo >= 0) return;
    const int it = italian();
    const char *title = "GALAXY CAMERA REVIVED";
    const char *l1 = it ? "L'HDMI FUNZIONA IN GC CAMERA E GC GALLERY" : "HDMI WORKS IN GC CAMERA AND GC GALLERY";
    const char *l2 = it ? "APRI LA FOTOCAMERA O UNA FOTO PER VEDERLA QUI" : "OPEN THE CAMERA OR A PHOTO TO SEE IT HERE";
    char l3[64];
    snprintf(l3, sizeof(l3), "%s%s%s%s", st.edid.name, st.edid.name[0] ? " - " : "", st.mode->name,
             st.output == TV_OUTPUT_DVI ? " DVI" : "");

    const struct hdmi_mode *m = st.mode;
    /* Pixel aspect ratio: glyphs are narrowed or widened to stay square on the display */
    double par = (double)m->dar_w * m->h / ((double)m->dar_h * m->w);
    int sy1 = m->h / 100, sy2 = m->h / 170, sy3 = m->h / 270;
    if (sy2 < 1) sy2 = 1;
    if (sy3 < 1) sy3 = 1;
    int sx1 = (int)(sy1 / par + 0.5), sx2 = (int)(sy2 / par + 0.5), sx3 = (int)(sy3 / par + 0.5);
    if (sx2 < 1) sx2 = 1;
    if (sx3 < 1) sx3 = 1;

    int tw = font5x7_width(title, sx1), w1 = font5x7_width(l1, sx2), w2 = font5x7_width(l2, sx2);
    int w3 = font5x7_width(l3, sx3);
    int w = tw;
    if (w1 > w) w = w1;
    if (w2 > w) w = w2;
    if (w3 > w) w = w3;
    int margin = 2 * sy1;
    w = (w + 2 * margin + 15) & ~15;
    if (w > m->w) w = m->w & ~15;
    int y1 = margin + 7 * sy1 + 11 * sy2;
    int y2 = y1 + 10 * sy2;
    int y3 = y2 + 7 * sy2 + 8 * sy2;
    int h = y3 + 7 * sy3 + margin;
    if (h > m->h) h = m->h;

    /* The photos' memory goes back to the heap first: only the notice stays */
    layer_blank();
    free_photos();
    buf_free(&st.idle);
    if (buf_alloc(&st.idle, (size_t)w * h * 4) < 0) return;
    uint32_t *px = st.idle.map;
    for (int i = 0; i < w * h; i++) px[i] = 0xff000000;
    font5x7_draw(px, w, h, title, (w - tw) / 2, margin, sx1, sy1, 0xffffffff);
    font5x7_draw(px, w, h, l1, (w - w1) / 2, y1, sx2, sy2, 0xffffd040);
    font5x7_draw(px, w, h, l2, (w - w2) / 2, y2, sx2, sy2, 0xffdddddd);
    font5x7_draw(px, w, h, l3, (w - w3) / 2, y3, sx3, sy3, 0xff808080);
    if (layer_config(w, h, (m->w - w) / 2, (m->h - h) / 2, 0, 1) == 0) layer_show(&st.idle);
}

/* --- EDID over DDC (E-DDC: segment pointer 0x30, EDID 0x50) */

static int ddc_read(int fd, int block, uint8_t *out) {
    uint8_t segment = (uint8_t)(block / 2), offset = (uint8_t)((block % 2) * EDID_BLOCK);
    struct i2c_msg msgs[3];
    int n = 0;
    if (segment) msgs[n++] = (struct i2c_msg){0x30, 0, 1, &segment};
    msgs[n++] = (struct i2c_msg){0x50, 0, 1, &offset};
    msgs[n++] = (struct i2c_msg){0x50, I2C_M_RD, EDID_BLOCK, out};
    struct i2c_rdwr_ioctl_data data = {msgs, (unsigned)n};
    return ioctl(fd, I2C_RDWR, &data) == n ? 0 : -1;
}

static void read_edid(void) {
    uint8_t raw[4 * EDID_BLOCK];
    int len = 0;
    memset(&st.edid, 0, sizeof(st.edid));
    int fd = open(DDC_BUS, O_RDWR | O_CLOEXEC);
    if (fd < 0) { ALOGE("open " DDC_BUS ": %m"); return; }
    /* Right after hot-plug the sink may not answer yet */
    for (int tries = 0; tries < 5; tries++) {
        if (ddc_read(fd, 0, raw) == 0 && edid_parse(raw, EDID_BLOCK, &st.edid) == 0) {
            len = EDID_BLOCK;
            break;
        }
        usleep(100 * 1000);
    }
    int ext = len ? raw[126] : 0;
    for (int b = 1; b <= ext && b < 4; b++) {
        if (ddc_read(fd, b, raw + b * EDID_BLOCK) < 0) break;
        len += EDID_BLOCK;
    }
    close(fd);
    if (!len) {
        ALOGW("no EDID from the sink");
        return;
    }
    edid_parse(raw, len, &st.edid);
    ALOGI("EDID: \"%s\", %s, %d blocks", st.edid.name, st.edid.hdmi ? "HDMI" : "DVI", len / EDID_BLOCK);
}

static int sink_lists(const struct hdmi_mode *m) {
    return st.edid.valid && edid_supports(&st.edid, m->w, m->h, m->interlaced, m->mhz, m->vic);
}

static void choose_mode(void) {
    char want[PROPERTY_VALUE_MAX];
    property_get(PROP_MODE, want, "auto");
    st.mode = NULL;
    st.automatic = 0;
    if (strcmp(want, "auto")) {
        st.mode = hdmi_mode_by_name(want);
        if (!st.mode) ALOGW("unknown " PROP_MODE " \"%s\": auto", want);
    }
    if (!st.mode) {
        st.automatic = 1;
        for (int i = 0; i < HDMI_MODE_COUNT && !st.mode; i++)
            if (sink_lists(&hdmi_modes[i])) st.mode = &hdmi_modes[i];
        if (!st.mode) st.mode = hdmi_mode_by_name(HDMI_MODE_FALLBACK);
    }
    /* A sink with an EDID but no HDMI data block is DVI */
    st.output = st.edid.valid && !st.edid.hdmi ? TV_OUTPUT_DVI : TV_OUTPUT_HDMI_RGB;
}

/*
 * --- The video layer: the camera's live view. The camera HAL hands over each
 * preview frame's physical planes (FIMC memory, NV21); the video processor
 * scales it to the mode in hardware, so a frame costs one ioctl. The graphic
 * layer is off meanwhile and its buffers go back to the heap.
 */

static int video_src(uint32_t y, uint32_t c) {
    struct v4l2_format f;
    memset(&f, 0, sizeof(f));
    f.type = V4L2_BUF_TYPE_PRIVATE;
    struct vo_src *s = (struct vo_src *)f.fmt.raw_data;
    s->base_y = (void *)(uintptr_t)y;
    s->base_c = (void *)(uintptr_t)c;
    s->pix_fmt.width = st.video.w;
    s->pix_fmt.height = st.video.h;
    s->pix_fmt.pixelformat = st.video.fourcc;
    s->pix_fmt.field = V4L2_FIELD_NONE;
    /* priv 0: the planes are used in place (non-zero copies them in the driver) */
    if (ioctl(st.vo, VIDIOC_S_FMT, &f) < 0) { ALOGE("video S_FMT: %m"); return -1; }
    return 0;
}

static void video_close(void) {
    if (st.vo < 0) return;
    int off = 0;
    ioctl(st.vo, VIDIOC_OVERLAY, &off);
    /* The release also turns the mixer's video layer off */
    close(st.vo);
    st.vo = -1;
    memset(&st.video, 0, sizeof(st.video));
}

/*
 * The frame's square pixels fitted into the mode, centred: a 4:3 preview is
 * pillarboxed on a 16:9 screen. Even sizes and positions.
 */
static void video_rect(struct v4l2_rect *r) {
    const struct hdmi_mode *m = st.mode;
    double a = (double)st.video.w / st.video.h, dar = (double)m->dar_w / m->dar_h;
    double fw = a >= dar ? 1.0 : a / dar, fh = a >= dar ? dar / a : 1.0;
    int w = (int)(m->w * fw + 0.5) & ~1, h = (int)(m->h * fh + 0.5) & ~1;
    r->left = ((m->w - w) / 2) & ~1;
    r->top = ((m->h - h) / 2) & ~1;
    r->width = w;
    r->height = h;
}

static int video_open(uint32_t w, uint32_t h, uint32_t fourcc, uint32_t y, uint32_t c) {
    video_close();
    /* The graphic layer's notice or photo goes, and its memory with it */
    layer_blank();
    free_photos();
    buf_free(&st.idle);
    st.vo = open(TV_VIDEO, O_RDWR | O_CLOEXEC);
    if (st.vo < 0) { ALOGE("open " TV_VIDEO ": %m"); return -1; }
    st.video.w = w;
    st.video.h = h;
    st.video.fourcc = fourcc;
    if (video_src(y, c) < 0) goto fail;
    struct v4l2_crop crop;
    memset(&crop, 0, sizeof(crop));
    crop.type = V4L2_BUF_TYPE_PRIVATE;
    crop.c.width = w;
    crop.c.height = h;
    if (ioctl(st.vo, VIDIOC_S_CROP, &crop) < 0) { ALOGE("video S_CROP: %m"); goto fail; }
    struct v4l2_format f;
    memset(&f, 0, sizeof(f));
    f.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
    video_rect(&f.fmt.win.w);
    f.fmt.win.global_alpha = 0xff;
    if (ioctl(st.vo, VIDIOC_S_FMT, &f) < 0) { ALOGE("video S_FMT overlay: %m"); goto fail; }
    int on = 1;
    if (ioctl(st.vo, VIDIOC_OVERLAY, &on) < 0) { ALOGE("video OVERLAY: %m"); goto fail; }
    ALOGI("live view %ux%u on %dx%d at (%d,%d)", w, h, f.fmt.win.w.width, f.fmt.win.w.height,
          f.fmt.win.w.left, f.fmt.win.w.top);
    return 0;
fail:
    video_close();
    return -1;
}

static void video_frame(const struct client *from, const struct hdmid_msg *m) {
    uint32_t w = m->a[0], h = m->a[1], y = m->a[2], c = m->a[3], fourcc = m->a[4];
    /* Physical addresses: the TV would show any memory they point at */
    if (from->uid != 0 && from->uid != AID_CAMERASERVER) {
        ALOGW("video frame from uid %u refused", (unsigned)from->uid);
        return;
    }
    /* Its memory lives as long as its connection: see drop_client() */
    st.video_owner = from;
    if (st.tv < 0 || !st.mode) return;
    if (w < 16 || h < 16 || w > 1920 || h > 1088 || (w | h) & 1 || !y || !c ||
        (fourcc != V4L2_PIX_FMT_NV21 && fourcc != V4L2_PIX_FMT_NV12)) {
        ALOGW("video frame %ux%u %08x refused", w, h, fourcc);
        return;
    }
    if (st.vo >= 0 && w == st.video.w && h == st.video.h && fourcc == st.video.fourcc) {
        /* The address takes effect at the next vsync */
        if (video_src(y, c) < 0) video_close();
        return;
    }
    video_open(w, h, fourcc, y, c);
}

static void video_stop(void) {
    if (st.vo < 0) return;
    video_close();
    ALOGI("live view off");
    show_idle();
}

/* --- The TV output */

static void tv_stop(void) {
    video_close();
    layer_blank();
    free_photos();
    buf_free(&st.idle);
    if (st.fb >= 0) close(st.fb);
    /* The last close of the TV interface stops the HDMI output */
    if (st.tv >= 0) close(st.tv);
    st.fb = st.tv = -1;
    st.layer_w = st.layer_h = st.layer_on = 0;
    property_set("vendor.hdmi.state", "off");
    property_set("vendor.hdmi.mode", "");
}

static int tv_start(void) {
    choose_mode();
    st.tv = open("/dev/video16", O_RDWR | O_CLOEXEC);
    if (st.tv < 0) { ALOGE("open /dev/video16: %m"); return -1; }
    v4l2_std_id std = st.mode->std;
    int output = st.output;
    if (ioctl(st.tv, VIDIOC_S_STD, &std) < 0) { ALOGE("VIDIOC_S_STD: %m"); goto fail; }
    if (ioctl(st.tv, VIDIOC_S_OUTPUT, &output) < 0) { ALOGE("VIDIOC_S_OUTPUT: %m"); goto fail; }
    st.fb = open("/dev/graphics/fb10", O_RDWR | O_CLOEXEC);
    if (st.fb < 0) { ALOGE("open fb10: %m"); goto fail; }
    st.layer_w = st.layer_h = st.layer_on = 0;
    ALOGI("output on: %s %dx%d%s (%s)", st.mode->name, st.mode->w, st.mode->h,
          st.output == TV_OUTPUT_DVI ? " DVI" : "", st.automatic ? "auto" : "chosen");
    property_set("vendor.hdmi.state", "on");
    property_set("vendor.hdmi.mode", st.mode->name);
    return 0;
fail:
    tv_stop();
    return -1;
}

/* --- The gallery connection */

static int write_full(int fd, const void *p, size_t n) {
    while (n) {
        ssize_t r = send(fd, p, n, MSG_NOSIGNAL);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p = (const char *)p + r;
        n -= (size_t)r;
    }
    return 0;
}

static int read_full(int fd, void *p, size_t n) {
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        p = (char *)p + r;
        n -= (size_t)r;
    }
    return 0;
}

/*
 * A w x h picture from the socket into the buffer at (x, y), with rows stride
 * pixels apart. It arrives in Android's ARGB_8888 memory order (R, G, B, A);
 * the mixer wants B, G, R, A. The swap is done in a small cached chunk, then
 * copied out: the layer buffer is write-combined, so reading it back would be
 * slow.
 */
static int read_pixels(int fd, uint32_t *dst, int stride, int x, int y, int w, int h) {
    static uint32_t chunk[64 * 1024];
    int rows = (int)(sizeof(chunk) / 4) / w;
    for (int r = 0; r < h; r += rows) {
        int n = h - r < rows ? h - r : rows;
        if (read_full(fd, chunk, (size_t)n * w * 4) < 0) return -1;
        for (int i = 0; i < n * w; i++) {
            uint32_t p = chunk[i];
            chunk[i] = (p & 0xff00ff00u) | (p & 0xffu) << 16 | (p >> 16 & 0xffu);
        }
        for (int k = 0; k < n; k++) memcpy(dst + (size_t)(y + r + k) * stride + x, chunk + (size_t)k * w, (size_t)w * 4);
    }
    return 0;
}

static int skip(int fd, size_t n) {
    char tmp[4096];
    while (n) {
        size_t k = n < sizeof(tmp) ? n : sizeof(tmp);
        if (read_full(fd, tmp, k) < 0) return -1;
        n -= k;
    }
    return 0;
}

/* Tells a client whether a TV is on, its mode, and the modes it can choose. */
static void send_state_to(const struct client *cl) {
    if (cl->fd < 0) return;
    char text[2048];
    int n = 0;
    int on = st.tv >= 0;
    n += snprintf(text + n, sizeof(text) - n, "connected=%d\n", on);
    if (on) {
        const struct hdmi_mode *m = st.mode;
        n += snprintf(text + n, sizeof(text) - n, "mode=%s\nwidth=%d\nheight=%d\ndar=%d:%d\nauto=%d\ndvi=%d\nsink=%s\nmodes=",
                      m->name, m->w, m->h, m->dar_w, m->dar_h, st.automatic, st.output == TV_OUTPUT_DVI, st.edid.name);
        /* name:1 when the sink lists it */
        for (int i = 0; i < HDMI_MODE_COUNT && n < (int)sizeof(text) - 32; i++)
            n += snprintf(text + n, sizeof(text) - n, "%s%s:%d", i ? "," : "", hdmi_modes[i].name, sink_lists(&hdmi_modes[i]));
        n += snprintf(text + n, sizeof(text) - n, "\n");
    }
    struct hdmid_msg h = {HDMID_MAGIC, HDMID_STATE, (uint32_t)n, {0}};
    if (write_full(cl->fd, &h, sizeof(h)) < 0 || write_full(cl->fd, text, (size_t)n) < 0)
        ALOGW("state to %s: %m", cl == &st.camera ? "the camera" : "the gallery");
}

static void send_state(void) {
    send_state_to(&st.gallery);
    send_state_to(&st.camera);
}

/*
 * A w x h picture follows on the socket: into the buffer not on screen, centred
 * in a full-screen layer, then shown at the next vsync. The layer never changes
 * size for a new photo, so nothing blanks. With HDMID_SHOW_HALF the picture is
 * for a half-size layer the mixer doubles: cheap frames while a zoom or pan
 * moves. Ignored while the camera's live view is on.
 */
static int show_photo(int fd, uint32_t w, uint32_t h, uint32_t len, uint32_t flags) {
    int half = (flags & HDMID_SHOW_HALF) != 0;
    /* st.mode is NULL until the first plug-in */
    if (st.tv < 0 || !st.mode || st.vo >= 0) return skip(fd, len);
    int lw = half ? st.mode->w / 2 : st.mode->w, lh = half ? st.mode->h / 2 : st.mode->h;
    if (w == 0 || h == 0 || w > (uint32_t)lw || h > (uint32_t)lh || len != w * h * 4) {
        ALOGW("picture %ux%u (%u bytes) refused", w, h, len);
        return skip(fd, len);
    }
    if (alloc_photos() < 0) return skip(fd, len);
    int back = st.nphoto == 2 && st.front >= 0 ? 1 - st.front : 0;
    struct buf *b = &st.photo[back];
    /* One buffer only: it is the one on screen, so off while it is rewritten */
    int blank = st.front < 0 || back == st.front;
    if (blank) layer_blank();
    int x = (lw - (int)w) / 2, y = (lh - (int)h) / 2;
    /* The bars around it are transparent (the mixer's black background shows):
     * cleared when the buffer last held another layout */
    if (st.layout[back].lw != lw || st.layout[back].lh != lh || st.layout[back].x != x || st.layout[back].y != y ||
        st.layout[back].w != (int)w || st.layout[back].h != (int)h)
        memset(b->map, 0, (size_t)lw * lh * 4);
    if (read_pixels(fd, b->map, lw, x, y, (int)w, (int)h) < 0) {
        st.layout[back].lw = 0;
        return -1;
    }
    st.layout[back].lw = lw;
    st.layout[back].lh = lh;
    st.layout[back].x = x;
    st.layout[back].y = y;
    st.layout[back].w = (int)w;
    st.layout[back].h = (int)h;
    if (layer_config(lw, lh, 0, 0, half, blank) == 0 && layer_show(b) == 0) st.front = back;
    return 0;
}

static void set_mode(const char *name) {
    const struct hdmi_mode *m = hdmi_mode_by_name(name);
    property_set(PROP_MODE, m ? name : "auto");
    if (st.tv < 0) return;
    /* The same choice again: no restart, the TV would re-sync for nothing */
    if (m ? (!st.automatic && m == st.mode) : st.automatic) return;
    tv_stop();
    /* The camera's next frame brings the live view back */
    if (tv_start() == 0 && st.gallery.fd < 0) show_idle();
}

static void drop_client(struct client *cl) {
    if (cl->fd < 0) return;
    close(cl->fd);
    cl->fd = -1;
    ALOGI("%s gone", cl == &st.camera ? "camera" : "gallery");
    /* The frames' memory may go with the sender: the video layer must not read it */
    if (st.video_owner == cl) {
        st.video_owner = NULL;
        video_stop();
    }
    show_idle();
}

/* One message from a client; -1 when the connection is over. */
static int handle_client(struct client *cl) {
    struct hdmid_msg h;
    int fd = cl->fd;
    if (read_full(fd, &h, sizeof(h)) < 0 || h.magic != HDMID_MAGIC) return -1;
    switch (h.type) {
    case HDMID_HELLO:
        if (skip(fd, h.len) < 0) return -1;
        send_state_to(cl);
        return 0;
    case HDMID_SHOW:
        return show_photo(fd, h.a[0], h.a[1], h.len, h.a[2]);
    case HDMID_CLEAR:
        if (skip(fd, h.len) < 0) return -1;
        show_idle();
        return 0;
    case HDMID_SET_MODE: {
        char name[32] = "";
        if (h.len >= sizeof(name)) return -1;
        if (read_full(fd, name, h.len) < 0) return -1;
        name[h.len] = '\0';
        set_mode(name);
        send_state();
        return 0;
    }
    case HDMID_VIDEO:
        if (skip(fd, h.len) < 0) return -1;
        video_frame(cl, &h);
        return 0;
    case HDMID_VIDEO_STOP:
        if (skip(fd, h.len) < 0) return -1;
        video_stop();
        return 0;
    default:
        return skip(fd, h.len);
    }
}

/*
 * Only GC Gallery, the camera HAL (and root, system and shell, for tests) may
 * drive the TV. The uid, or -1 when refused.
 */
static long allowed(int fd) {
    struct ucred cred;
    socklen_t l = sizeof(cred);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &l) < 0) return -1;
    if (cred.uid == 0 || cred.uid == 1000 || cred.uid == 2000 || cred.uid == AID_CAMERASERVER) return cred.uid;
    FILE *f = fopen("/data/system/packages.list", "re");
    if (!f) return -1;
    char line[512], pkg[256];
    unsigned uid;
    int ok = 0;
    while (!ok && fgets(line, sizeof(line), f))
        if (sscanf(line, "%255s %u", pkg, &uid) == 2 && !strcmp(pkg, GALLERY_PACKAGE) && uid == cred.uid) ok = 1;
    fclose(f);
    if (!ok) ALOGW("uid %u may not use the TV", cred.uid);
    return ok ? (long)cred.uid : -1;
}

/*
 * The camera HAL has its own slot, so the gallery and the camera can overlap
 * while one activity hands over to the other. Anyone else takes the gallery's.
 */
static void accept_client(int lsock) {
    int fd = accept4(lsock, NULL, NULL, SOCK_CLOEXEC);
    if (fd < 0) return;
    long uid = allowed(fd);
    if (uid < 0) {
        close(fd);
        return;
    }
    /* A stalled sender must not hang the daemon */
    struct timeval tv = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct client *cl = uid == AID_CAMERASERVER ? &st.camera : &st.gallery;
    if (cl->fd >= 0) {
        /* Replaced: what it showed stays until the new client sends, but not frames
         * from memory that may go with it */
        close(cl->fd);
        if (st.video_owner == cl) {
            st.video_owner = NULL;
            video_stop();
        }
    }
    cl->fd = fd;
    cl->uid = (uid_t)uid;
    ALOGI("%s connected", cl == &st.camera ? "camera" : "gallery");
    send_state_to(cl);
}

/*
 * --- Hot-plug and the camera's own screen. The output is on while a sink is
 * plugged in and the screen is on: with the screen off nobody is showing
 * anything, and the output would keep the memory bus at up to 400 MHz.
 */

static int plugged;
static int screen_on = 1;

static void update_output(void) {
    int want = plugged && screen_on;
    if (want == (st.tv >= 0)) return;
    if (want) {
        if (tv_start() < 0) return;
        /* The gallery sends its photo again on the state below, the camera its next frame */
        if (st.gallery.fd < 0) show_idle();
    } else {
        tv_stop();
        ALOGI("output off (%s)", plugged ? "screen off" : "unplugged");
    }
    send_state();
}

static void set_plugged(int p) {
    if (p == plugged) return;
    plugged = p;
    if (p) {
        /* Read once per plug-in; a screen off and on keeps it */
        read_edid();
        property_set("vendor.hdmi.sink", st.edid.name);
    } else {
        memset(&st.edid, 0, sizeof(st.edid));
        property_set("vendor.hdmi.sink", "");
    }
    update_output();
}

/*
 * The screen state comes from system_server: DisplayPowerController sets
 * debug.tracing.screen_state on every change (android.view.Display states:
 * 1 off, 2 on, 3 doze, 4 doze-suspend, 5 VR, 6 on-suspend). A thread sleeps
 * in __system_property_wait() and passes each change down a pipe.
 */
#define PROP_SCREEN "debug.tracing.screen_state"
static int screen_pipe[2] = {-1, -1};

static char screen_value(void) {
    char v[PROPERTY_VALUE_MAX];
    property_get(PROP_SCREEN, v, "");
    int s = atoi(v);
    /* Unset (early boot) counts as on */
    return (char)(!v[0] || s == 2 || s == 5 || s == 6);
}

static void serial_of(void *cookie, const char *name, const char *value, uint32_t serial) {
    (void)name;
    (void)value;
    *(uint32_t *)cookie = serial;
}

static void *screen_watch(void *arg) {
    (void)arg;
    const prop_info *pi;
    /* Not set before system_server turns the display on: wait for it to appear.
     * The first wait returns at once with the current global serial. */
    uint32_t global = 0;
    while (!(pi = __system_property_find(PROP_SCREEN))) __system_property_wait(NULL, global, &global, NULL);
    uint32_t serial = 0;
    __system_property_read_callback(pi, serial_of, &serial);
    for (;;) {
        char on = screen_value();
        if (write(screen_pipe[1], &on, 1) < 0 && errno != EINTR) return NULL;
        while (!__system_property_wait(pi, serial, &serial, NULL)) {}
    }
}

static void handle_screen(void) {
    char buf[16];
    ssize_t n = read(screen_pipe[0], buf, sizeof(buf));
    if (n <= 0) return;
    /* The last word wins */
    int on = buf[n - 1] != 0;
    if (on == screen_on) return;
    screen_on = on;
    update_output();
}

static int read_state(void) {
    char buf[8] = "";
    int fd = open(HDMI_STATE, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    read(fd, buf, sizeof(buf) - 1);
    close(fd);
    return atoi(buf) != 0;
}

static void handle_uevent(int sock) {
    char msg[2048];
    ssize_t n = recv(sock, msg, sizeof(msg) - 1, 0);
    if (n < 0 && errno == ENOBUFS) {
        /* Events were lost (a uevent storm): the hdmi switch may have been one */
        set_plugged(read_state());
        return;
    }
    if (n <= 0) return;
    msg[n] = '\0';
    /* NUL-separated KEY=value pairs after "action@path" */
    int is_hdmi = 0, state = -1;
    for (char *p = msg; p < msg + n; p += strlen(p) + 1) {
        if (!strcmp(p, "SWITCH_NAME=hdmi")) is_hdmi = 1;
        else if (!strncmp(p, "SWITCH_STATE=", 13)) state = atoi(p + 13);
    }
    if (is_hdmi && state >= 0) set_plugged(state != 0);
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    st.photo[0] = st.photo[1] = st.idle = no_buf;
    st.ion = open("/dev/ion", O_RDWR | O_CLOEXEC);
    if (st.ion < 0) { ALOGE("open /dev/ion: %m"); return 1; }

    int uev = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (uev < 0) { ALOGE("uevent socket: %m"); return 1; }
    struct sockaddr_nl addr = {.nl_family = AF_NETLINK, .nl_pid = 0, .nl_groups = 1};
    if (bind(uev, (struct sockaddr *)&addr, sizeof(addr)) < 0) { ALOGE("bind: %m"); return 1; }

    int lsock = android_get_control_socket("hdmid");
    if (lsock < 0) {
        /* Started by hand (a test build run as root): make the socket ourselves */
        lsock = socket_local_server("hdmid", ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
        if (lsock >= 0) chmod("/dev/socket/hdmid", 0666);
    }
    if (lsock < 0 || listen(lsock, 2) < 0) { ALOGE("control socket: %m"); return 1; }

    /* Whatever a previous run left */
    property_set("vendor.hdmi.state", "off");
    property_set("vendor.hdmi.mode", "");
    property_set("vendor.hdmi.sink", "");
    screen_on = screen_value();
    pthread_t watcher;
    if (pipe2(screen_pipe, O_CLOEXEC | O_NONBLOCK) < 0 || pthread_create(&watcher, NULL, screen_watch, NULL)) {
        ALOGE("screen state watch: %m");
        return 1;
    }

    /* Bind first, then read the state, so a plug-in in between is not lost */
    set_plugged(read_state());

    for (;;) {
        /* A negative fd is ignored by poll() */
        struct pollfd p[5] = {{uev, POLLIN, 0}, {lsock, POLLIN, 0}, {screen_pipe[0], POLLIN, 0},
                              {st.gallery.fd, POLLIN, 0}, {st.camera.fd, POLLIN, 0}};
        if (poll(p, 5, -1) < 0) {
            if (errno == EINTR) continue;
            ALOGE("poll: %m");
            return 1;
        }
        if (p[0].revents & POLLIN) handle_uevent(uev);
        if (p[1].revents & POLLIN) accept_client(lsock);
        if (p[2].revents & POLLIN) handle_screen();
        struct client *cls[2] = {&st.gallery, &st.camera};
        for (int i = 0; i < 2; i++) {
            struct client *cl = cls[i];
            /* Only if the fd polled is still the client's: an accept may have replaced it */
            if (p[3 + i].fd < 0 || !p[3 + i].revents || p[3 + i].fd != cl->fd) continue;
            if ((p[3 + i].revents & (POLLHUP | POLLERR)) && !(p[3 + i].revents & POLLIN)) drop_client(cl);
            else if (handle_client(cl) < 0) drop_client(cl);
        }
    }
}

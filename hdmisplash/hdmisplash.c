/*
 * HDMI placeholder screen for the micro-HDMI port.
 *
 * There is no HDMI support in the display stack yet, so while a TV or monitor
 * is connected this daemon shows a fixed "support is coming" screen on it.
 * It sleeps on the kernel's uevent socket and wakes only when the hdmi switch
 * (HPD) changes: on plug-in it starts the s5p-tvout output at 720p60 in RGB
 * (/dev/video16) and puts the screen on mixer graphic layer 0
 * (/dev/graphics/fb10); on unplug it frees everything, which stops the output.
 *
 * The layer needs physically contiguous memory (CONFIG_USER_ALLOC_TVOUT), so
 * the buffer comes from the ION exynos contig heap. bringup-tools/hdmitest.c
 * is the standalone test this is based on.
 */

#define LOG_TAG "hdmisplash"

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/netlink.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

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
#define ION_EXYNOS_CUSTOM_PHYS 1

/* s5p-tvout private ABI (drivers/media/video/samsung/tvout) */
struct s5ptvfb_user_window { int x; int y; };
#define S5PTVFB_WIN_POSITION _IOW('F', 213, struct s5ptvfb_user_window)
#define S5PTVFB_WIN_SET_ADDR _IOW('F', 219, uint32_t)
#define TV_STD_720P_60 ((v4l2_std_id)0x08000000)
/* Output 2 is HDMI RGB. With output 1 (YCbCr) a test TV read the signal as
 * RGB and showed black as purple. */
#define TV_OUTPUT_HDMI_RGB 2

#define HDMI_STATE "/sys/class/switch/hdmi/state"
#define W 1280
#define H 720
#define SIZE (W * H * 4)

/* 5x7 glyphs, one byte per row, bit 4 = leftmost pixel */
static const struct { char c; unsigned char rows[7]; } font[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}}, {'G', {14, 17, 16, 23, 17, 17, 15}},
    {'H', {17, 17, 17, 31, 17, 17, 17}}, {'I', {14, 4, 4, 4, 4, 4, 14}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'R', {30, 17, 17, 30, 20, 18, 17}}, {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},       {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},  {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},      {' ', {0, 0, 0, 0, 0, 0, 0}},
};

static const unsigned char *glyph(char c) {
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++)
        if (font[i].c == c) return font[i].rows;
    return font[sizeof(font) / sizeof(font[0]) - 1].rows;
}

static void draw_text(uint32_t *px, const char *s, int y, int scale, uint32_t color) {
    int x = (W - (int)strlen(s) * 6 * scale) / 2;
    for (; *s; s++, x += 6 * scale) {
        const unsigned char *g = glyph(*s);
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++)
                if (g[r] & (16 >> c))
                    for (int dy = 0; dy < scale; dy++)
                        for (int dx = 0; dx < scale; dx++)
                            px[(y + r * scale + dy) * W + x + c * scale + dx] = color;
    }
}

static struct {
    int tv, ion, buf, fb;
    void *handle;
    uint32_t *px;
} out = {-1, -1, -1, -1, NULL, MAP_FAILED};

static void hide(void) {
    if (out.fb >= 0) {
        ioctl(out.fb, FBIOBLANK, FB_BLANK_POWERDOWN);
        close(out.fb);
    }
    if (out.px != MAP_FAILED) munmap(out.px, SIZE);
    if (out.buf >= 0) close(out.buf);
    if (out.handle) {
        struct ion_handle_data hd = {out.handle};
        ioctl(out.ion, ION_IOC_FREE, &hd);
    }
    if (out.ion >= 0) close(out.ion);
    /* the last close of the TV interface stops the HDMI output */
    if (out.tv >= 0) close(out.tv);
    out.tv = out.ion = out.buf = out.fb = -1;
    out.handle = NULL;
    out.px = MAP_FAILED;
}

static int show(void) {
    out.tv = open("/dev/video16", O_RDWR | O_CLOEXEC);
    if (out.tv < 0) { ALOGE("open /dev/video16: %m"); return -1; }
    v4l2_std_id std = TV_STD_720P_60;
    int output = TV_OUTPUT_HDMI_RGB;
    if (ioctl(out.tv, VIDIOC_S_STD, &std) < 0) { ALOGE("VIDIOC_S_STD: %m"); return -1; }
    if (ioctl(out.tv, VIDIOC_S_OUTPUT, &output) < 0) { ALOGE("VIDIOC_S_OUTPUT: %m"); return -1; }

    out.ion = open("/dev/ion", O_RDWR | O_CLOEXEC);
    if (out.ion < 0) { ALOGE("open /dev/ion: %m"); return -1; }
    struct ion_allocation_data alloc = {SIZE, 4096, ION_HEAP_EXYNOS_CONTIG_MASK, NULL};
    if (ioctl(out.ion, ION_IOC_ALLOC, &alloc) < 0) { ALOGE("ION_IOC_ALLOC: %m"); return -1; }
    out.handle = alloc.handle;
    struct ion_fd_data share = {alloc.handle, -1};
    if (ioctl(out.ion, ION_IOC_SHARE, &share) < 0) { ALOGE("ION_IOC_SHARE: %m"); return -1; }
    out.buf = share.fd;
    struct ion_phys_data phys = {out.buf, 0, 0};
    struct ion_custom_data custom = {ION_EXYNOS_CUSTOM_PHYS, (unsigned long)&phys};
    if (ioctl(out.ion, ION_IOC_CUSTOM, &custom) < 0) { ALOGE("ION phys: %m"); return -1; }
    out.px = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, out.buf, 0);
    if (out.px == MAP_FAILED) { ALOGE("mmap: %m"); return -1; }

    /* ARGB8888 */
    for (int i = 0; i < W * H; i++) out.px[i] = 0xff000000;
    draw_text(out.px, "GALAXY CAMERA REVIVED", 250, 7, 0xffffffff);
    draw_text(out.px, "HDMI SUPPORT IS COMING IN A FUTURE RELEASE", 400, 4, 0xffffd040);
    msync(out.px, SIZE, MS_SYNC);

    out.fb = open("/dev/graphics/fb10", O_RDWR | O_CLOEXEC);
    if (out.fb < 0) { ALOGE("open fb10: %m"); return -1; }
    struct fb_var_screeninfo var;
    if (ioctl(out.fb, FBIOGET_VSCREENINFO, &var) < 0) { ALOGE("FBIOGET_VSCREENINFO: %m"); return -1; }
    var.xres = var.xres_virtual = W;
    var.yres = var.yres_virtual = H;
    var.xoffset = var.yoffset = 0;
    var.bits_per_pixel = 32;
    var.blue.offset = 0;    var.blue.length = 8;
    var.green.offset = 8;   var.green.length = 8;
    var.red.offset = 16;    var.red.length = 8;
    var.transp.offset = 24; var.transp.length = 8;
    var.activate = FB_ACTIVATE_FORCE;
    struct s5ptvfb_user_window pos = {0, 0};
    if (ioctl(out.fb, FBIOPUT_VSCREENINFO, &var) < 0) { ALOGE("FBIOPUT_VSCREENINFO: %m"); return -1; }
    if (ioctl(out.fb, S5PTVFB_WIN_SET_ADDR, (uint32_t)phys.phys) < 0) { ALOGE("WIN_SET_ADDR: %m"); return -1; }
    if (ioctl(out.fb, S5PTVFB_WIN_POSITION, &pos) < 0) { ALOGE("WIN_POSITION: %m"); return -1; }
    if (ioctl(out.fb, FBIOBLANK, FB_BLANK_UNBLANK) < 0) { ALOGE("FBIOBLANK: %m"); return -1; }
    return 0;
}

static void set_connected(int connected) {
    if (connected == (out.tv >= 0)) return;
    if (connected) {
        if (show() < 0) {
            hide();
            return;
        }
        ALOGI("HDMI connected: showing the placeholder screen");
    } else {
        hide();
        ALOGI("HDMI disconnected: output off");
    }
}

static int read_state(void) {
    char buf[8] = "";
    int fd = open(HDMI_STATE, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    read(fd, buf, sizeof(buf) - 1);
    close(fd);
    return atoi(buf) != 0;
}

int main(void) {
    int sock = socket(AF_NETLINK, SOCK_DGRAM | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
    if (sock < 0) { ALOGE("uevent socket: %m"); return 1; }
    struct sockaddr_nl addr = {.nl_family = AF_NETLINK, .nl_pid = 0, .nl_groups = 1};
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) { ALOGE("bind: %m"); return 1; }

    /* Bind first, then read the state, so a plug-in in between is not lost */
    set_connected(read_state());

    char msg[2048];
    for (;;) {
        ssize_t n = recv(sock, msg, sizeof(msg) - 1, 0);
        if (n <= 0) continue;
        msg[n] = '\0';
        /* The message is NUL-separated KEY=value pairs after "action@path" */
        int is_hdmi = 0, state = -1;
        for (char *p = msg; p < msg + n; p += strlen(p) + 1) {
            if (!strcmp(p, "SWITCH_NAME=hdmi")) is_hdmi = 1;
            else if (!strncmp(p, "SWITCH_STATE=", 13)) state = atoi(p + 13);
        }
        if (is_hdmi && state >= 0) set_connected(state != 0);
    }
}

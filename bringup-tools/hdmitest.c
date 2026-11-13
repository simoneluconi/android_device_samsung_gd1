/*
 * HDMI test for the micro-HDMI port.
 *
 * Starts the s5p-tvout HDMI output (/dev/video16) in the given mode and shows
 * a test screen on mixer graphic layer 0 (/dev/graphics/fb10), sized to the
 * mode. The layer needs physically contiguous memory (CONFIG_USER_ALLOC_TVOUT),
 * so the buffer comes from the ION exynos contig heap. Closing /dev/video16
 * stops the output. Run "stop vendor.hdmid" first: the daemon owns the output
 * while a TV is connected.
 *
 * The test screen checks what a capture card can measure:
 *   - a 1-pixel white border and coloured corner squares (white top left, red
 *     top right, green bottom left, blue bottom right): cropping, overscan and
 *     orientation;
 *   - 100% colour bars (white, yellow, cyan, green, magenta, red, blue, black);
 *   - a 16-step grey ramp from 0 to 255: RGB range (full or limited);
 *   - a ring that is round on the display, drawn with the mode's pixel aspect
 *     ratio: 480p and 576p have non-square pixels.
 *
 * Usage: gd1_hdmitest list
 *        gd1_hdmitest <mode> [seconds [output [bpp]]]
 *        (default 120 s, output 2 = HDMI RGB, bpp 32 = ARGB8888 or 16 = RGB565)
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "font5x7.h"
#include "hdmi_modes.h"

// Old (3.4) ION userspace ABI, 32-bit
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
// Write-combined mapping: without it the CPU's writes can sit in its caches
// while the mixer reads the buffer from memory
#define ION_EXYNOS_NONCACHE_MASK (1 << 30)
#define ION_EXYNOS_CUSTOM_PHYS 1

// s5p-tvout private ABI (drivers/media/video/samsung/tvout)
struct s5ptvfb_user_window { int x; int y; };
#define S5PTVFB_WIN_POSITION _IOW('F', 213, struct s5ptvfb_user_window)
#define S5PTVFB_WIN_SET_ADDR _IOW('F', 219, uint32_t)
// Output 1 is HDMI YCbCr, 2 HDMI RGB. With YCbCr the test TV showed black as
// purple (it read the YCbCr signal as RGB), so RGB is the default.
#define TV_OUTPUT_HDMI_RGB 2

static volatile int stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

static int W, H;

static void fill(uint32_t *px, int x0, int y0, int w, int h, uint32_t color) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) px[y * W + x] = color;
}

static void draw_pattern(uint32_t *px, const struct hdmi_mode *m) {
    fill(px, 0, 0, W, H, 0xff000000);

    // Border and corner squares
    fill(px, 0, 0, W, 1, 0xffffffff);
    fill(px, 0, H - 1, W, 1, 0xffffffff);
    fill(px, 0, 0, 1, H, 0xffffffff);
    fill(px, W - 1, 0, 1, H, 0xffffffff);
    int sq = H / 27;
    fill(px, 4, 4, sq, sq, 0xffffffff);
    fill(px, W - 4 - sq, 4, sq, sq, 0xffff0000);
    fill(px, 4, H - 4 - sq, sq, sq, 0xff00ff00);
    fill(px, W - 4 - sq, H - 4 - sq, sq, sq, 0xff0000ff);

    // Title: mode name and size
    char title[64];
    snprintf(title, sizeof(title), "%s %dX%d", m->name, W, H);
    for (char *p = title; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p -= 32;
    int scale = H / 120;
    font5x7_draw(px, W, H, title, (W - font5x7_width(title, scale)) / 2, H * 6 / 100, scale, scale,
                 0xffffffff);

    // Colour bars
    static const uint32_t bars[8] = {0xffffffff, 0xffffff00, 0xff00ffff, 0xff00ff00,
                                     0xffff00ff, 0xffff0000, 0xff0000ff, 0xff000000};
    int y0 = H * 22 / 100, bh = H * 30 / 100, margin = W / 16;
    int bw = (W - 2 * margin) / 8;
    for (int i = 0; i < 8; i++) fill(px, margin + i * bw, y0, bw, bh, bars[i]);

    // Grey ramp, 16 steps from 0 to 255
    y0 = H * 56 / 100;
    bh = H * 10 / 100;
    bw = (W - 2 * margin) / 16;
    for (int i = 0; i < 16; i++) {
        uint32_t g = i * 17;
        fill(px, margin + i * bw, y0, bw, bh, 0xff000000 | g << 16 | g << 8 | g);
    }

    // Ring, round on the display: x radius = y radius / pixel aspect ratio
    double par = (double)m->dar_w * H / ((double)m->dar_h * W);
    double ry = H * 0.12, rx = ry / par, cx = W / 2.0, cy = H * 0.82;
    // At least 3 rows and 6 columns thick, so 4:2:2 capture chroma keeps it yellow
    double t = fmax(3.0 / ry, 6.0 / rx);
    for (int y = (int)(cy - ry - 4); y <= (int)(cy + ry + 4); y++)
        for (int x = (int)(cx - rx - 4); x <= (int)(cx + rx + 4); x++) {
            double dx = (x - cx) / rx, dy = (y - cy) / ry;
            double d = sqrt(dx * dx + dy * dy);
            if (d <= 1.0 && d >= 1.0 - t) px[y * W + x] = 0xffffd040;
        }
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "list")) {
        for (int i = 0; i < HDMI_MODE_COUNT; i++)
            printf("%-11s %4dx%-4d VIC %2d %d:%d %s\n", hdmi_modes[i].name, hdmi_modes[i].w,
                   hdmi_modes[i].h, hdmi_modes[i].vic, hdmi_modes[i].dar_w, hdmi_modes[i].dar_h,
                   hdmi_modes[i].interlaced ? "interlaced" : "");
        return argc < 2;
    }
    const struct hdmi_mode *m = hdmi_mode_by_name(argv[1]);
    if (!m) { fprintf(stderr, "unknown mode %s (see: gd1_hdmitest list)\n", argv[1]); return 1; }
    W = m->w;
    H = m->h;
    int seconds = argc > 2 ? atoi(argv[2]) : 120;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int tv = open("/dev/video16", O_RDWR);
    if (tv < 0) { perror("open /dev/video16"); return 1; }
    v4l2_std_id std = m->std;
    if (ioctl(tv, VIDIOC_S_STD, &std) < 0) { perror("VIDIOC_S_STD"); return 1; }
    int out = argc > 3 ? atoi(argv[3]) : TV_OUTPUT_HDMI_RGB;
    if (ioctl(tv, VIDIOC_S_OUTPUT, &out) < 0) { perror("VIDIOC_S_OUTPUT"); return 1; }
    printf("HDMI output started: %s %dx%d\n", m->name, W, H);

    int bpp = argc > 4 ? atoi(argv[4]) : 32;
    if (bpp != 32 && bpp != 16) { fprintf(stderr, "bpp must be 32 or 16\n"); return 1; }

    int ion = open("/dev/ion", O_RDWR);
    if (ion < 0) { perror("open /dev/ion"); return 1; }
    size_t size = (size_t)W * H * bpp / 8;
    struct ion_allocation_data alloc = {size, 4096, ION_HEAP_EXYNOS_CONTIG_MASK | ION_EXYNOS_NONCACHE_MASK, NULL};
    if (ioctl(ion, ION_IOC_ALLOC, &alloc) < 0) { perror("ION_IOC_ALLOC"); return 1; }
    struct ion_fd_data share = {alloc.handle, -1};
    if (ioctl(ion, ION_IOC_SHARE, &share) < 0) { perror("ION_IOC_SHARE"); return 1; }
    struct ion_phys_data phys = {share.fd, 0, 0};
    struct ion_custom_data custom = {ION_EXYNOS_CUSTOM_PHYS, (unsigned long)&phys};
    if (ioctl(ion, ION_IOC_CUSTOM, &custom) < 0) { perror("ION phys"); return 1; }
    printf("buffer: phys 0x%08lx, %zu bytes\n", phys.phys, phys.size);
    void *mem = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, share.fd, 0);
    if (mem == MAP_FAILED) { perror("mmap"); return 1; }

    if (bpp == 32) {
        draw_pattern(mem, m);
    } else {
        uint32_t *px = malloc((size_t)W * H * 4);
        if (!px) { perror("malloc"); return 1; }
        draw_pattern(px, m);
        uint16_t *o = mem;
        for (int i = 0; i < W * H; i++) {
            uint32_t c = px[i];
            o[i] = (uint16_t)(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
        }
        free(px);
    }
    msync(mem, size, MS_SYNC);

    int fb = open("/dev/graphics/fb10", O_RDWR);
    if (fb < 0) { perror("open fb10"); return 1; }
    struct fb_var_screeninfo var;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &var) < 0) { perror("FBIOGET_VSCREENINFO"); return 1; }
    var.xres = var.xres_virtual = W;
    var.yres = var.yres_virtual = H;
    var.xoffset = var.yoffset = 0;
    var.bits_per_pixel = bpp;
    if (bpp == 32) {
        var.blue.offset = 0;    var.blue.length = 8;
        var.green.offset = 8;   var.green.length = 8;
        var.red.offset = 16;    var.red.length = 8;
        var.transp.offset = 24; var.transp.length = 8;
    } else {
        var.blue.offset = 0;    var.blue.length = 5;
        var.green.offset = 5;   var.green.length = 6;
        var.red.offset = 11;    var.red.length = 5;
        var.transp.offset = 0;  var.transp.length = 0;
    }
    var.activate = FB_ACTIVATE_FORCE;
    if (ioctl(fb, FBIOPUT_VSCREENINFO, &var) < 0) { perror("FBIOPUT_VSCREENINFO"); return 1; }
    if (ioctl(fb, S5PTVFB_WIN_SET_ADDR, (uint32_t)phys.phys) < 0) { perror("WIN_SET_ADDR"); return 1; }
    struct s5ptvfb_user_window pos = {0, 0};
    if (ioctl(fb, S5PTVFB_WIN_POSITION, &pos) < 0) { perror("WIN_POSITION"); return 1; }
    if (ioctl(fb, FBIOBLANK, FB_BLANK_UNBLANK) < 0) { perror("FBIOBLANK unblank"); return 1; }
    printf("layer on; showing for %d s\n", seconds);
    fflush(stdout);

    for (int i = 0; i < seconds && !stop; i++) sleep(1);

    ioctl(fb, FBIOBLANK, FB_BLANK_POWERDOWN);
    close(fb);
    munmap(mem, size);
    close(share.fd);
    struct ion_handle_data hd = {alloc.handle};
    ioctl(ion, ION_IOC_FREE, &hd);
    close(ion);
    close(tv); // last close of the TV interface stops the HDMI output
    printf("stopped\n");
    return 0;
}

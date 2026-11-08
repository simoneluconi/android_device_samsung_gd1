/*
 * HDMI bring-up test for the micro-HDMI port.
 *
 * Starts the s5p-tvout HDMI output at 720p60 (/dev/video16) and shows a colour
 * test screen on mixer graphic layer 0 (/dev/graphics/fb10). The layer needs
 * physically contiguous memory (CONFIG_USER_ALLOC_TVOUT), so the buffer comes
 * from the ION exynos contig heap. Closing /dev/video16 stops the output.
 *
 * Usage: gd1_hdmitest [seconds [output]]   (default 120 s, output 2 = HDMI RGB)
 * Verified 2026-11-08 on a TV: text and red, green and blue bars correct.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

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
#define ION_EXYNOS_CUSTOM_PHYS 1

// s5p-tvout private ABI (drivers/media/video/samsung/tvout)
struct s5ptvfb_user_window { int x; int y; };
#define S5PTVFB_WIN_POSITION _IOW('F', 213, struct s5ptvfb_user_window)
#define S5PTVFB_WIN_SET_ADDR _IOW('F', 219, uint32_t)
#define TV_STD_720P_60 ((v4l2_std_id)0x08000000)
// Output 1 is HDMI YCbCr, 2 HDMI RGB. With YCbCr the test TV showed black as
// purple (it read the YCbCr signal as RGB), so RGB is the default.
#define TV_OUTPUT_HDMI_RGB 2

#define W 1280
#define H 720

static volatile int stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

// 5x7 glyphs, one byte per row, bit 4 = leftmost pixel
static const struct { char c; unsigned char rows[7]; } font[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'C', {14, 17, 16, 16, 16, 17, 14}},
    {'B', {30, 17, 17, 30, 17, 17, 30}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'D', {30, 17, 17, 17, 17, 17, 30}}, {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},      {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}}, {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'R', {30, 17, 17, 30, 20, 18, 17}}, {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},  {'Y', {17, 17, 10, 4, 4, 4, 4}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},     {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {' ', {0, 0, 0, 0, 0, 0, 0}},
};

static const unsigned char *glyph(char c) {
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++)
        if (font[i].c == c) return font[i].rows;
    return font[sizeof(font) / sizeof(font[0]) - 1].rows;
}

static void draw_text_at(uint32_t *px, const char *s, int x, int y, int scale, uint32_t color) {
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

static void draw_text(uint32_t *px, const char *s, int y, int scale, uint32_t color) {
    draw_text_at(px, s, (W - (int)strlen(s) * 6 * scale) / 2, y, scale, color);
}

static void fill(uint32_t *px, int x0, int y0, int w, int h, uint32_t color) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) px[y * W + x] = color;
}

int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 120;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    int tv = open("/dev/video16", O_RDWR);
    if (tv < 0) { perror("open /dev/video16"); return 1; }
    v4l2_std_id std = TV_STD_720P_60;
    if (ioctl(tv, VIDIOC_S_STD, &std) < 0) { perror("VIDIOC_S_STD"); return 1; }
    int out = argc > 2 ? atoi(argv[2]) : TV_OUTPUT_HDMI_RGB;
    if (ioctl(tv, VIDIOC_S_OUTPUT, &out) < 0) { perror("VIDIOC_S_OUTPUT"); return 1; }
    printf("HDMI output started at 720p60\n");

    int ion = open("/dev/ion", O_RDWR);
    if (ion < 0) { perror("open /dev/ion"); return 1; }
    size_t size = W * H * 4;
    struct ion_allocation_data alloc = {size, 4096, ION_HEAP_EXYNOS_CONTIG_MASK, NULL};
    if (ioctl(ion, ION_IOC_ALLOC, &alloc) < 0) { perror("ION_IOC_ALLOC"); return 1; }
    struct ion_fd_data share = {alloc.handle, -1};
    if (ioctl(ion, ION_IOC_SHARE, &share) < 0) { perror("ION_IOC_SHARE"); return 1; }
    struct ion_phys_data phys = {share.fd, 0, 0};
    struct ion_custom_data custom = {ION_EXYNOS_CUSTOM_PHYS, (unsigned long)&phys};
    if (ioctl(ion, ION_IOC_CUSTOM, &custom) < 0) { perror("ION phys"); return 1; }
    printf("buffer: phys 0x%08lx, %zu bytes\n", phys.phys, phys.size);
    uint32_t *px = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, share.fd, 0);
    if (px == MAP_FAILED) { perror("mmap"); return 1; }

    // ARGB8888 colour test: black background, pure red, green and blue bars
    fill(px, 0, 0, W, H, 0xff000000);
    draw_text(px, "GALAXY CAMERA REVIVED", 60, 6, 0xffffffff);
    fill(px, 80, 200, 320, 300, 0xffff0000);
    fill(px, 480, 200, 320, 300, 0xff00ff00);
    fill(px, 880, 200, 320, 300, 0xff0000ff);
    draw_text_at(px, "RED", 80 + (320 - 3 * 36) / 2, 560, 6, 0xffffffff);
    draw_text_at(px, "GREEN", 480 + (320 - 5 * 36) / 2, 560, 6, 0xffffffff);
    draw_text_at(px, "BLUE", 880 + (320 - 4 * 36) / 2, 560, 6, 0xffffffff);
    msync(px, size, MS_SYNC);

    int fb = open("/dev/graphics/fb10", O_RDWR);
    if (fb < 0) { perror("open fb10"); return 1; }
    struct fb_var_screeninfo var;
    if (ioctl(fb, FBIOGET_VSCREENINFO, &var) < 0) { perror("FBIOGET_VSCREENINFO"); return 1; }
    var.xres = var.xres_virtual = W;
    var.yres = var.yres_virtual = H;
    var.xoffset = var.yoffset = 0;
    var.bits_per_pixel = 32;
    var.blue.offset = 0;    var.blue.length = 8;
    var.green.offset = 8;   var.green.length = 8;
    var.red.offset = 16;    var.red.length = 8;
    var.transp.offset = 24; var.transp.length = 8;
    var.activate = FB_ACTIVATE_FORCE;
    if (ioctl(fb, FBIOPUT_VSCREENINFO, &var) < 0) { perror("FBIOPUT_VSCREENINFO"); return 1; }
    if (ioctl(fb, S5PTVFB_WIN_SET_ADDR, (uint32_t)phys.phys) < 0) { perror("WIN_SET_ADDR"); return 1; }
    struct s5ptvfb_user_window pos = {0, 0};
    if (ioctl(fb, S5PTVFB_WIN_POSITION, &pos) < 0) { perror("WIN_POSITION"); return 1; }
    if (ioctl(fb, FBIOBLANK, FB_BLANK_UNBLANK) < 0) { perror("FBIOBLANK unblank"); return 1; }
    printf("layer on; showing for %d s\n", seconds);

    for (int i = 0; i < seconds && !stop; i++) sleep(1);

    ioctl(fb, FBIOBLANK, FB_BLANK_POWERDOWN);
    close(fb);
    munmap(px, size);
    close(share.fd);
    struct ion_handle_data hd = {alloc.handle};
    ioctl(ion, ION_IOC_FREE, &hd);
    close(ion);
    close(tv); // last close of the TV interface stops the HDMI output
    printf("stopped\n");
    return 0;
}

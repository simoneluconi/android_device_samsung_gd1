/*
 * Test of hdmid's live view path (HDMID_VIDEO, docs/HDMI.md) without the
 * camera: NV21 frames in ION contig memory, handed to hdmid by physical
 * address like the camera HAL does. Run as root (only root and cameraserver
 * may send frames), with hdmid running and a TV connected.
 *
 * The frame checks what a capture card can measure:
 *   - coloured corner squares (white top left, red top right, green bottom
 *     left, blue bottom right) and a 2-pixel white border: orientation and
 *     cropping;
 *   - 100% colour bars and a 16-step grey ramp, BT.601 limited range;
 *   - a white bar that moves 8 pixels per frame and the frame number: motion
 *     and dropped or repeated frames.
 *
 * Usage: gd1_hdmivideo [seconds [width height [fps [nv12]]]]
 *        (default 20 s, 960x720 as the camera's preview, 30 fps, NV21)
 */
#include <cutils/sockets.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "font5x7.h"
#include "hdmid_proto.h"

// Old (3.4) ION userspace ABI, 32-bit
struct ion_allocation_data { size_t len; size_t align; unsigned int flags; void *handle; };
struct ion_fd_data { void *handle; int fd; };
struct ion_custom_data { unsigned int cmd; unsigned long arg; };
struct ion_phys_data { int fd_buffer; unsigned long phys; size_t size; };
#define ION_IOC_MAGIC 'I'
#define ION_IOC_ALLOC _IOWR(ION_IOC_MAGIC, 0, struct ion_allocation_data)
#define ION_IOC_SHARE _IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)
#define ION_IOC_CUSTOM _IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data)
#define ION_HEAP_EXYNOS_CONTIG_MASK (1 << 4)
#define ION_EXYNOS_NONCACHE_MASK (1 << 30)
#define ION_EXYNOS_CUSTOM_PHYS 1

#define NBUF 3

static volatile int stop;
static void on_signal(int sig) { (void)sig; stop = 1; }

static int W, H, nv12;
static uint8_t *Y, *C;

static void ycc(int r, int g, int b, int *y, int *cb, int *cr) {
    double R = r / 255.0, G = g / 255.0, B = b / 255.0;
    *y = (int)(16 + 65.481 * R + 128.553 * G + 24.966 * B + 0.5);
    *cb = (int)(128 - 37.797 * R - 74.203 * G + 112.0 * B + 0.5);
    *cr = (int)(128 + 112.0 * R - 93.786 * G - 18.214 * B + 0.5);
}

// An RGB rectangle, on even coordinates (4:2:0 chroma)
static void fill(int x0, int y0, int w, int h, int r, int g, int b) {
    int yv, cb, cr;
    ycc(r, g, b, &yv, &cb, &cr);
    x0 &= ~1;
    y0 &= ~1;
    for (int y = y0; y < y0 + h && y < H; y++) memset(Y + (size_t)y * W + x0, yv, (size_t)w);
    for (int y = y0 / 2; y < (y0 + h) / 2 && y < H / 2; y++) {
        uint8_t *p = C + (size_t)y * W + x0;
        for (int x = 0; x < w / 2; x++) {
            p[2 * x] = (uint8_t)(nv12 ? cb : cr);
            p[2 * x + 1] = (uint8_t)(nv12 ? cr : cb);
        }
    }
}

// font5x7 draws ARGB pixels: draw into a small scratch image, copy the lit ones to Y
static void text(const char *s, int x0, int y0, int scale) {
    int w = font5x7_width(s, scale), h = 7 * scale;
    uint32_t *px = calloc((size_t)w * h, 4);
    if (!px) return;
    font5x7_draw(px, w, h, s, 0, 0, scale, scale, 0xffffffff);
    for (int y = 0; y < h && y0 + y < H; y++)
        for (int x = 0; x < w && x0 + x < W; x++)
            if (px[y * w + x]) Y[(size_t)(y0 + y) * W + x0 + x] = 235;
    free(px);
}

static void draw(int frame) {
    fill(0, 0, W, H, 0, 0, 0);
    static const int bars[8][3] = {{255, 255, 255}, {255, 255, 0}, {0, 255, 255}, {0, 255, 0},
                                   {255, 0, 255},   {255, 0, 0},   {0, 0, 255},   {0, 0, 0}};
    int margin = W / 16, bw = (W - 2 * margin) / 8 & ~1;
    for (int i = 0; i < 8; i++) fill(margin + i * bw, H * 20 / 100, bw, H * 30 / 100, bars[i][0], bars[i][1], bars[i][2]);
    bw = (W - 2 * margin) / 16 & ~1;
    for (int i = 0; i < 16; i++) fill(margin + i * bw, H * 54 / 100, bw, H * 10 / 100, i * 17, i * 17, i * 17);
    // Border and corners
    fill(0, 0, W, 2, 255, 255, 255);
    fill(0, H - 2, W, 2, 255, 255, 255);
    fill(0, 0, 2, H, 255, 255, 255);
    fill(W - 2, 0, 2, H, 255, 255, 255);
    int sq = H / 16 & ~1;
    fill(4, 4, sq, sq, 255, 255, 255);
    fill(W - 4 - sq, 4, sq, sq, 255, 0, 0);
    fill(4, H - 4 - sq, sq, sq, 0, 255, 0);
    fill(W - 4 - sq, H - 4 - sq, sq, sq, 0, 0, 255);
    // Moving bar and frame number
    int span = W - 2 * margin - 16;
    fill(margin + (frame * 8) % span, H * 70 / 100, 16, H * 12 / 100, 255, 255, 255);
    char s[48];
    snprintf(s, sizeof(s), "%dX%d %s FRAME %d", W, H, nv12 ? "NV12" : "NV21", frame);
    text(s, margin, H * 6 / 100, H / 120 > 1 ? H / 120 : 1);
}

static int alloc(int ion, size_t size, unsigned long *phys, void **map) {
    struct ion_allocation_data a = {size, 4096, ION_HEAP_EXYNOS_CONTIG_MASK | ION_EXYNOS_NONCACHE_MASK, NULL};
    if (ioctl(ion, ION_IOC_ALLOC, &a) < 0) return -1;
    struct ion_fd_data share = {a.handle, -1};
    if (ioctl(ion, ION_IOC_SHARE, &share) < 0) return -1;
    struct ion_phys_data pd = {share.fd, 0, 0};
    struct ion_custom_data c = {ION_EXYNOS_CUSTOM_PHYS, (unsigned long)&pd};
    if (ioctl(ion, ION_IOC_CUSTOM, &c) < 0) return -1;
    *phys = pd.phys;
    *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, share.fd, 0);
    return *map == MAP_FAILED ? -1 : 0;
}

static long long now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

int main(int argc, char **argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 20;
    W = argc > 3 ? atoi(argv[2]) : 960;
    H = argc > 3 ? atoi(argv[3]) : 720;
    int fps = argc > 4 ? atoi(argv[4]) : 30;
    nv12 = argc > 5 && !strcmp(argv[5], "nv12");
    if (W < 16 || H < 16 || (W | H) & 1 || fps <= 0) {
        fprintf(stderr, "usage: %s [seconds [width height [fps [nv12]]]]\n", argv[0]);
        return 1;
    }
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);

    int ion = open("/dev/ion", O_RDWR | O_CLOEXEC);
    if (ion < 0) { perror("/dev/ion"); return 1; }
    size_t size = ((size_t)W * H * 3 / 2 + 4095) & ~(size_t)4095;
    unsigned long phys[NBUF];
    void *map[NBUF];
    for (int i = 0; i < NBUF; i++)
        if (alloc(ion, size, &phys[i], &map[i]) < 0) { perror("ION"); return 1; }

    int s = socket_local_client(HDMID_SOCKET, ANDROID_SOCKET_NAMESPACE_RESERVED, SOCK_STREAM);
    if (s < 0) { perror("connect hdmid"); return 1; }
    // The state hdmid sends on connect
    struct hdmid_msg h;
    char state[2048];
    if (read(s, &h, sizeof(h)) == sizeof(h) && h.magic == HDMID_MAGIC && h.len < sizeof(state)) {
        ssize_t n = read(s, state, h.len);
        if (n > 0) {
            state[n] = '\0';
            printf("%s", state);
        }
    }

    long long period = 1000000LL / fps, next = now_us(), t0 = next, worst = 0;
    int frame = 0;
    for (; !stop && now_us() - t0 < seconds * 1000000LL; frame++) {
        int b = frame % NBUF;
        Y = map[b];
        C = Y + (size_t)W * H;
        long long d0 = now_us();
        draw(frame);
        long long d = now_us() - d0;
        if (d > worst) worst = d;
        struct hdmid_msg m = {HDMID_MAGIC, HDMID_VIDEO, 0,
                              {(uint32_t)W, (uint32_t)H, (uint32_t)phys[b], (uint32_t)(phys[b] + (size_t)W * H),
                               nv12 ? V4L2_PIX_FMT_NV12 : V4L2_PIX_FMT_NV21}};
        if (send(s, &m, sizeof(m), MSG_NOSIGNAL) != sizeof(m)) { perror("send"); break; }
        next += period;
        long long wait = next - now_us();
        if (wait > 0) usleep((useconds_t)wait);
    }
    struct hdmid_msg m = {HDMID_MAGIC, HDMID_VIDEO_STOP, 0, {0}};
    send(s, &m, sizeof(m), MSG_NOSIGNAL);
    printf("%d frames in %.1f s, slowest draw %lld us\n", frame, (now_us() - t0) / 1e6, worst);
    // hdmid stops reading our memory on VIDEO_STOP; closing the socket would too
    close(s);
    return 0;
}

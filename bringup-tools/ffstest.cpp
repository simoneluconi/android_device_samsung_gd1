/*
 * Bring-up test for adb over FunctionFS on the 3.4 kernel.
 *
 * Stop adbd first (setprop ctl.stop adbd). This tool then binds the "adb"
 * FunctionFS instance with plain v1 descriptors (full and high speed, the
 * ADB interface class), prints the ep0 events, and dumps whatever the host
 * sends on the bulk OUT endpoint. With the PC's adb server running, the first
 * thing to arrive should be a CNXN packet.
 *
 * Usage: gd1_ffstest [seconds]
 */
#include <endian.h>
#include <fcntl.h>
#include <linux/usb/ch9.h>
#include <linux/usb/functionfs.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define FFS "/dev/usb-ffs/adb/"

struct func_desc {
    struct usb_interface_descriptor intf;
    struct usb_endpoint_descriptor_no_audio source;
    struct usb_endpoint_descriptor_no_audio sink;
} __attribute__((packed));

struct desc_v1 {
    struct {
        __le32 magic, length, fs_count, hs_count;
    } __attribute__((packed)) header;
    struct func_desc fs, hs;
} __attribute__((packed));

static struct func_desc make_desc(int packet_size) {
    struct func_desc d = {};
    d.intf.bLength = sizeof(d.intf);
    d.intf.bDescriptorType = USB_DT_INTERFACE;
    d.intf.bNumEndpoints = 2;
    d.intf.bInterfaceClass = 0xff;     // ADB_CLASS
    d.intf.bInterfaceSubClass = 0x42;  // ADB_SUBCLASS
    d.intf.bInterfaceProtocol = 0x01;  // ADB_PROTOCOL
    d.intf.iInterface = 1;
    d.source.bLength = sizeof(d.source);
    d.source.bDescriptorType = USB_DT_ENDPOINT;
    d.source.bEndpointAddress = 1 | USB_DIR_OUT;
    d.source.bmAttributes = USB_ENDPOINT_XFER_BULK;
    d.source.wMaxPacketSize = htole16(packet_size);
    d.sink = d.source;
    d.sink.bEndpointAddress = 2 | USB_DIR_IN;
    return d;
}

#define STR_INTERFACE "ADB Interface"
struct strings {
    struct usb_functionfs_strings_head header;
    struct {
        __le16 code;
        char str1[sizeof(STR_INTERFACE)];
    } __attribute__((packed)) lang0;
} __attribute__((packed));

static const char* event_name(int type) {
    switch (type) {
        case FUNCTIONFS_BIND: return "BIND";
        case FUNCTIONFS_UNBIND: return "UNBIND";
        case FUNCTIONFS_ENABLE: return "ENABLE";
        case FUNCTIONFS_DISABLE: return "DISABLE";
        case FUNCTIONFS_SETUP: return "SETUP";
        case FUNCTIONFS_SUSPEND: return "SUSPEND";
        case FUNCTIONFS_RESUME: return "RESUME";
        default: return "?";
    }
}

static void* ep0_events(void* arg) {
    int ep0 = *(int*)arg;
    struct usb_functionfs_event ev;
    while (read(ep0, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
        printf("ep0 event: %s\n", event_name(ev.type));
        fflush(stdout);
    }
    return nullptr;
}

int main(int argc, char** argv) {
    int seconds = argc > 1 ? atoi(argv[1]) : 30;
    setvbuf(stdout, nullptr, _IONBF, 0);

    int ep0 = open(FFS "ep0", O_RDWR);
    if (ep0 < 0) { perror("open ep0"); return 1; }

    struct desc_v1 d = {};
    d.header.magic = htole32(FUNCTIONFS_DESCRIPTORS_MAGIC);
    d.header.length = htole32(sizeof(d));
    d.header.fs_count = htole32(3);
    d.header.hs_count = htole32(3);
    d.fs = make_desc(64);
    d.hs = make_desc(512);
    if (write(ep0, &d, sizeof(d)) < 0) { perror("write v1 descriptors"); return 1; }
    printf("v1 descriptors accepted\n");

    struct strings s = {};
    s.header.magic = htole32(FUNCTIONFS_STRINGS_MAGIC);
    s.header.length = htole32(sizeof(s));
    s.header.str_count = htole32(1);
    s.header.lang_count = htole32(1);
    s.lang0.code = htole16(0x0409);
    strcpy(s.lang0.str1, STR_INTERFACE);
    if (write(ep0, &s, sizeof(s)) < 0) { perror("write strings"); return 1; }
    printf("strings accepted\n");

    pthread_t t;
    pthread_create(&t, nullptr, ep0_events, &ep0);

    int out = open(FFS "ep1", O_RDONLY);
    int in = open(FFS "ep2", O_WRONLY);
    if (out < 0 || in < 0) { perror("open ep1/ep2"); return 1; }
    printf("endpoints open, waiting %d s for host data\n", seconds);

    alarm(seconds);
    unsigned char buf[16384];
    for (;;) {
        ssize_t n = read(out, buf, sizeof(buf));
        if (n < 0) { perror("read ep1"); break; }
        printf("read %zd bytes:", n);
        for (ssize_t i = 0; i < n && i < 48; i++) printf(" %02x", buf[i]);
        printf("  |");
        for (ssize_t i = 0; i < n && i < 48; i++) putchar(buf[i] >= 32 && buf[i] < 127 ? buf[i] : '.');
        printf("|\n");
    }
    return 0;
}

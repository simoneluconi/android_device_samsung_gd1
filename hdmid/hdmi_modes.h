/*
 * The HDMI modes of the s5p-tvout driver (kernel
 * drivers/media/video/samsung/tvout/s5p_tvout_v4l2.c), shared by hdmid and
 * bringup-tools/hdmitest. All of them were checked on a capture card
 * (docs/HDMI.md).
 *
 * The std ids are the driver's private V4L2_STD_* values. vic is the CTA-861
 * video identification code a sink lists in its EDID; the 59.94 Hz variants
 * share the VIC of the 60 Hz mode. dar is the display aspect ratio: 480p and
 * 576p have non-square pixels, so a picture is (dar_w * h) / (dar_h * w)
 * times wider on the display than in the buffer.
 */
#pragma once

#include <linux/videodev2.h>
#include <string.h>

struct hdmi_mode {
    const char *name;
    v4l2_std_id std;
    int w, h;
    int vic;
    int dar_w, dar_h;
    int interlaced;
    int mhz;    /* refresh rate (field rate for interlaced) in mHz */
};

/* Ordered from best to worst: hdmid's "auto" picks the first one the sink lists. */
static const struct hdmi_mode hdmi_modes[] = {
    {"1080p60",     0x0a000000, 1920, 1080, 16, 16, 9, 0, 60000},
    {"1080p59",     0x11000000, 1920, 1080, 16, 16, 9, 0, 59940},
    {"1080p50",     0x0b000000, 1920, 1080, 31, 16, 9, 0, 50000},
    {"1080p30",     0x12000000, 1920, 1080, 34, 16, 9, 0, 30000},
    {"1080i60",     0x0c000000, 1920, 1080,  5, 16, 9, 1, 60000},
    {"1080i59",     0x10000000, 1920, 1080,  5, 16, 9, 1, 59940},
    {"1080i50",     0x0d000000, 1920, 1080, 20, 16, 9, 1, 50000},
    {"720p60",      0x08000000, 1280,  720,  4, 16, 9, 0, 60000},
    {"720p59",      0x0f000000, 1280,  720,  4, 16, 9, 0, 59940},
    {"720p50",      0x09000000, 1280,  720, 19, 16, 9, 0, 50000},
    {"576p50",      0x06000000,  720,  576, 18, 16, 9, 0, 50000},
    {"576p50-4:3",  0x07000000,  720,  576, 17,  4, 3, 0, 50000},
    {"480p60",      0x04000000,  720,  480,  3, 16, 9, 0, 60000},
    {"480p59",      0x0e000000,  720,  480,  3, 16, 9, 0, 59940},
    {"480p60-4:3",  0x05000000,  720,  480,  2,  4, 3, 0, 60000},
};

#define HDMI_MODE_COUNT ((int)(sizeof(hdmi_modes) / sizeof(hdmi_modes[0])))
/* The fallback when the sink's EDID cannot be read: every HD sink takes it */
#define HDMI_MODE_FALLBACK "720p60"

static inline const struct hdmi_mode *hdmi_mode_by_name(const char *name) {
    for (int i = 0; i < HDMI_MODE_COUNT; i++)
        if (!strcmp(hdmi_modes[i].name, name)) return &hdmi_modes[i];
    return NULL;
}

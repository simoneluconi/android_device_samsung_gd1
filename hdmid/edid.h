/*
 * A small EDID parser for hdmid: what the sink is called, whether it is an
 * HDMI sink (HDMI vendor-specific data block) or DVI, and which of our modes
 * it lists. Plain C without Android dependencies, so it can be tested on a PC.
 */
#pragma once

#include <stdint.h>

#define EDID_BLOCK 128
#define EDID_MAX_DTD 16

struct edid_timing {
    int w, h;          /* active pixels; h is the frame height for interlaced modes */
    int interlaced;
    int mhz;           /* refresh (field rate for interlaced) in mHz */
};

struct edid_info {
    int valid;                 /* base block header and checksum are right */
    int hdmi;                  /* has the HDMI VSDB (IEEE OUI 00-0C-03) */
    char name[14];             /* monitor name descriptor, "" if none */
    unsigned char vic[256];    /* vic[n] = 1: the sink lists CEA VIC n */
    int ndtd;
    struct edid_timing dtd[EDID_MAX_DTD];   /* detailed and standard timings */
};

/* Parses len bytes (one or more 128-byte blocks). Returns 0 if the base block is valid. */
int edid_parse(const uint8_t *buf, int len, struct edid_info *out);

/* 1 if the sink lists the mode: by VIC, or by a detailed or standard timing. */
int edid_supports(const struct edid_info *e, int w, int h, int interlaced, int mhz, int vic);

/*
 * EDID parsing for hdmid (see edid.h). References: VESA E-EDID 1.4 and
 * CTA-861 (extension blocks, data blocks, VICs).
 */
#include "edid.h"

#include <string.h>

static void add_timing(struct edid_info *e, int w, int h, int interlaced, int mhz) {
    if (e->ndtd >= EDID_MAX_DTD || w <= 0 || h <= 0) return;
    e->dtd[e->ndtd++] = (struct edid_timing){w, h, interlaced, mhz};
}

/* An 18-byte descriptor: a detailed timing, or a display descriptor (name, ...). */
static void parse_descriptor(const uint8_t *d, struct edid_info *e) {
    unsigned pclk = d[0] | d[1] << 8;   /* 10 kHz units; 0 = display descriptor */
    if (pclk == 0) {
        if (d[3] == 0xfc) {             /* monitor name, ends at 0x0a */
            int n = 0;
            for (int i = 5; i < 18 && d[i] != 0x0a && n < 13; i++) e->name[n++] = (char)d[i];
            while (n > 0 && e->name[n - 1] == ' ') n--;
            e->name[n] = '\0';
        }
        return;
    }
    int hact = d[2] | (d[4] & 0xf0) << 4;
    int hblank = d[3] | (d[4] & 0x0f) << 8;
    int vact = d[5] | (d[7] & 0xf0) << 4;
    int vblank = d[6] | (d[7] & 0x0f) << 8;
    int interlaced = (d[17] & 0x80) != 0;
    long long htotal = hact + hblank, vtotal = vact + vblank;
    if (htotal <= 0 || vtotal <= 0) return;
    int mhz = (int)((long long)pclk * 10000LL * 1000LL / (htotal * vtotal));
    add_timing(e, hact, interlaced ? vact * 2 : vact, interlaced, mhz);
}

/* Standard timing: two bytes, 0x0101 = unused. Only the 16:9 sizes matter to us. */
static void parse_standard(const uint8_t *s, struct edid_info *e) {
    if (s[0] == 0x01 && s[1] == 0x01) return;
    if (s[0] == 0x00) return;
    int w = (s[0] + 31) * 8;
    int h;
    switch (s[1] >> 6) {
    case 0: h = w * 10 / 16; break;
    case 1: h = w * 3 / 4; break;
    case 2: h = w * 4 / 5; break;
    default: h = w * 9 / 16; break;
    }
    add_timing(e, w, h, 0, ((s[1] & 0x3f) + 60) * 1000);
}

static void parse_cea(const uint8_t *b, struct edid_info *e) {
    int dtd_off = b[2];
    if (dtd_off < 4 || dtd_off > EDID_BLOCK - 1) dtd_off = EDID_BLOCK - 1;
    /* Revision 3+: data block collection from byte 4 to the first DTD */
    if (b[1] >= 3) {
        for (int i = 4; i < dtd_off;) {
            int tag = b[i] >> 5, len = b[i] & 0x1f;
            if (i + 1 + len > dtd_off) break;
            const uint8_t *p = b + i + 1;
            if (tag == 2) {             /* video data block: short video descriptors */
                for (int k = 0; k < len; k++) {
                    int v = p[k];
                    /* 129..192: native flag on VICs 1..64 (CTA-861-F) */
                    if (v >= 129 && v <= 192) v &= 0x7f;
                    e->vic[v] = 1;
                }
            } else if (tag == 3 && len >= 3 && p[0] == 0x03 && p[1] == 0x0c && p[2] == 0x00) {
                e->hdmi = 1;
            }
            i += 1 + len;
        }
    }
    for (int i = dtd_off; i + 18 <= EDID_BLOCK - 1; i += 18) {
        if (b[i] == 0 && b[i + 1] == 0) break;
        parse_descriptor(b + i, e);
    }
}

static int checksum_ok(const uint8_t *b) {
    uint8_t s = 0;
    for (int i = 0; i < EDID_BLOCK; i++) s += b[i];
    return s == 0;
}

int edid_parse(const uint8_t *buf, int len, struct edid_info *out) {
    static const uint8_t header[8] = {0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    memset(out, 0, sizeof(*out));
    if (len < EDID_BLOCK || memcmp(buf, header, 8) || !checksum_ok(buf)) return -1;
    out->valid = 1;
    for (int i = 0; i < 8; i++) parse_standard(buf + 38 + 2 * i, out);
    for (int i = 0; i < 4; i++) parse_descriptor(buf + 54 + 18 * i, out);
    int ext = buf[126];
    for (int n = 1; n <= ext && (n + 1) * EDID_BLOCK <= len; n++) {
        const uint8_t *b = buf + n * EDID_BLOCK;
        if (!checksum_ok(b)) continue;
        if (b[0] == 0x02) parse_cea(b, out);
    }
    return 0;
}

int edid_supports(const struct edid_info *e, int w, int h, int interlaced, int mhz, int vic) {
    if (vic > 0 && vic < 256 && e->vic[vic]) return 1;
    for (int i = 0; i < e->ndtd; i++) {
        const struct edid_timing *t = &e->dtd[i];
        int d = t->mhz - mhz;
        if (d < 0) d = -d;
        /* 600 mHz: 60 and 59.94 Hz are the same timing to a sink */
        if (t->w == w && t->h == h && t->interlaced == interlaced && d <= 600) return 1;
    }
    return 0;
}

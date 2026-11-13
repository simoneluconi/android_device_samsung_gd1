/*
 * 5x7 bitmap font for the HDMI screens (hdmid and bringup-tools/hdmitest).
 * One byte per row, bit 4 = leftmost pixel. Upper case, digits and a little
 * punctuation; lower case draws as upper case, anything else as a space.
 */
#pragma once

#include <stdint.h>
#include <string.h>

static const struct { char c; unsigned char rows[7]; } font5x7[] = {
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 15}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {14, 4, 4, 4, 4, 4, 14}},      {'J', {7, 2, 2, 2, 2, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {31, 2, 4, 2, 1, 17, 14}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 30, 1, 1, 17, 14}},
    {'6', {6, 8, 16, 30, 17, 17, 14}},   {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 2, 12}},
    {'.', {0, 0, 0, 0, 0, 12, 12}},      {',', {0, 0, 0, 0, 12, 4, 8}},
    {':', {0, 12, 12, 0, 12, 12, 0}},    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'/', {0, 1, 2, 4, 8, 16, 0}},       {'\'', {4, 4, 8, 0, 0, 0, 0}},
    {' ', {0, 0, 0, 0, 0, 0, 0}},
};

static inline const unsigned char *font5x7_glyph(char c) {
    const size_t n = sizeof(font5x7) / sizeof(font5x7[0]);
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (size_t i = 0; i < n; i++)
        if (font5x7[i].c == c) return font5x7[i].rows;
    return font5x7[n - 1].rows;
}

/* Width in pixels of a string at horizontal scale sx (6 columns per character, the last blank). */
static inline int font5x7_width(const char *s, int sx) {
    int n = (int)strlen(s);
    return n > 0 ? (n * 6 - 1) * sx : 0;
}

/* Draws s into an ARGB8888 buffer of w x h pixels at (x, y), scaled by sx and sy, clipped. */
static inline void font5x7_draw(uint32_t *px, int w, int h, const char *s, int x, int y, int sx,
                                int sy, uint32_t color) {
    for (; *s; s++, x += 6 * sx) {
        const unsigned char *g = font5x7_glyph(*s);
        for (int r = 0; r < 7; r++)
            for (int c = 0; c < 5; c++) {
                if (!(g[r] & (16 >> c))) continue;
                for (int dy = 0; dy < sy; dy++) {
                    int yy = y + r * sy + dy;
                    if (yy < 0 || yy >= h) continue;
                    for (int dx = 0; dx < sx; dx++) {
                        int xx = x + c * sx + dx;
                        if (xx >= 0 && xx < w) px[yy * w + xx] = color;
                    }
                }
            }
    }
}

/*
 * The protocol of /dev/socket/hdmid (docs/HDMI.md): a stream of messages, each
 * a 32-byte header in native byte order, then len bytes of payload.
 *
 * Shared by hdmid, the camera HAL and the bring-up tools. GC Gallery speaks it
 * from Kotlin (TvOut.kt).
 */
#ifndef HDMID_PROTO_H
#define HDMID_PROTO_H

#include <stdint.h>

#define HDMID_SOCKET "hdmid"            /* in the reserved namespace, /dev/socket */
#define HDMID_MAGIC 0x44484347u         /* "GCHD" */

struct hdmid_msg {
    uint32_t magic, type, len, a[5];
};

/* To hdmid */
#define HDMID_HELLO 1       /* answered with STATE */
#define HDMID_SHOW 2        /* a0 w, a1 h, a2 flags (HDMID_SHOW_HALF); w*h*4 bytes ARGB_8888 */
#define HDMID_CLEAR 3       /* back to the notice */
#define HDMID_SET_MODE 4    /* payload: a mode name of hdmi_modes.h, or "auto" */
/*
 * A camera frame, no payload: a0 w, a1 h, a2 the luma plane's physical
 * address, a3 the chroma plane's, a4 the V4L2 fourcc (NV21 or NV12, linear).
 * The video layer reads the planes until the next VIDEO or VIDEO_STOP, so the
 * sender must not rewrite them before handing over the next frame. Physical
 * memory: only root and the camera HAL (cameraserver) may send it.
 */
#define HDMID_VIDEO 5
#define HDMID_VIDEO_STOP 6  /* the frames are over: back to the gallery or the notice */

/* From hdmid */
#define HDMID_STATE 16      /* key=value lines: connected, mode, width, height, ... */

#define HDMID_SHOW_HALF 1

#endif

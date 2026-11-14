/*
 * The live view on a TV: each preview frame goes to hdmid (device/samsung/gd1
 * hdmid/, docs/HDMI.md) by physical address. The frames are FIMC1's output
 * buffers, physically contiguous NV21 in FIMC1's reserved memory, which the
 * TV's video processor reads and scales itself: no copy, one 32-byte message
 * per frame. Frames are sent only while hdmid says a TV is on.
 *
 * FIMC1 rewrites a buffer only after the other five (preview_output), so the
 * one on the TV is never refilled while it is shown. Its reserved memory stays
 * put between previews, so during a still capture the TV keeps the last frame.
 *
 * Called from the capture thread (frames) and from HwBinder threads (stop,
 * close): one mutex. Nothing here may block: the socket is non-blocking, and a
 * frame hdmid has no room for is dropped.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define LOG_TAG "exynos_hdmi"
#include <utils/Log.h>

#include "exynos_camera.h"
#include "hdmid_proto.h"

#define HDMID_PATH "/dev/socket/" HDMID_SOCKET
// Without hdmid (not started yet, or restarting) try again this often
#define RETRY_MS 2000

static pthread_mutex_t hdmi_mutex = PTHREAD_MUTEX_INITIALIZER;
static int hdmi_fd = -1;
static int hdmi_tv_on;          // hdmid's last state said connected=1
static int hdmi_sending;        // frames went out since the last stop
static long long hdmi_retry_ms; // no connect attempt before this
static char hdmi_in[4096];      // hdmid's messages, as they arrive
static size_t hdmi_in_len;

static long long hdmi_now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

static void hdmi_disconnect(void)
{
	if (hdmi_fd >= 0)
		close(hdmi_fd);
	hdmi_fd = -1;
	hdmi_tv_on = 0;
	hdmi_sending = 0;
	hdmi_in_len = 0;
	hdmi_retry_ms = hdmi_now_ms() + RETRY_MS;
}

static void hdmi_connect(void)
{
	struct sockaddr_un addr;
	int fd;

	if (hdmi_now_ms() < hdmi_retry_ms)
		return;

	fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return;

	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, HDMID_PATH, sizeof(addr.sun_path) - 1);

	// A local stream socket connects at once or fails (ENOENT, ECONNREFUSED)
	if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
		close(fd);
		hdmi_retry_ms = hdmi_now_ms() + RETRY_MS;
		return;
	}

	hdmi_fd = fd;
	hdmi_in_len = 0;
	ALOGD("%s: connected to hdmid", __func__);
}

// hdmid's state: only "connected=" matters here
static void hdmi_state(const char *text, size_t len)
{
	const char *p = text, *end = text + len;
	int on = 0;

	while (p < end) {
		const char *nl = memchr(p, '\n', end - p);
		size_t n = nl ? (size_t) (nl - p) : (size_t) (end - p);

		if (n >= 11 && !strncmp(p, "connected=1", 11))
			on = 1;
		p += n + 1;
	}

	if (on != hdmi_tv_on)
		ALOGD("%s: TV %s", __func__, on ? "on" : "off");
	hdmi_tv_on = on;
}

// Whatever hdmid sent meanwhile; -1 when the connection is gone
static int hdmi_read(void)
{
	for (;;) {
		struct hdmid_msg h;
		ssize_t n;

		if (hdmi_in_len == sizeof(hdmi_in)) {
			// A message bigger than the buffer: not from this protocol
			return -1;
		}

		n = recv(hdmi_fd, hdmi_in + hdmi_in_len, sizeof(hdmi_in) - hdmi_in_len, MSG_DONTWAIT);
		if (n == 0)
			return -1;
		if (n < 0)
			return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
		hdmi_in_len += n;

		while (hdmi_in_len >= sizeof(h)) {
			memcpy(&h, hdmi_in, sizeof(h));
			if (h.magic != HDMID_MAGIC || h.len > sizeof(hdmi_in) - sizeof(h))
				return -1;
			if (hdmi_in_len < sizeof(h) + h.len)
				break;
			if (h.type == HDMID_STATE)
				hdmi_state(hdmi_in + sizeof(h), h.len);
			hdmi_in_len -= sizeof(h) + h.len;
			memmove(hdmi_in, hdmi_in + sizeof(h) + h.len, hdmi_in_len);
		}
	}
}

static int hdmi_send(uint32_t type, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
	struct hdmid_msg m = { HDMID_MAGIC, type, 0, { a0, a1, a2, a3, a4 } };
	ssize_t n;

	n = send(hdmi_fd, &m, sizeof(m), MSG_DONTWAIT | MSG_NOSIGNAL);
	if (n == (ssize_t) sizeof(m))
		return 0;
	// hdmid is busy: this frame is skipped, the next one replaces it
	if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
		return 0;
	// Gone, or half a message out: the stream is lost either way
	return -1;
}

void exynos_hdmi_frame(int width, int height, int format, int address)
{
	int y = 0, cb = 0, cr = 0;

	if (format != V4L2_PIX_FMT_NV21 && format != V4L2_PIX_FMT_NV12)
		return;

	pthread_mutex_lock(&hdmi_mutex);

	if (hdmi_fd < 0)
		hdmi_connect();
	if (hdmi_fd < 0)
		goto complete;

	if (hdmi_read() < 0) {
		ALOGD("%s: hdmid gone", __func__);
		hdmi_disconnect();
		goto complete;
	}

	if (!hdmi_tv_on)
		goto complete;

	exynos_camera_yuv_planes(width, height, format, address, &y, &cb, &cr);
	if (hdmi_send(HDMID_VIDEO, width, height, y, cb, format) < 0) {
		ALOGD("%s: hdmid gone", __func__);
		hdmi_disconnect();
		goto complete;
	}
	hdmi_sending = 1;

complete:
	pthread_mutex_unlock(&hdmi_mutex);
}

// The preview is over (not a capture's pause): the TV leaves the last frame
void exynos_hdmi_stop(void)
{
	pthread_mutex_lock(&hdmi_mutex);

	if (hdmi_fd >= 0 && hdmi_sending) {
		if (hdmi_send(HDMID_VIDEO_STOP, 0, 0, 0, 0, 0) < 0)
			hdmi_disconnect();
		hdmi_sending = 0;
	}

	pthread_mutex_unlock(&hdmi_mutex);
}

// The camera is closed: hdmid stops reading FIMC memory when the socket goes
void exynos_hdmi_close(void)
{
	pthread_mutex_lock(&hdmi_mutex);

	if (hdmi_fd >= 0)
		hdmi_disconnect();
	// The next camera open connects at once
	hdmi_retry_ms = 0;

	pthread_mutex_unlock(&hdmi_mutex);
}

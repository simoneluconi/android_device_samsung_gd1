/*
 * SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * NFC placeholder for the GC200's tag: when a phone's NFC field reaches the
 * tag, tell the user that NFC is not supported yet.
 *
 * The kernel (arch/arm/mach-exynos/sf2-nfc-field.c) counts approaches in
 * /sys/kernel/nfc_field/approaches and sysfs_notify()s it, so this sleeps
 * in poll() and costs no wakeups while nothing happens. On a new approach
 * it sends an explicit broadcast to GalaxyCameraNfcNotice, whose receiver
 * shows the toast. At most one every MIN_INTERVAL_S.
 */

#define LOG_TAG "nfc-field-notice"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <log/log.h>

#define APPROACHES "/sys/kernel/nfc_field/approaches"
#define MIN_INTERVAL_S 5

extern char **environ;

static long read_count(int fd)
{
    char buf[16];
    ssize_t n;

    if (lseek(fd, 0, SEEK_SET) < 0)
        return -1;
    n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0)
        return -1;
    buf[n] = '\0';
    return strtol(buf, NULL, 10);
}

/* Runs /system/bin/cmd with argv and waits for it */
static void run_cmd(char *argv[])
{
    posix_spawn_file_actions_t actions;
    pid_t pid;
    int rc, status;

    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    rc = posix_spawn(&pid, "/system/bin/cmd", &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        ALOGE("cannot run cmd: %s", strerror(rc));
        return;
    }
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        ALOGW("cmd %s %s failed (status %d)", argv[1], argv[2], status);
}

/*
 * Android 13+ drops the toasts of an app without notification permission,
 * and a preinstalled app gets default grants only on a fresh /data or a
 * fingerprint change (ours stays Samsung's), so grant it at every start.
 */
static void grant_notifications(void)
{
    char *argv[] = {
        "cmd", "package", "grant", "--user", "0",
        "com.simoneluconi.gcnfc", "android.permission.POST_NOTIFICATIONS",
        NULL,
    };

    run_cmd(argv);
}

static void notify(void)
{
    char *argv[] = {
        "cmd", "activity", "broadcast", "--user", "0",
        "-a", "com.simoneluconi.gcnfc.FIELD",
        "-n", "com.simoneluconi.gcnfc/.FieldReceiver",
        "-f", "0x10000000", /* FLAG_RECEIVER_FOREGROUND */
        NULL,
    };

    run_cmd(argv);
}

int main(void)
{
    struct timespec now, last = { 0, 0 };
    long count, seen;
    int fd;

    fd = open(APPROACHES, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        ALOGI("no %s (not a GC200 kernel?): nothing to do", APPROACHES);
        return 0;
    }

    grant_notifications();

    /* sysfs poll() needs a read first; later reads re-arm it */
    seen = read_count(fd);
    ALOGI("watching %s (%ld so far)", APPROACHES, seen);

    for (;;) {
        struct pollfd pfd = { .fd = fd, .events = POLLPRI | POLLERR };

        if (poll(&pfd, 1, -1) < 0) {
            if (errno == EINTR)
                continue;
            ALOGE("poll: %s", strerror(errno));
            return 1;
        }

        count = read_count(fd);
        if (count < 0 || count == seen)
            continue;
        seen = count;

        clock_gettime(CLOCK_BOOTTIME, &now);
        if (last.tv_sec != 0 && now.tv_sec - last.tv_sec < MIN_INTERVAL_S)
            continue;
        last = now;

        ALOGI("phone field on the tag (%ld): NFC notice", count);
        notify();
    }
}

/*
 * SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
 * SPDX-License-Identifier: Apache-2.0
 *
 * gd1_sensortest <type> [cycles] [on_ms] [period_us]
 *
 * Enables a sensor through the NDK (so through sensorservice and the HAL),
 * counts its events for on_ms, disables it, and repeats for cycles. Prints
 * the count and the last sample of each cycle. type 1 = accelerometer,
 * 2 = magnetic field, 4 = gyroscope. A short on_ms with many cycles stresses
 * the HAL's activate/deactivate paths.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <android/looper.h>
#include <android/sensor.h>

static long long now_ms(void)
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

int main(int argc, char *argv[])
{
    int type = argc > 1 ? atoi(argv[1]) : ASENSOR_TYPE_MAGNETIC_FIELD;
    int cycles = argc > 2 ? atoi(argv[2]) : 1;
    int on_ms = argc > 3 ? atoi(argv[3]) : 2000;
    int period_us = argc > 4 ? atoi(argv[4]) : 20000;
    ASensorManager *manager;
    const ASensor *sensor;
    ASensorEventQueue *queue;
    ALooper *looper;
    int c, empty = 0;

    manager = ASensorManager_getInstanceForPackage("gd1.sensortest");
    sensor = ASensorManager_getDefaultSensor(manager, type);
    if (sensor == NULL) {
        fprintf(stderr, "no sensor of type %d\n", type);
        return 1;
    }
    printf("%s, %d cycles of %d ms at %d us\n", ASensor_getName(sensor), cycles, on_ms,
           period_us);

    looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    queue = ASensorManager_createEventQueue(manager, looper, 1, NULL, NULL);

    for (c = 0; c < cycles; c++) {
        ASensorEvent event, last = { 0 };
        long long end;
        int n = 0;

        if (ASensorEventQueue_registerSensor(queue, sensor, period_us, 0) < 0) {
            fprintf(stderr, "cycle %d: register failed\n", c);
            return 1;
        }
        end = now_ms() + on_ms;
        while (now_ms() < end) {
            ALooper_pollOnce((int) (end - now_ms()), NULL, NULL, NULL);
            while (ASensorEventQueue_getEvents(queue, &event, 1) > 0) {
                last = event;
                n++;
            }
        }
        ASensorEventQueue_disableSensor(queue, sensor);
        if (n == 0)
            empty++;
        printf("cycle %d: %d events, last %.2f %.2f %.2f status %d\n", c, n,
               last.vector.x, last.vector.y, last.vector.z, last.vector.status);
        fflush(stdout);
    }

    ASensorManager_destroyEventQueue(manager, queue);
    printf("done: %d of %d cycles without events\n", empty, cycles);
    return empty ? 2 : 0;
}

/*
 * SPDX-FileCopyrightText: 2020 The LineageOS Project
 * SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Symbols that the GC100's gpsd_4752 (Broadcom BCM4752 daemon, Android 4.1)
 * links against but Android 14 no longer has. Preloaded into the daemon.
 */

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>

#include <openssl/ssl.h>
#include <utils/String8.h>

extern "C" {

/* BoringSSL has no SSLv3. The daemon only uses TLS when tlsEnable="true". */
const SSL_METHOD *SSLv3_method(void) {
    const SSL_METHOD *kMethod = TLSv1_method();
    char ssl_version[] = {0, 0x03}; // 0x0300

    memcpy((void*)kMethod, ssl_version, 1);

    return kMethod;
}

const SSL_METHOD *SSLv3_client_method(void) {
    return SSLv3_method();
}

}

/*
 * The Android 4.1 android::SensorManager API. The daemon reads the
 * accelerometer and gyroscope only with SensorsEnable="true", which our
 * gps.xml turns off, so these stubs report no sensors and never touch the
 * objects the daemon passes in.
 */

/* Singleton<SensorManager>::sLock (a 4.1 android::Mutex, a bare
 * pthread_mutex_t) and ::sInstance. The daemon is not PIE and takes copy
 * relocations of both, so the sizes must match the 4.1 ones. */
pthread_mutex_t sensor_manager_lock
        asm("_ZN7android9SingletonINS_13SensorManagerEE5sLockE") = PTHREAD_MUTEX_INITIALIZER;
void *sensor_manager_instance
        asm("_ZN7android9SingletonINS_13SensorManagerEE9sInstanceE") = nullptr;

static const void *const kNoSensors[1] = {nullptr};
static const android::String8 kEmptyName;

/* ARM EABI constructors return this */
extern "C" void *sensor_manager_ctor(void *self) asm("_ZN7android13SensorManagerC1Ev");
void *sensor_manager_ctor(void *self) {
    return self;
}

extern "C" ssize_t sensor_manager_get_sensor_list(const void *, const void *const **list)
        asm("_ZNK7android13SensorManager13getSensorListEPPKPKNS_6SensorE");
ssize_t sensor_manager_get_sensor_list(const void *, const void *const **list) {
    *list = kNoSensors;
    return 0;
}

extern "C" const void *sensor_manager_get_default_sensor(void *, int)
        asm("_ZN7android13SensorManager16getDefaultSensorEi");
const void *sensor_manager_get_default_sensor(void *, int) {
    return nullptr;
}

/* Returns sp<SensorEventQueue> by value: the caller passes the result slot
 * before this. A null sp is a single null pointer. */
extern "C" void sensor_manager_create_event_queue(void **result, void *)
        asm("_ZN7android13SensorManager16createEventQueueEv");
void sensor_manager_create_event_queue(void **result, void *) {
    *result = nullptr;
}

extern "C" int sensor_event_queue_get_fd(const void *)
        asm("_ZNK7android16SensorEventQueue5getFdEv");
int sensor_event_queue_get_fd(const void *) {
    return -1;
}

extern "C" ssize_t sensor_event_queue_read(void *, void *, size_t)
        asm("_ZN7android16SensorEventQueue4readEP12ASensorEventj");
ssize_t sensor_event_queue_read(void *, void *, size_t) {
    return 0;
}

extern "C" int sensor_event_queue_enable_sensor(const void *, const void *)
        asm("_ZNK7android16SensorEventQueue12enableSensorEPKNS_6SensorE");
int sensor_event_queue_enable_sensor(const void *, const void *) {
    return -ENODEV;
}

extern "C" int sensor_event_queue_disable_sensor(const void *, const void *)
        asm("_ZNK7android16SensorEventQueue13disableSensorEPKNS_6SensorE");
int sensor_event_queue_disable_sensor(const void *, const void *) {
    return -ENODEV;
}

extern "C" int sensor_event_queue_set_event_rate(const void *, const void *, int64_t)
        asm("_ZNK7android16SensorEventQueue12setEventRateEPKNS_6SensorEx");
int sensor_event_queue_set_event_rate(const void *, const void *, int64_t) {
    return -ENODEV;
}

/* There are no Sensor objects, so these are never called; they only have to
 * resolve. */
extern "C" const android::String8 *sensor_get_name(const void *)
        asm("_ZNK7android6Sensor7getNameEv");
const android::String8 *sensor_get_name(const void *) {
    return &kEmptyName;
}

extern "C" const android::String8 *sensor_get_vendor(const void *)
        asm("_ZNK7android6Sensor9getVendorEv");
const android::String8 *sensor_get_vendor(const void *) {
    return &kEmptyName;
}

extern "C" int sensor_get_handle(const void *) asm("_ZNK7android6Sensor9getHandleEv");
int sensor_get_handle(const void *) {
    return 0;
}

extern "C" int sensor_get_type(const void *) asm("_ZNK7android6Sensor7getTypeEv");
int sensor_get_type(const void *) {
    return 0;
}

extern "C" float sensor_get_max_value(const void *) asm("_ZNK7android6Sensor11getMaxValueEv");
float sensor_get_max_value(const void *) {
    return 0.0f;
}

extern "C" float sensor_get_resolution(const void *) asm("_ZNK7android6Sensor13getResolutionEv");
float sensor_get_resolution(const void *) {
    return 0.0f;
}

extern "C" float sensor_get_power_usage(const void *) asm("_ZNK7android6Sensor13getPowerUsageEv");
float sensor_get_power_usage(const void *) {
    return 0.0f;
}

extern "C" int32_t sensor_get_min_delay(const void *) asm("_ZNK7android6Sensor11getMinDelayEv");
int32_t sensor_get_min_delay(const void *) {
    return 0;
}

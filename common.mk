#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#
# What the Galaxy Cameras have in common: the M9MO camera, the panel and
# touchscreen, sensors, GPS, audio and the camera-first app set. gd1.mk adds
# what only the GC100 has (the 3G modem, its partitions and its 768MB RAM).
# A GC200 (EK-GC200) tree would inherit this file too; see docs/GC200.md.
#

COMMON_PATH := device/samsung/gd1

# Never write the PARAM partition (bootloader environment, see the
# anti-brick rules): skip smdk4412-common's param.sh, which would flash
# /vendor/param.lfs over it on first boot.
SMDK4412_NO_PARAM_SCRIPT := true

# Off-mode charging must not suspend: right after a resume there, processes
# crash (ueventd segfaults, BoringSSL self-tests fail), and after four
# ueventd crashes init reboots to the bootloader, i.e. download mode.
SMDK4412_CHARGER_NO_SUSPEND := true

# Overlay
DEVICE_PACKAGE_OVERLAYS += $(COMMON_PATH)/overlay

# Screen density
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi

# Input: rotate the touchscreen with the landscape display
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/idc/sec_touchscreen.idc:$(TARGET_COPY_OUT_VENDOR)/usr/idc/sec_touchscreen.idc

# Audio (WM1811 codec, same as the i9300; mixer paths still to be checked on the GC100)
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/tiny_hw.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound/gd1

# Health (legacy HIDL 2.0 carried in this tree)
PRODUCT_PACKAGES += \
    android.hardware.health@2.0-service

# GPS (BCM4752): the stock daemon and HAL from the vendor blobs, the HAL
# wrapper and the daemon's symbol shim
PRODUCT_PACKAGES += \
    gps.smdk4x12 \
    libgpsd_shim

PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/configs/gps.xml:$(TARGET_COPY_OUT_VENDOR)/etc/gps.xml \
    $(COMMON_PATH)/configs/gps_debug.conf:$(TARGET_COPY_OUT_VENDOR)/etc/gps_debug.conf

# Sensors (LSM330 accelerometer and gyroscope, AK8963C compass)
PRODUCT_PACKAGES += \
    sensors.smdk4x12

# Proprietary blobs dependency on libstlport
PRODUCT_PACKAGES += \
    libstlport

# Camera
PRODUCT_PACKAGES += \
    camera.smdk4x12

# Camera-first app set (see RemovePackages/Android.mk). GcCamera replaces
# OpenCamera and GcGallery replaces Glimpse; the home layout
# (overlay/packages/apps/Trebuchet) shows both.
PRODUCT_PACKAGES += \
    GcCamera \
    GcGallery \
    RemovePackages
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/system_ext/gd1-setup.sh:$(TARGET_COPY_OUT_SYSTEM_EXT)/bin/gd1-setup.sh \
    $(COMMON_PATH)/system_ext/gd1-setup.rc:$(TARGET_COPY_OUT_SYSTEM_EXT)/etc/init/gd1-setup.rc

# Defaults: English UI (the build default), Italian time zone
PRODUCT_SYSTEM_PROPERTIES += \
    persist.sys.timezone=Europe/Rome

# Storage: FUSE with bind mounts, as on GKI devices, not sdcardfs. The 3.4
# kernel's sdcardfs leaks a dentry reference on the microSD: unmounting it
# at reboot hit the BUG in shrink_dcache_for_umount_subtree() and panicked
# (2026-11-02, after the camera app had saved photos there).
PRODUCT_PRODUCT_PROPERTIES += \
    external_storage.sdcardfs.enabled=false

# Hardware features: a camera, not a phone
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml

# Vendor properties
-include $(COMMON_PATH)/vendor_prop.mk

# Bring-up aids, userdebug/eng only. Keep logcat on /data so that a
# bootloop can be read back from the mini recovery. A developer's own adb
# key (bringup/adb_keys, never committed) is trusted from the first boot
# only when the build asks for it with GALAXYCAMERA_DEV_ADB_KEYS=true in the
# environment: a build for others must not carry it.
ifneq ($(TARGET_BUILD_VARIANT),user)
ifeq ($(GALAXYCAMERA_DEV_ADB_KEYS),true)
ifneq ($(wildcard $(COMMON_PATH)/bringup/adb_keys),)
PRODUCT_ADB_KEYS := $(COMMON_PATH)/bringup/adb_keys
endif
endif
PRODUCT_SYSTEM_PROPERTIES += \
    persist.logd.logpersistd=logcatd
PRODUCT_COPY_FILES += \
    $(COMMON_PATH)/bringup-tools/gd1-bringup.sh:$(TARGET_COPY_OUT_VENDOR)/bin/gd1-bringup.sh
endif

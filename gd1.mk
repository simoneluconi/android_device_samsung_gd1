#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := device/samsung/gd1

# Never write the PARAM partition (bootloader environment, see the
# anti-brick rules): skip smdk4412-common's param.sh, which would flash
# /vendor/param.lfs over it on first boot.
SMDK4412_NO_PARAM_SCRIPT := true

# Overlay
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay

# Screen density
PRODUCT_AAPT_CONFIG := normal
PRODUCT_AAPT_PREF_CONFIG := xhdpi

# Init files
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/fstab.smdk4x12:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.smdk4x12 \
    $(LOCAL_PATH)/rootdir/fstab.smdk4x12:$(TARGET_COPY_OUT_RAMDISK)/fstab.smdk4x12 \
    $(LOCAL_PATH)/rootdir/init.target.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.target.rc \
    $(LOCAL_PATH)/rootdir/init.target.usb.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.target.usb.rc

# HIDL
DEVICE_MANIFEST_FILE := $(LOCAL_PATH)/manifest.xml

# Audio (WM1811 codec, same as the i9300; mixer paths still to be checked on the GC100)
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/configs/tiny_hw.xml:$(TARGET_COPY_OUT_VENDOR)/etc/sound/gd1

# Health (legacy HIDL 2.0 carried in this tree)
PRODUCT_PACKAGES += \
    android.hardware.health@2.0-service

# RIL (XMM6262, 3G data only)
PRODUCT_PACKAGES += \
    libsecril-client \
    libsecril-client-sap \
    libsecril-shim

PRODUCT_PROPERTY_OVERRIDES += \
    ro.telephony.ril_class=SamsungExynos4RIL

# Proprietary blobs dependency on libstlport
PRODUCT_PACKAGES += \
    libstlport

# Camera
PRODUCT_PACKAGES += \
    camera.smdk4x12

# Camera-first app set (see RemovePackages/Android.mk)
PRODUCT_PACKAGES += \
    RemovePackages
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/gd1-disable-packages.sh:$(TARGET_COPY_OUT_VENDOR)/bin/gd1-disable-packages.sh

# Hardware features: a camera with 3G data, not a phone
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/handheld_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/handheld_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml

# Dalvik heap (768MB RAM)
$(call inherit-product, $(LOCAL_PATH)/phone-hdpi-512-dalvik-heap.mk)

# Vendor blobs
$(call inherit-product-if-exists, vendor/samsung/gd1/gd1-vendor.mk)

# Vendor properties
-include $(LOCAL_PATH)/vendor_prop.mk

# Bring-up aids, userdebug/eng only. Trust a local adb key from the first
# boot (bringup/adb_keys, never committed), and keep logcat on /data so that
# a bootloop can be read back from the mini recovery.
ifneq ($(TARGET_BUILD_VARIANT),user)
ifneq ($(wildcard $(LOCAL_PATH)/bringup/adb_keys),)
PRODUCT_ADB_KEYS := $(LOCAL_PATH)/bringup/adb_keys
endif
PRODUCT_SYSTEM_PROPERTIES += \
    persist.logd.logpersistd=logcatd
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/bringup-tools/gd1-bringup.sh:$(TARGET_COPY_OUT_VENDOR)/bin/gd1-bringup.sh
endif

#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#
# The GC100 (EK-GC100) only: the 3G modem, its partitions and init files,
# the gpio key map and the 768MB RAM settings. The rest is in common.mk.
#

LOCAL_PATH := device/samsung/gd1

$(call inherit-product, $(LOCAL_PATH)/common.mk)

# Init files
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/rootdir/fstab.smdk4x12:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.smdk4x12 \
    $(LOCAL_PATH)/rootdir/fstab.smdk4x12:$(TARGET_COPY_OUT_RAMDISK)/fstab.smdk4x12 \
    $(LOCAL_PATH)/rootdir/init.target.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.target.rc \
    $(LOCAL_PATH)/rootdir/init.target.usb.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.target.usb.rc

# Input: the shutter and zoom lever. The camera and gallery apps tell the
# lever's fast stage by scan codes 545/546; keep them on another model.
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/keylayout/gpio-keys.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/gpio-keys.kl

# HIDL (includes the radio HAL)
DEVICE_MANIFEST_FILE := $(LOCAL_PATH)/manifest.xml

# RIL (XMM6262, 3G data only)
PRODUCT_PACKAGES += \
    libsecril-client \
    libsecril-client-sap \
    libsecril-shim

PRODUCT_PROPERTY_OVERRIDES += \
    ro.telephony.ril_class=SamsungExynos4RIL \
    vendor.rild.libpath=/vendor/lib/libsecril-shim.so \
    ro.ril.telephony.mqanelements=5 \
    ro.ril.telephony.nstrings=4

# Hardware features: 3G data
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.telephony.gsm.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.telephony.gsm.xml

# SMS (user decision 2026-11-04): the GC100 has the modem, so it keeps the
# messaging app. Still no dialer: no voice calls.
PRODUCT_PACKAGES += \
    messaging

# RAM (768MB): dalvik heap, low RAM mode and the framework's trim limits
$(call inherit-product, $(LOCAL_PATH)/phone-hdpi-512-dalvik-heap.mk)

PRODUCT_PROPERTY_OVERRIDES += \
    ro.config.low_ram=true \
    ro.sys.fw.trim_enable_memory=671088640 \
    ro.sys.fw.use_trim_settings=true \
    ro.sys.fw.empty_app_percent=25 \
    ro.sys.fw.trim_empty_percent=30 \
    ro.sys.fw.trim_cache_percent=30 \
    ro.sys.fw.bg_apps_limit=12 \
    ro.sys.fw.bservice_limit=6 \
    ro.sys.fw.bservice_age=5000 \
    ro.sys.fw.bservice_enable=true

# Vendor blobs
$(call inherit-product-if-exists, vendor/samsung/gd1/gd1-vendor.mk)

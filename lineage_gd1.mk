#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#

# Release name
PRODUCT_RELEASE_NAME := gd1

# Inherit device configuration
$(call inherit-product, device/samsung/gd1/gd1.mk)
$(call inherit-product, device/samsung/smdk4412-common/common.mk)

# Inherit from the common Open Source product configuration
$(call inherit-product, $(SRC_TARGET_DIR)/product/aosp_base_telephony.mk)

# Boot animation
TARGET_SCREEN_HEIGHT := 1280
TARGET_SCREEN_WIDTH := 720

# Inherit some common stuff (Go / low RAM)
$(call inherit-product, vendor/lineage/config/common_mini_go_phone.mk)

# Device identifier. This must come after all inclusions
PRODUCT_DEVICE := gd1
PRODUCT_NAME := lineage_gd1
PRODUCT_BRAND := samsung
PRODUCT_MODEL := EK-GC100
PRODUCT_MANUFACTURER := samsung

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRODUCT_NAME=gd1xx \
    TARGET_DEVICE=gd1 \
    PRIVATE_BUILD_DESC="gd1xx-user 4.1.2 JZO54K GC100XXBMC4 release-keys"

BUILD_FINGERPRINT := samsung/gd1xx/gd1:4.1.2/JZO54K/GC100XXBMC4:user/release-keys

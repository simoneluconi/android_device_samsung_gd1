#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#
# The GC100 (EK-GC100). The shared settings are in BoardConfigCommon.mk.
#

include device/samsung/gd1/BoardConfigCommon.mk

DEVICE_PATH := device/samsung/gd1

# RIL (Java class for the XMM6262 Samsung IPC RIL)
BOARD_RIL_CLASS := ../../../$(DEVICE_PATH)/ril

# Kernel
TARGET_KERNEL_CONFIG := lineageos_gd1_defconfig

# Partitions: stock GC100 layout, no repartition (see CLAUDE.md)
BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 8388608
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1610612736
BOARD_CACHEIMAGE_PARTITION_SIZE := 1073741824
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_PARTITION_SIZE := 377487360
BOARD_USERDATAIMAGE_PARTITION_SIZE := 4441767936

TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/fstab.smdk4x12

# Assert
TARGET_OTA_ASSERT_DEVICE := gd1,gd1xx,EK-GC100

# Init
TARGET_INIT_VENDOR_LIB := libinit_gd1

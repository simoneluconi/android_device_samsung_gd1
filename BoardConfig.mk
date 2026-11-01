#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#

include device/samsung/smdk4412-common/BoardCommonConfig.mk

DEVICE_PATH := device/samsung/gd1

# Wifi
WIFI_DRIVER_MODULE_PATH :=

# Bionic
MALLOC_SVELTE := true
TARGET_NEEDS_PLATFORM_TEXT_RELOCATIONS := true
LIBART_IMG_BASE := 0x30000000

# The linker matches these against the executable only: the GNSS service
# dlopens the stock GPS HAL, which has text relocations
TARGET_PROCESS_SDK_VERSION_OVERRIDE += \
    /vendor/bin/hw/android.hardware.gnss@1.0-service.exynos4=22

# Graphics
TARGET_REQUIRES_SYNCHRONOUS_SETSURFACE := true

# RIL (Java class for the XMM6262 Samsung IPC RIL)
BOARD_RIL_CLASS := ../../../$(DEVICE_PATH)/ril

# Bluetooth
BOARD_BLUETOOTH_BDROID_BUILDCFG_INCLUDE_DIR := $(DEVICE_PATH)/bluetooth

# Kernel
TARGET_KERNEL_SOURCE := kernel/samsung/smdk4412
TARGET_KERNEL_CONFIG := lineageos_gd1_defconfig

# Partitions: stock GC100 layout, no repartition (see CLAUDE.md)
BOARD_BOOTIMAGE_PARTITION_SIZE := 8388608
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 8388608
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 1610612736
BOARD_CACHEIMAGE_PARTITION_SIZE := 1073741824
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_PARTITION_SIZE := 377487360
BOARD_USERDATAIMAGE_PARTITION_SIZE := 4441767936

# Recovery: the 8MB RECOVERY partition cannot hold the LineageOS recovery,
# flashing is done with Odin (see docs/RECOVERY.md)
# VINTF
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += $(DEVICE_PATH)/framework_compatibility_matrix.xml

# SELinux permissive for bring-up (honoured on userdebug only): there is no
# device policy for the vendor services yet. See docs/TODO.md.
BOARD_KERNEL_CMDLINE += androidboot.selinux=permissive

# Minimal device policy: domains for the vendor daemons that init must
# start in their own context (cbd, gpsd). smdk4412-common's selinux/ is
# not wired into the build and is older than html6405's shipped policy.
BOARD_VENDOR_SEPOLICY_DIRS += $(DEVICE_PATH)/sepolicy/vendor

# Root folders: only /efs is mounted. The common tree also creates /misc
# (a raw partition here), /preload (HIDDEN is our /vendor) and a /tombstones
# symlink (old Samsung blobs). None of them has a SELinux label in
# LineageOS-UL, and they break the system image.
BOARD_ROOT_EXTRA_FOLDERS := efs
BOARD_ROOT_EXTRA_SYMLINKS :=

TARGET_NO_RECOVERY := true
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/fstab.smdk4x12
RECOVERY_FSTAB_VERSION := 2

# PowerHAL
TARGET_POWERHAL_VARIANT := pegasusq

# Assert
TARGET_OTA_ASSERT_DEVICE := gd1,gd1xx,EK-GC100

# Init
TARGET_INIT_VENDOR_LIB := libinit_gd1

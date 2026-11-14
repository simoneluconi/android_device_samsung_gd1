#
# Copyright (C) 2013 Paul Kocialkowski
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#

# Galaxy Camera (gd1) camera HAL: smdk4412-common's open HAL1 exynos_camera,
# specialised for the Fujitsu M9MO ISP (optical zoom, PASM, OIS, RAW).
# Build with EXYNOS_CAMERA_DISABLED to report no cameras (kill switch).

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
	exynos_camera.c \
	exynos_exif.c \
	exynos_jpeg.c \
	exynos_param.c \
	exynos_utils.c \
	exynos_v4l2.c \
	exynos_v4l2_output.c \
	exynos_ion.c \
	exynos_hdmi.c

LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/include \
	$(LOCAL_PATH)/../hdmid \
	system/media/camera/include \
	hardware/samsung/exynos4/hal/include

LOCAL_SHARED_LIBRARIES := libutils libcutils liblog libhardware libhwjpeg
LOCAL_PRELINK_MODULE := false

LOCAL_CFLAGS += -DEXYNOS_M9MO -DEXYNOS_JPEG_HW -DEXYNOS_ION

LOCAL_MODULE := camera.$(TARGET_BOOTLOADER_BOARD_NAME)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)

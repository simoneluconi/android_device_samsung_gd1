#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := gpsd_shim.cpp
LOCAL_SHARED_LIBRARIES := libssl libutils
LOCAL_CFLAGS := -Wall -Werror -Wno-unused-parameter
LOCAL_MODULE := libgpsd_shim
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_VENDOR_MODULE := true

include $(BUILD_SHARED_LIBRARY)

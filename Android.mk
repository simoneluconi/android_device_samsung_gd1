#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

# Set by BoardConfigCommon.mk, for the GC100 and any model built on it
ifeq ($(TARGET_GALAXYCAMERA_COMMON),true)
include $(call all-makefiles-under,$(LOCAL_PATH))
endif

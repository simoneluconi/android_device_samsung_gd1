#
# Copyright (C) 2020 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#
# Vendor properties (inherited from the i9300 mini-go tree), common to the
# Galaxy Cameras. The RIL and RAM ones are in gd1.mk.
#
PRODUCT_PROPERTY_OVERRIDES += \
	config.disable_atlas=true \
	dalvik.vm.dexopt-data-only=1 \
	dalvik.vm.dex2oat-Xmx=256m \
	ro.sys.fw.dex2oat_thread_count=2 \
	ro.sf.lcd_density=320 \
	ro.lcd_min_brightness=20

# Display: the panel is 720x1280 portrait, but the camera is used in
# landscape. Rotate the primary display like stock does (SurfaceFlinger
# orientation 1), so landscape is the natural orientation.
PRODUCT_PROPERTY_OVERRIDES += \
	ro.surface_flinger.primary_display_orientation=ORIENTATION_90

# EGL blobs crash on screen off
PRODUCT_PROPERTY_OVERRIDES += \
	ro.egl.destroy_after_detach=true

# Camera API
PRODUCT_PROPERTY_OVERRIDES += \
	persist.camera.HAL3.enabled=1

# hwui
PRODUCT_PROPERTY_OVERRIDES += \
    ro.hwui.texture_cache_size=6 \
    ro.hwui.layer_cache_size=4 \
    ro.hwui.gradient_cache_size=0.2 \
    ro.hwui.path_cache_size=1.0 \
    ro.hwui.drop_shadow_cache_size=1 \
    ro.hwui.r_buffer_cache_size=1 \
    persist.sys.force_highendgfx=false

# Free up RAM by purging assets.
PRODUCT_PROPERTY_OVERRIDES += \
	persist.sys.purgeable_assets=1

# Better RAM management
PRODUCT_PROPERTY_OVERRIDES += \
	ro.HOME_APP_ADJ=1

# GPS
PRODUCT_PROPERTY_OVERRIDES += \
	ro.ril.def.agps.mode=1

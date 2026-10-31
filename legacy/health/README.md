# Legacy health@2.0 implementation

`android.hardware.health@2.0-impl`, `libhealthservice`, `libhealthstoragedefault` and the generic
`android.hardware.health@2.0-service` (formerly `system/core/healthd`), taken unchanged from
AOSP `hardware/interfaces/health/2.0` (as kept in html6405's `android_hardware_interfaces` fork, branch `lineage-21.0`).

LineageOS-UL 21 removed them, but the exynos4 health HAL in html6405's `hardware/samsung` (`lineage-21.0-exynos4`)
still links against them. They are plain C++ libraries, so they can live here instead of patching `hardware/interfaces`.

License: Apache 2.0 (see the headers of each file).

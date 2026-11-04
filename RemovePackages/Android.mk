#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#
# The Galaxy Camera is a camera, not a phone: drop the apps it does not need
# (dialer, NFC, printing, browser and stores, wallpapers and themes, ...).
# Overriding them keeps LineageOS itself untouched. The messaging app is not
# removed here: the GC100 has a modem and keeps it (gd1.mk); a model without
# one removes it in its own tree.
# See "Build profile" in the project CLAUDE.md for the keep/remove decisions.

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := RemovePackages
LOCAL_MODULE_CLASS := APPS
LOCAL_MODULE_TAGS := optional
LOCAL_OVERRIDES_PACKAGES := \
    AccessibilityMenu \
    AuroraStore \
    Backgrounds \
    BasicDreams \
    BluetoothMidiService \
    BookmarkProvider \
    BuiltInPrintService \
    CallLogBackup \
    Camera2 \
    CameraExtensionsProxy \
    CellBroadcastLegacyApp \
    CompanionDeviceManager \
    Contacts \
    DeskClock \
    DeviceAsWebcam \
    Dialer \
    DynamicSystemInstallationService \
    E2eeContactKeysProvider \
    EasterEgg \
    EmergencyInfo \
    ExactCalculator \
    FontNotoSerifSourceOverlay \
    Gallery2 \
    Glimpse \
    GlimpseFrameworksBaseOverlay \
    IconPackCircularAndroidOverlay \
    IconPackCircularLauncherOverlay \
    IconPackCircularSettingsOverlay \
    IconPackCircularSystemUIOverlay \
    IconPackFilledAndroidOverlay \
    IconPackFilledLauncherOverlay \
    IconPackFilledSettingsOverlay \
    IconPackFilledSystemUIOverlay \
    IconPackKaiAndroidOverlay \
    IconPackKaiLauncherOverlay \
    IconPackKaiSettingsOverlay \
    IconPackKaiSystemUIOverlay \
    IconPackRoundedAndroidOverlay \
    IconPackRoundedLauncherOverlay \
    IconPackRoundedSettingsOverlay \
    IconPackRoundedSystemUIOverlay \
    IconPackSamAndroidOverlay \
    IconPackSamLauncherOverlay \
    IconPackSamSettingsOverlay \
    IconPackSamSystemUIOverlay \
    IconPackVictorAndroidOverlay \
    IconPackVictorLauncherOverlay \
    IconPackVictorSettingsOverlay \
    IconPackVictorSystemUIOverlay \
    IconShapePebbleOverlay \
    IconShapeRoundedRectOverlay \
    IconShapeSquareOverlay \
    IconShapeSquircleOverlay \
    IconShapeTaperedRectOverlay \
    IconShapeTeardropOverlay \
    IconShapeVesselOverlay \
    ImsServiceEntitlement \
    Jelly \
    LiveWallpapersPicker \
    Music \
    MusicFX \
    NfcNci \
    ONS \
    OpenCamera \
    PhotoTable \
    PrintRecommendationService \
    PrintSpooler \
    QuickAccessWallet \
    SamsungDoze \
    SecureElement \
    SettingsIntelligence \
    StatementService \
    Stk \
    ThemePicker \
    ThemesStub \
    Updater \
    WAPPushManager
LOCAL_UNINSTALLABLE_MODULE := true
LOCAL_CERTIFICATE := PRESIGNED
LOCAL_SRC_FILES := /dev/null
include $(BUILD_PREBUILT)

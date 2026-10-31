#!/vendor/bin/sh
# Camera-first first-boot setup, run once the framework is up.
# - Skip the setup wizard: mark the device as set up and turn the wizard off.
#   The wizard itself stays installed because it replaces AOSP Provision,
#   so without it nothing would mark the device as provisioned.
# - Disable the Mainline (APEX) apps a camera does not need. They cannot be
#   overridden in the build like the plain system apps (see RemovePackages).
# Runs on every boot; all steps are no-ops once done.

settings put global device_provisioned 1
settings put secure user_setup_complete 1

# Apply the camera-first quick settings tiles once, also on devices that
# already saved the stock set
if [ "$(settings get secure gd1_qs_reset)" != 1 ]; then
    settings delete secure sysui_qs_tiles
    settings put secure gd1_qs_reset 1
fi

for p in \
    org.lineageos.setupwizard \
    com.android.adservices.api \
    com.android.sdksandbox \
    com.android.ondevicepersonalization.services \
    com.android.federatedcompute.services \
    com.android.healthconnect.controller \
    com.android.health.connect.backuprestore \
    com.android.devicelockcontroller \
    com.android.cellbroadcastreceiver.module \
    com.android.rkpdapp
do
    pm disable-user --user 0 "$p" > /dev/null 2>&1
done

#!/vendor/bin/sh
# Camera-first first-boot setup, run once the framework is up.
# - Skip the setup wizard: mark the device as set up and turn the wizard off.
#   The wizard itself stays installed because it replaces AOSP Provision,
#   so without it nothing would mark the device as provisioned.
# - Disable the Mainline (APEX) apps a camera does not need. They cannot be
#   overridden in the build like the plain system apps (see RemovePackages).
# - Apply the camera-first quick settings tiles and the Rome time zone once.
# Runs on every boot; all steps are no-ops once done. Output: logcat -s gd1-setup

log -t gd1-setup "started"

# sys.boot_completed can come before the settings and package services answer
i=0
until settings get global device_provisioned > /dev/null 2>&1 && pm path android > /dev/null 2>&1; do
    i=$((i + 1))
    if [ $i -gt 60 ]; then
        log -t gd1-setup "framework not ready after 120 s, giving up"
        exit 1
    fi
    sleep 2
done

settings put global device_provisioned 1
settings put secure user_setup_complete 1

if [ "$(settings get secure gd1_firstboot_done)" != 1 ]; then
    settings delete secure sysui_qs_tiles
    setprop persist.sys.timezone Europe/Rome
    settings put secure gd1_firstboot_done 1
    log -t gd1-setup "quick settings tiles and time zone set"
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
    pm disable-user --user 0 "$p" > /dev/null 2>&1 || log -t gd1-setup "could not disable $p"
done

log -t gd1-setup "done"

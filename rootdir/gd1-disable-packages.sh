#!/vendor/bin/sh
# Disable the Mainline (APEX) apps a camera does not need. They cannot be
# overridden in the build like the plain system apps (see RemovePackages), so
# turn them off once the framework is up. Runs on every boot; pm is a no-op
# for packages that are already disabled.

for p in \
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

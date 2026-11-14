#!/system/bin/sh
# Camera-first first-boot setup, run once the framework is up.
# - Skip the setup wizard: mark the device as set up and turn the wizard off.
#   The wizard itself stays installed because it replaces AOSP Provision,
#   so without it nothing would mark the device as provisioned.
# - Disable the Mainline (APEX) apps a camera does not need. They cannot be
#   overridden in the build like the plain system apps (see RemovePackages).
# - Apply the camera-first quick settings tiles, the Rome time zone, the
#   24-hour clock and the battery percentage once.
# - Add GC Gallery's Wi-Fi transfer tile to the quick settings, once.
# Runs on every boot; all steps are no-ops once done. Output: logcat -s gd1-setup
# Never disable com.android.sdksandbox: PackageManagerService requires it
# and system_server crashes at boot without it.

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
    # SystemUI keeps the tile list in memory and writes it straight back over
    # any outside change, so set it while SystemUI is restarting
    kill $(pidof com.android.systemui)
    settings put secure sysui_qs_tiles internet,bt,airplane,rotation,location,battery,dark,caffeine
    setprop persist.sys.timezone Europe/Rome
    # 24-hour clock (user, 2026-11-01)
    settings put system time_12_24 24
    # No "Discover Trust" onboarding notification
    settings put --lineage system trust_interface_hinted 1
    # Battery percentage next to the status bar icon (user, 2026-11-02)
    settings put --lineage system status_bar_show_battery_percent 2
    settings put secure gd1_firstboot_done 1
    log -t gd1-setup "quick settings tiles, time zone, 24-hour clock, battery percentage and Trust hint set"
fi

# Once, also on cameras set up before it: GC Gallery's Wi-Fi transfer tile, fifth, so the
# second row starts with it, under Internet (user, 2026-11-14). Only once that GC Gallery
# has the tile, because SystemUI drops (and writes back without) a tile whose service does
# not exist.
TRANSFER_TILE=com.simoneluconi.gcgallery/.transfer.TransferTile
if [ "$(settings get secure gd1_transfer_tile_done)" != 1 ] &&
    pm query-services --brief -a android.service.quicksettings.action.QS_TILE 2>/dev/null | grep -qF "$TRANSFER_TILE"; then
    tiles=$(settings get secure sysui_qs_tiles)
    case "$tiles" in
        *"custom($TRANSFER_TILE)"*) ;;
        null|"") ;;
        *)
            # After the fourth tile (at the end if there are fewer)
            tiles=$(echo "$tiles" | awk -F, -v t="custom($TRANSFER_TILE)" '{
                o = ""
                for (i = 1; i <= NF; i++) { o = o (i > 1 ? "," : "") $i; if (i == 4) o = o "," t }
                if (NF < 4) o = o "," t
                print o }')
            kill $(pidof com.android.systemui)
            settings put secure sysui_qs_tiles "$tiles"
            ;;
    esac
    settings put secure gd1_transfer_tile_done 1
    log -t gd1-setup "Wi-Fi transfer tile added"
fi

for p in \
    org.lineageos.setupwizard \
    com.android.adservices.api \
    com.android.ondevicepersonalization.services \
    com.android.federatedcompute.services \
    com.android.healthconnect.controller \
    com.android.health.connect.backuprestore \
    com.android.cellbroadcastreceiver.module \
    com.android.rkpdapp
do
    pm disable-user --user 0 "$p" > /dev/null 2>&1 || log -t gd1-setup "could not disable $p"
done

log -t gd1-setup "done"

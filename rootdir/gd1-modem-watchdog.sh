#!/vendor/bin/sh
#
# SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
# SPDX-License-Identifier: Apache-2.0
#
# The XMM6262 sometimes fails to come up at boot (about one boot in seven
# on 2026-11-02/08/11): the modem drops to CRASH_RESET a few seconds after
# cbd powers it, cbd's reload then never gets it online, rild waits for it
# forever without registering IRadio, and com.android.phone dies with
# "failed to complete startup" every 90 s until the next reboot. A reboot
# always brought it back, so do the same without the reboot: if rild has no
# baseband version a minute after boot, restart cbd and rild. Two tries,
# each given another minute.
#
# That helps when the modem crashed after booting. It does not when the
# HSIC link itself fails (2026-11-12: "XactErr", "port 2 reset error -110",
# then the boot ROM never connects again); only a reboot fixed that one
# (docs/TODO.md).
#
# Started once by init at sys.boot_completed (init.target.rc).

up() { [ -n "$(getprop gsm.version.baseband)" ]; }

try=0
while true; do
    t=0
    while [ $t -lt 60 ]; do
        if up; then
            [ $try -gt 0 ] && log -p i -t gd1-modem "modem up after $try restarts"
            exit 0
        fi
        sleep 5
        t=$((t + 5))
    done
    [ $try -ge 2 ] && break
    try=$((try + 1))
    log -p w -t gd1-modem "modem not up after ${t}s (cbd boot_done='$(getprop ril.cbd.boot_done)'), restarting cbd and rild, try $try"
    stop vendor.ril-daemon
    stop cpboot-daemon
    sleep 2
    start cpboot-daemon
    sleep 1
    start vendor.ril-daemon
done
log -p e -t gd1-modem "modem still not up after $try restarts"

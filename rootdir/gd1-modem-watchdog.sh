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
# baseband version a minute after boot, restart cbd and rild. Two tries.
#
# Started once by init at sys.boot_completed (init.target.rc).

up() { [ -n "$(getprop gsm.version.baseband)" ]; }

try=0
while [ $try -lt 2 ]; do
    t=0
    while [ $t -lt 60 ]; do
        up && exit 0
        sleep 5
        t=$((t + 5))
    done
    try=$((try + 1))
    log -p w -t gd1-modem "modem not up after ${t}s (cbd boot_done='$(getprop ril.cbd.boot_done)'), restarting cbd and rild, try $try"
    stop vendor.ril-daemon
    stop cpboot-daemon
    sleep 2
    start cpboot-daemon
    sleep 1
    start vendor.ril-daemon
done
up || log -p e -t gd1-modem "modem still not up after $try restarts"

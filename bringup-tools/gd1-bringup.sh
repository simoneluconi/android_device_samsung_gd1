#!/vendor/bin/sh
# Bring-up diagnostics for the Galaxy Camera (userdebug builds only).
# Every 20 s for about 3 minutes, save the kernel log and the graphics and
# USB state to /data/bringup, so that a failed boot can be read back from
# the mini recovery.

D=/data/bringup
mkdir -p $D
rm -f $D/*

for i in 1 2 3 4 5 6 7 8 9; do
    {
        echo "== $(date) uptime: $(cat /proc/uptime)"
        echo "== usb"
        for f in enable state functions idVendor idProduct f_ffs/aliases; do
            echo "$f: $(cat /sys/class/android_usb/android0/$f)"
        done
        ls -la /dev/usb-ffs/adb
        echo "== gpu and memory devices"
        ls -la /dev/mali /dev/ump /dev/ion /dev/graphics /dev/fimg2d /dev/exynos-mem
        echo "== processes"
        ps -A -o PID,USER,STAT,NAME
        echo "== meminfo"
        cat /proc/meminfo
        echo "== properties"
        getprop
    } > $D/state-$i.txt 2>&1
    dmesg > $D/dmesg-$i.txt 2>&1
    chmod 0644 $D/*
    sleep 20
done

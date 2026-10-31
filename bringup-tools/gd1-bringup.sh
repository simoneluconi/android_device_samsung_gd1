#!/vendor/bin/sh
# Bring-up diagnostics for the Galaxy Camera (userdebug builds only).
# - At once: keep the kernel log of the previous boot (/proc/last_kmsg, the
#   RAM console), so a kernel panic is never lost. The last 10 are kept.
# - Then every 20 s for about 3 minutes: save the kernel log and the graphics
#   and USB state, so a failed boot can be read back from the mini recovery.

D=/data/bringup
mkdir -p $D/last_kmsg
stamp=$(date +%Y%m%d-%H%M%S)
cp /proc/last_kmsg $D/last_kmsg/last_kmsg-$stamp.txt
ls -1t $D/last_kmsg/last_kmsg-*.txt | tail -n +11 | while read f; do rm -f "$f"; done
rm -f $D/state-*.txt $D/dmesg-*.txt

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
    chmod -R a+rX $D
    sleep 20
done

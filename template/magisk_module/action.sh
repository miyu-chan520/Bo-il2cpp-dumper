#!/system/bin/sh

STATUS=$(getprop persist.il2cppdumper.enabled)

if [ "$STATUS" = "0" ]; then
    setprop persist.il2cppdumper.enabled 1
    echo "========================================="
    echo " [Zygisk-Il2CppDumper] Status: ENABLED (ON)"
    echo "========================================="
    echo " Dumper is now turned ON."
    echo " Launching target games will trigger dump."
    echo " Default target: com.dts.freefireth"
    echo " Custom targets: /sdcard/Bo_dump/target.txt"
    echo " Output path:    /sdcard/Bo_dump/<package>/"
    echo " Click Action again to disable."
    echo "========================================="
else
    setprop persist.il2cppdumper.enabled 0
    echo "========================================="
    echo " [Zygisk-Il2CppDumper] Status: DISABLED (OFF)"
    echo "========================================="
    echo " Dumper is now turned OFF."
    echo " Launching games will NOT trigger dump."
    echo " Click Action again to re-enable."
    echo "========================================="
fi

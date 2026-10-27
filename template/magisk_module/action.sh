#!/system/bin/sh
MODDIR=${0%/*}

# Ensure Bo_dump storage directory exists with full permissions
mkdir -p /data/media/0/Bo_dump
chmod -R 0777 /data/media/0/Bo_dump 2>/dev/null
chown -R media_rw:media_rw /data/media/0/Bo_dump 2>/dev/null

# Ensure target.txt exists
if [ ! -f /data/media/0/Bo_dump/target.txt ]; then
    cat << 'EOF' > /data/media/0/Bo_dump/target.txt
# Target packages to dump (one package per line)
# Enter '*' to dump any Unity game
com.dts.freefireth
EOF
    chmod 0777 /data/media/0/Bo_dump/target.txt
fi

DISABLE_FILE="$MODDIR/disable"

if [ -f "$DISABLE_FILE" ]; then
    rm -f "$DISABLE_FILE"
    echo "========================================="
    echo " [Zygisk-Il2CppDumper] Status: ENABLED (ON)"
    echo "========================================="
    echo " Dumper is now ACTIVE."
    echo " Target games will be dumped on launch."
    echo " Config file: /sdcard/Bo_dump/target.txt"
    echo " Output path: /sdcard/Bo_dump/<package>/"
    echo " Tap Action again to turn OFF."
    echo "========================================="
else
    touch "$DISABLE_FILE"
    echo "========================================="
    echo " [Zygisk-Il2CppDumper] Status: DISABLED (OFF)"
    echo "========================================="
    echo " Dumper is now DEACTIVATED."
    echo " Saved in /data/adb/modules/zygisk_il2cppdumper/disable"
    echo " Games will run normally without dumping."
    echo " Tap Action again to turn ON."
    echo "========================================="
fi

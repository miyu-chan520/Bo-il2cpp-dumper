#!/system/bin/sh
# KernelSU / APatch / Magisk Action Script for Zygisk-Il2CppDumper

PKG=$(getprop persist.il2cppdumper.package)
if [ -z "$PKG" ]; then
    PKG="com.dts.freefireth"
fi

echo "========================================"
echo "       Zygisk-Il2CppDumper Manager      "
echo "========================================"
echo "Current Target Package: $PKG"
echo ""

DUMP_PATH="/data/data/$PKG/files/dump.cs"
if [ -f "$DUMP_PATH" ]; then
    SIZE=$(ls -lh "$DUMP_PATH" | awk '{print $5}')
    echo "[+] Found dump.cs ($SIZE) in $PKG"
    
    mkdir -p /sdcard/Download
    cp "$DUMP_PATH" "/sdcard/Download/${PKG}_dump.cs"
    chmod 666 "/sdcard/Download/${PKG}_dump.cs"
    echo "[+] Successfully copied to /sdcard/Download/${PKG}_dump.cs"
else
    echo "[-] dump.cs not found yet."
    echo "    Please launch $PKG and wait for game assets to load."
fi

echo "========================================"

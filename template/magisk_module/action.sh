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

mkdir -p /sdcard/Download

DUMP_PATH="/data/data/$PKG/files/dump.cs"
if [ -f "$DUMP_PATH" ]; then
    SIZE=$(ls -lh "$DUMP_PATH" | awk '{print $5}')
    echo "[+] Found dump.cs ($SIZE) in $PKG"
    cp "$DUMP_PATH" "/sdcard/Download/${PKG}_dump.cs"
    chmod 666 "/sdcard/Download/${PKG}_dump.cs"
    echo "[+] Copied to /sdcard/Download/${PKG}_dump.cs"
else
    echo "[-] dump.cs not found yet."
fi

SCRIPT_PATH="/data/data/$PKG/files/script.json"
if [ -f "$SCRIPT_PATH" ]; then
    SIZE=$(ls -lh "$SCRIPT_PATH" | awk '{print $5}')
    echo "[+] Found script.json ($SIZE) in $PKG"
    cp "$SCRIPT_PATH" "/sdcard/Download/${PKG}_script.json"
    chmod 666 "/sdcard/Download/${PKG}_script.json"
    echo "[+] Copied to /sdcard/Download/${PKG}_script.json"
else
    echo "[-] script.json not found yet."
fi

LOG_PATH="/data/data/$PKG/files/il2cppdumper.log"
if [ ! -f "$LOG_PATH" ]; then
    LOG_PATH="/data/local/tmp/il2cppdumper.log"
fi
if [ -f "$LOG_PATH" ]; then
    echo ""
    echo "--- Latest Execution Log ---"
    cat "$LOG_PATH"
fi

echo "========================================"

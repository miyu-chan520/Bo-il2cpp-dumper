#!/system/bin/sh
# KernelSU / APatch / Magisk Action Script for Zygisk-Il2CppDumper

echo "=========================================="
echo "        Zygisk-Il2CppDumper Manager       "
echo "=========================================="

DUMP_DIR="/sdcard/Bo_dump"
echo "Dump Directory: $DUMP_DIR"
echo ""

if [ -d "$DUMP_DIR" ]; then
    echo "[+] Files in $DUMP_DIR:"
    ls -lh "$DUMP_DIR" 2>/dev/null
else
    echo "[-] Directory $DUMP_DIR not created yet."
fi

echo ""
PROP_PKG=$(getprop persist.il2cppdumper.package)
if [ -n "$PROP_PKG" ]; then
    echo "Target Package (override): $PROP_PKG"
else
    echo "Target Package: com.dts.freefireth (default)"
fi

if [ -f "/data/local/tmp/il2cppdumper_disabled" ]; then
    echo "Dumper Engine Status: [ DISABLED ]"
else
    echo "Dumper Engine Status: [ ENABLED ]"
fi

echo "=========================================="

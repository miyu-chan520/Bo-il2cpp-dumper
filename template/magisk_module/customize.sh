#!/system/bin/sh

# Set execute permissions
set_perm $MODPATH/action.sh 0 0 0755
set_perm $MODPATH/service.sh 0 0 0755

# Ensure Bo_dump exists with 0777 permissions
mkdir -p /data/media/0/Bo_dump
chmod -R 0777 /data/media/0/Bo_dump 2>/dev/null
chown -R media_rw:media_rw /data/media/0/Bo_dump 2>/dev/null

# Create default target.txt if not exists
if [ ! -f /data/media/0/Bo_dump/target.txt ]; then
    cat << 'EOF' > /data/media/0/Bo_dump/target.txt
# Target packages to dump (one package per line)
# Enter '*' to dump any Unity game
com.dts.freefireth
com.dts.freefiremax
EOF
    chmod 0777 /data/media/0/Bo_dump/target.txt
fi

# Copy target.txt to module directory as backup
cp -f /data/media/0/Bo_dump/target.txt $MODPATH/target.txt 2>/dev/null

ui_print "**********************************************"
ui_print " Zygisk-Il2CppDumper v3.1.0"
ui_print "**********************************************"
ui_print "- Action button: Configured and enabled"
ui_print "- Storage directory: /sdcard/Bo_dump/ (0777)"
ui_print "- Config file: /sdcard/Bo_dump/target.txt"
ui_print "- Default targets: com.dts.freefireth, com.dts.freefiremax"
ui_print "- Output folder: /sdcard/Bo_dump/<package>/"
ui_print "**********************************************"

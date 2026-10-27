#!/system/bin/sh
MODDIR=${0%/*}

# Wait until storage is ready
while [ "$(getprop sys.boot_completed)" != "1" ]; do
    sleep 2
done

# Ensure Bo_dump storage directory exists with full permissions
mkdir -p /data/media/0/Bo_dump
chmod -R 0777 /data/media/0/Bo_dump 2>/dev/null
chown -R media_rw:media_rw /data/media/0/Bo_dump 2>/dev/null

if [ ! -f /data/media/0/Bo_dump/target.txt ]; then
    cat << 'EOF' > /data/media/0/Bo_dump/target.txt
# Target packages to dump (one package per line)
# Enter '*' to dump any Unity game
com.dts.freefireth
EOF
    chmod 0777 /data/media/0/Bo_dump/target.txt
fi

cp -f /data/media/0/Bo_dump/target.txt $MODDIR/target.txt 2>/dev/null

#!/bin/bash
set -e

SIZE_MB="${1:-64}"
OUTPUT="$(realpath -m "${2:-bin/disk.img}")"
STAGE="$(mktemp -d)"

cleanup() {
    rm -rf "$STAGE"
}
trap cleanup EXIT

mkdir -p "$(dirname "$OUTPUT")"
rm -f "$OUTPUT"

dd if=/dev/zero of="$OUTPUT" bs=1M count="$SIZE_MB" status=none
mkfs.ext2 -F -L "SNAKEOS" -b 1024 "$OUTPUT" >/dev/null

mkdir -p "$STAGE"/{bin,etc,home,dev,proc,sys,tmp,var,usr/bin,usr/lib}
mkdir -p "$STAGE"/boot/grub

cat > "$STAGE/README.txt" <<'EOF'
SnakeOS v0.00
EOF

cat > "$STAGE/etc/motd" <<'EOF'
Welcome
EOF

echo "Hello from SnakeOS disk image!" > "$STAGE/home/hello.txt"
pushd "$STAGE" >/dev/null

find . -type d | while read -r d; do
    rel="${d#.}"
    [ -z "$rel" ] && continue
    debugfs -w -R "mkdir ${rel}" "$OUTPUT" >/dev/null 2>&1 || true
done

find . -type f | while read -r f; do
    rel="${f#.}"
    debugfs -w -R "write ${f} ${rel}" "$OUTPUT" >/dev/null 2>&1
done

popd >/dev/null

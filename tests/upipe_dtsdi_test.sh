#!/bin/sh

set -e

srcdir="$1"

TMP="`mktemp -d tmp.XXXXXXXXXX`"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

for i in 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F 10 11 12 13 14 15 16 17 18 19 1A; do
    printf 'DekTec.dtsdi\x00\x'$i'\x01\x01' > "$TMP"/dtsdi
    head -c $((2750 * 1125 * 4)) /dev/zero >> "$TMP"/dtsdi
    "$srcdir"/valgrind_wrapper.sh "$srcdir" ./upipe_dtsdi_test "$TMP"/dtsdi
done

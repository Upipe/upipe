#!/bin/sh

set -e

srcdir="$1"

TMP="`mktemp -d tmp.XXXXXXXXXX`"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

for file in "$srcdir"/upipe_m3u_reader_test_files/*.m3u; do
    echo "$file"
    "$srcdir"/valgrind_wrapper.sh "$srcdir" ./upipe_m3u_reader_test $file > "$TMP"/logs
    diff -u "$file".logs "$TMP"/logs
done

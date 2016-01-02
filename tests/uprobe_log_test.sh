#!/bin/sh

set -e

srcdir="$1"

TMP="`mktemp -d tmp.XXXXXXXXXX`"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

"$srcdir"/valgrind_wrapper.sh "$srcdir" ./uprobe_log_test > "$TMP"/logs
sed -e "s/\(error at\) .*$/\1/" -i "$TMP"/logs
diff -u "$srcdir"/uprobe_log_test.txt "$TMP"/logs

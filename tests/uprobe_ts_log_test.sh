#!/bin/sh

set -e

srcdir="$1"

TMP="`mktemp -d tmp.XXXXXXXXXX`"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

"$srcdir"/valgrind_wrapper.sh "$srcdir" ./uprobe_ts_log_test > "$TMP"/logs
diff -u "$srcdir"/uprobe_ts_log_test.txt "$TMP"/logs

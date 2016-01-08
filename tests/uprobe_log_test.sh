#!/bin/sh

set -e

srcdir="$1"

TMP="`mktemp -d tmp.XXXXXXXXXX`"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

"$srcdir"/valgrind_wrapper.sh "$srcdir" ./uprobe_log_test > "$TMP"/logs

sed < "$TMP"/logs \
    -e "s/\(error at\) .*$/\1/" \
    > "$TMP"/logs2

diff -u "$srcdir"/uprobe_log_test.txt "$TMP"/logs2

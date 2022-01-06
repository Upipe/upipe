#!/bin/sh

set -e

srcdir="$1"

TMP="`mktemp -d tmp.XXXXXXXXXX`"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

"$srcdir"/valgrind_wrapper.sh "$srcdir" ./upipe_file_test "$srcdir"/upipe_ts_test.ts "$TMP"/test
cmp --quiet "$TMP"/test "$srcdir"/upipe_ts_test.ts

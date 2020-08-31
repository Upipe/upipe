#!/bin/sh

set -e

srcdir="$1"

TMP="`mktemp -d tmp.XXXXXXXXXX`"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

"$srcdir"/valgrind_wrapper.sh "$srcdir" ./uref_dump_test > "$TMP"/logs
diff -u "$srcdir"/uref_dump_test.txt "$TMP"/logs

#!/bin/sh

set -e

srcdir="$1"

TMP="`mktemp -d tmp.XXXXXXXXXX`"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

"$srcdir"/valgrind_wrapper.sh "$srcdir" ./udict_inline_test > "$TMP"/logs

cat "$srcdir"/udict_inline_test.txt > "$TMP"/ref
cat "$srcdir"/udict_inline_test.txt >> "$TMP"/ref
cat "$srcdir"/udict_inline_test.txt >> "$TMP"/ref

sed < "$TMP"/logs \
    -e "s/^\(debug: dumping udict\) .*$/\1/" \
    -e "s/^\(debug: end of attributes for udict\) .*$/\1/" \
    > "$TMP"/logs2

diff -u "$TMP"/ref "$TMP"/logs2

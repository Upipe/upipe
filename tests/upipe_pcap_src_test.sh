#!/bin/sh

set -e

srcdir="$1"

TMP="`mktemp -d tmp.XXXXXXXXXX`"
cleanup() { rm -rf "$TMP"; }
trap cleanup EXIT

"$srcdir"/valgrind_wrapper.sh "$srcdir" ./upipe_pcap_src_test "$srcdir"/upipe_pcap_src_test.pcap > "$TMP"/logs
diff -u "$srcdir"/upipe_pcap_src_test.txt "$TMP"/logs

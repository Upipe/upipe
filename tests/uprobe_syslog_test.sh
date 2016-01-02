#!/bin/sh

set -e

srcdir="$1"

"$srcdir"/valgrind_wrapper.sh "$srcdir" ./uprobe_syslog_test

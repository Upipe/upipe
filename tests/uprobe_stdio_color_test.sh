#!/bin/sh

srcdir="$1"
DIR="`mktemp -d tmp.XXXXXXXXXX`"
./uprobe_stdio_color_test > "$DIR"/logs
RET=$?
if test $RET -ne 0; then
	rm -rf "$DIR"
	exit $RET
fi

diff -q "$DIR"/logs "$srcdir"/uprobe_stdio_color_test.txt
RET=$?
rm -rf "$DIR"
if test $RET -ne 0; then
	exit $RET
fi

if ! which valgrind >/dev/null 2>&1; then
	echo "#### Please install valgrind for unit tests"
	exit 1
fi

unset DIR
FILE="`mktemp tmp.XXXXXXXXXX`"
../libtool --mode=execute valgrind -q --leak-check=full ./uprobe_stdio_color_test > /dev/null 2> "$FILE"
RET=$?
if test -s "$FILE"; then
        cat "$FILE" >&2
        RET=1
fi
rm -f "$FILE"
exit $RET

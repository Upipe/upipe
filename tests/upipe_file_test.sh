#!/bin/sh

TMP="`mktemp -d tmp.XXXXXXXXXX`"
if ! ./upipe_file_test Makefile "$TMP"/test; then
	rm -rf "$TMP"
	exit 1
fi
if ! cmp --quiet "$TMP"/test Makefile; then
	rm -rf "$TMP"
	exit 2
fi

if ! which valgrind >/dev/null 2>&1; then
	echo "#### Please install valgrind for unit tests"
	rm -rf "$TMP"
	exit 1
fi

# Run in valgrind, with leak checking enabled
libtool --mode=execute valgrind -q --leak-check=full ./upipe_file_test Makefile "$TMP"/test2 > /dev/null 2> "$TMP"/logs
RET=$?
if test -s "$TMP"/logs; then
        cat "$TMP"/logs >&2
        RET=1
fi
rm -rf "$TMP"
exit $RET

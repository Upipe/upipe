#!/bin/sh

TMPDIR="`mktemp -d tmp.XXXXXXXXXX`"
if ! ./upipe_file_test Makefile "$TMPDIR"/test; then
	rm -rf "$TMPDIR"
	exit 1
fi
if ! cmp --quiet "$TMPDIR"/test Makefile; then
	rm -rf "$TMPDIR"
	exit 2
fi

if ! which valgrind >/dev/null 2>&1; then
	echo "#### Please install valgrind for unit tests"
	rm -rf "$TMPDIR"
	exit 1
fi

# Run in valgrind, with leak checking enabled
libtool --mode=execute valgrind -q --leak-check=full ./upipe_file_test Makefile "$TMPDIR"/test2 > /dev/null 2> "$TMPDIR"/logs
RET=$?
if test -s "$TMPDIR"/logs; then
        cat "$TMPDIR"/logs >&2
        RET=1
fi
rm -rf "$TMPDIR"
exit $RET

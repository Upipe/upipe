#!/bin/sh

UNAME=$(uname)

srcdir="$1"
shift

TMP="`mktemp -d tmp.XXXXXXXXXX`"
if ! ./upipe_ts_test $srcdir/upipe_ts_test.ts "$TMP"/test.ts; then
	rm -rf "$TMP"
	exit 1
fi
if ! cmp --quiet "$TMP"/test.ts $srcdir/upipe_ts_test.ts; then
	rm -rf "$TMP"
	exit 2
fi

if ! which valgrind >/dev/null 2>&1; then
	echo "#### Please install valgrind for unit tests"
	rm -rf "$TMP"
	exit 1
fi

# valgrind suppressions
VALGRIND_SUPPRESSIONS=" --suppressions=$srcdir/valgrind.supp "
if [ "$UNAME" = "Darwin" ]; then
    VALGRIND_SUPPRESSIONS+=" --suppressions=$srcdir/valgrind_osx.supp "
fi

# Run in valgrind, with leak checking enabled
libtool --mode=execute valgrind -q --leak-check=full $VALGRIND_SUPPRESSIONS ./upipe_ts_test $srcdir/upipe_ts_test.ts "$TMP"/test2.ts > /dev/null 2> "$TMP"/logs
RET=$?
if test -s "$TMP"/logs; then
        cat "$TMP"/logs >&2
        RET=1
fi
rm -rf "$TMP"
exit $RET

#!/bin/sh

UNAME=$(uname)

TMP="`mktemp -d tmp.XXXXXXXXXX`"
mkdir -p "$TMP"/test1
if ! ./upipe_multicat_test -r 270000000 "$TMP"/test1/ .bar; then
	rm -rf "$TMP"
	exit 1
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

mkdir -p "$TMP"/test2
# Run in valgrind, with leak checking enabled
../libtool --mode=execute valgrind -q --leak-check=full $VALGRIND_SUPPRESSIONS ./upipe_multicat_test "$TMP"/test2/ .bar > /dev/null 2> "$TMP"/logs
RET=$?
if test -s "$TMP"/logs; then
        cat "$TMP"/logs >&2
        RET=1
fi
rm -rf "$TMP"
exit $RET

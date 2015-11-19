#!/bin/sh

if [ $# -ne 1 ]; then
    exit 1
fi

srcdir="$1"
UNAME=$(uname)
TESTNAME=upipe_m3u_reader_test
TESTGOOD="$srcdir"/"$TESTNAME"_files/
TMP="`mktemp -d tmp.XXXXXXXXXX`"

for file in "$srcdir"/"$TESTNAME"_files/*.m3u; do
    echo "$file"
    if ! ./"$TESTNAME" $file > "$TMP"/logs 2> /dev/null; then
        rm -rf "$TMP"
        exit 1
    fi

    if ! diff "$TMP"/logs "$file".logs; then
        rm -rf "$TMP"
        exit 1
    fi
done

echo "all"

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
../libtool --mode=execute valgrind -q --leak-check=full --log-file="$TMP"/logs $VALGRIND_SUPPRESSIONS ./"$TESTNAME" "$srcdir"/"$TESTNAME"_files/*.m3u > /dev/null 2> /dev/null
RET=$?
if test -s "$TMP"/logs; then
    cat "$TMP"/logs >&2
    rm -rf "$TMP"
    exit 1
fi

rm -rf "$TMP"

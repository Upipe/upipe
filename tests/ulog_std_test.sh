#!/bin/sh

srcdir="$1"
TMPDIR="`mktemp -d tmp.XXXXXXXXXX`"
./ulog_std_test > "$TMPDIR"/logs
RET=$?
if test $RET -ne 0; then
	rm -rf "$TMPDIR"
	exit $RET
fi

sed -e "s/^\(test error: allocation failure at\) .*$/\1/" < "$TMPDIR"/logs > "$TMPDIR"/logs2
diff -q "$TMPDIR"/logs2 "$srcdir"/ulog_std_test.txt
RET=$?
rm -rf "$TMPDIR"
if test $RET -ne 0; then
	exit $RET
fi

if ! which valgrind >/dev/null 2>&1; then
	echo "#### Please install valgrind for unit tests"
	exit 1
fi

unset TMPDIR
TMPFILE="`mktemp tmp.XXXXXXXXXX`"
libtool --mode=execute valgrind -q --leak-check=full ./ulog_std_test > /dev/null 2> "$TMPFILE"
RET=$?
if test -s "$TMPFILE"; then
        cat "$TMPFILE" >&2
        RET=1
fi
rm -f "$TMPFILE"
exit $RET

#!/bin/sh

srcdir="$1"
TMPDIR="`mktemp -d`"
./uprobe_print_test > "$TMPDIR"/logs
RET=$?
if test $RET -ne 0; then
	rm -rf "$TMPDIR"
	exit $RET
fi

ADDR=`head -n 1 "$TMPDIR"/logs`
tail -n +2 "$TMPDIR"/logs | sed -e "s/$ADDR/TEST_PIPE/g" > "$TMPDIR"/logs2
diff -q "$TMPDIR"/logs2 "$srcdir"/uprobe_print_test.txt
RET=$?
rm -rf "$TMPDIR"
if test $RET -ne 0; then
	exit $RET
fi

if ! which valgrind >/dev/null 2>&1; then
	echo "#### Please install valgrind for unit tests"
	exit 1
fi

TMPFILE="`mktemp`"
libtool --mode=execute valgrind -q --leak-check=full ./uprobe_print_test > /dev/null 2> "$TMPFILE"
RET=$?
if test -s "$TMPFILE"; then
        cat "$TMPFILE" >&2
        RET=1
fi
rm -f "$TMPFILE"
exit $RET

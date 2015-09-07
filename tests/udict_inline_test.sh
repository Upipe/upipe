#!/bin/sh

srcdir="$1"
DIR="`mktemp -d tmp.XXXXXXXXXX`"
./udict_inline_test > "$DIR"/logs
RET=$?
if test $RET -ne 0; then
	rm -rf "$DIR"
	exit $RET
fi

cat "$srcdir"/udict_inline_test.txt > "$DIR"/ref
cat "$srcdir"/udict_inline_test.txt >> "$DIR"/ref
cat "$srcdir"/udict_inline_test.txt >> "$DIR"/ref

sed -e "s/^\(debug: dumping udict\) .*$/\1/" < "$DIR"/logs | sed -e  "s/^\(debug: end of attributes for udict\) .*$/\1/" > "$DIR"/logs2
diff -q "$DIR"/logs2 "$DIR"/ref
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
../libtool --mode=execute valgrind -q --leak-check=full ./udict_inline_test > /dev/null 2> "$FILE"
RET=$?
if test -s "$FILE"; then
        cat "$FILE" >&2
        RET=1
fi
rm -f "$FILE"
exit $RET

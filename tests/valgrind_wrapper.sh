#!/bin/sh

UNAME=$(uname)

srcdir="$1"
shift

# Check for shell scripts or leaking Qt
if test "${1%.sh}" != "$1" -o `basename "$1"` = "upipe_qt_html_test"; then
    exec "$1" "$srcdir"
fi

if ! which valgrind >/dev/null 2>&1; then
    echo "#### Please install valgrind for unit tests"
    exit 1
fi

# valgrind suppressions
SUPPRESSIONS="--suppressions=$srcdir/valgrind.supp"
if [ "$UNAME" = "Darwin" ]; then
    SUPPRESSIONS="$SUPPRESSIONS --suppressions=$srcdir/valgrind_osx.supp"
fi

VALGRIND_FLAGS="-q --leak-check=full --track-origins=yes --error-exitcode=1 $SUPPRESSIONS"

if test -z "$DISABLE_VALGRIND"; then
    VALGRIND="valgrind $VALGRIND_FLAGS"
fi

# Run in valgrind, with leak checking enabled
../libtool --mode=execute $VALGRIND "$@"
exit $?

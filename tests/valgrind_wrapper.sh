#!/bin/sh

UNAME=$(uname)

srcdir="$1"
shift

# Check for shell scripts
if test "${1%.sh}" != "$1"; then
    exec "$1" "$srcdir"
fi

if test -z "$DISABLE_VALGRIND"; then
    if ! which valgrind >/dev/null 2>&1; then
        echo "#### Please install valgrind for unit tests"
        exit 1
    fi
fi

# valgrind suppressions
SUPPRESSIONS="--suppressions=$srcdir/valgrind.supp"
if [ "$UNAME" = "Darwin" ]; then
    export DYLD_LIBRARY_PATH="$_DYLD_LIBRARY_PATH"
fi

VALGRIND_FLAGS="-q --leak-check=full --track-origins=yes --error-exitcode=1 $SUPPRESSIONS"

if test -z "$DISABLE_VALGRIND"; then
    VALGRIND="valgrind $VALGRIND_FLAGS"
fi

# Run in valgrind, with leak checking enabled
$VALGRIND "$@"
exit $?

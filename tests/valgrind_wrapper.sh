#!/bin/sh

srcdir="$1"
shift

# Check for shell scripts or leaking Qt
if test "${1%.sh}" != "$1" -o `basename "$1"` = "upipe_qt_html_test"; then
    exec "$1" "$srcdir"
fi

if test -z "$DISABLE_VALGRIND"; then
    if ! which valgrind >/dev/null 2>&1; then
        echo "#### Please install valgrind for unit tests"
        exit 1
    fi

    VALGRIND="valgrind -q \
        --leak-check=full \
        --track-origins=yes \
        --error-exitcode=1 \
        --suppressions=$srcdir/valgrind.supp"
fi

export DYLD_LIBRARY_PATH="$_DYLD_LIBRARY_PATH:$DYLD_LIBRARY_PATH"

# Run in valgrind, with leak checking enabled
$VALGRIND "$@"
exit $?

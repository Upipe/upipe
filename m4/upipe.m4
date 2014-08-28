dnl Custom autoconf macros for upipe

AC_DEFUN([PKG_CHECK_UPIPE], [
        PKG_CHECK_MODULES($1, $2, [
                CFLAGS_save="$CFLAGS"
                CFLAGS="$CFLAGS $$1[]_CFLAGS"
                CPPFLAGS_save="$CPPFLAGS"
                CPPFLAGS="$CPPFLAGS $$1[]_CFLAGS"
                CXXFLAGS_save="$CXXFLAGS"
                CXXFLAGS="$CXXFLAGS $$1[]_CFLAGS"
                AC_CHECK_HEADERS($3,
                        AM_CONDITIONAL(HAVE_$1[], true),
                        AM_CONDITIONAL(HAVE_$1[], false))
                CFLAGS="$CFLAGS_save"
                CPPFLAGS="$CPPFLAGS_save"
                CXXFLAGS="$CXXFLAGS_save"
        ],[
                AM_CONDITIONAL(HAVE_$1[], false)
        ])
        AC_SUBST($1[]_CFLAGS)
        AC_SUBST($1[]_LIBS)
])

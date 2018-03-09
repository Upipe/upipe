lib_LTLIBRARIES = libupipe_ebur128.la

libupipe_ebur128_la_SOURCES = upipe_ebur128.c
libupipe_ebur128_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_ebur128_la_CFLAGS = $(AM_CFLAGS) $(EBUR128_CFLAGS)
libupipe_ebur128_la_LIBADD = $(EBUR128_LIBS)
libupipe_ebur128_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_ebur128.pc

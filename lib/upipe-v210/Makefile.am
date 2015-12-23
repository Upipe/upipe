lib_LTLIBRARIES = libupipe_v210.la

libupipe_v210_la_SOURCES = upipe_v210enc.c
libupipe_v210_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_v210_la_CFLAGS = -Wall $(AVUTIL_CFLAGS)
libupipe_v210_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(AVUTIL_LIBS)
libupipe_v210_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_v210.pc

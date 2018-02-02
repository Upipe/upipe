lib_LTLIBRARIES = libupipe_x265.la

libupipe_x265_la_SOURCES = upipe_x265.c
libupipe_x265_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_x265_la_CFLAGS = $(AM_CFLAGS) $(X265_CFLAGS) $(BITSTREAM_CFLAGS)
libupipe_x265_la_LIBADD = $(X265_LIBS) $(top_builddir)/lib/upipe-framers/libupipe_framers.la
libupipe_x265_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_x265.pc

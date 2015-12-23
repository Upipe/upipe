lib_LTLIBRARIES = libupipe_swscale.la

libupipe_swscale_la_SOURCES = upipe_sws.c upipe_sws_thumbs.c
libupipe_swscale_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_swscale_la_CFLAGS = -Wall $(SWSCALE_CFLAGS)
libupipe_swscale_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(SWSCALE_LIBS)
libupipe_swscale_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_swscale.pc

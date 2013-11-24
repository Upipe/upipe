if HAVE_SWSCALE
lib_LTLIBRARIES = libupipe_sws.la
endif

libupipe_sws_la_SOURCES = upipe_sws.c upipe_sws_thumbs.c
libupipe_sws_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_sws_la_CFLAGS = -Wall $(SWSCALE_CFLAGS)
libupipe_sws_la_LIBADD = $(SWSCALE_LIBS)
libupipe_sws_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_sws.pc

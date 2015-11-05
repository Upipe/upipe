lib_LTLIBRARIES = libupipe_swresample.la

libupipe_swresample_la_SOURCES = upipe_swr.c
libupipe_swresample_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_swresample_la_CFLAGS = -Wall $(SWRESAMPLE_CFLAGS)
libupipe_swresample_la_LIBADD = $(SWRESAMPLE_LIBS)
libupipe_swresample_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_swresample.pc

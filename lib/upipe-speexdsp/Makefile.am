lib_LTLIBRARIES = libupipe_speexdsp.la

libupipe_speexdsp_la_SOURCES = upipe_speexdsp.c
libupipe_speexdsp_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_speexdsp_la_CFLAGS = $(SPEEXDSP_CFLAGS)
libupipe_speexdsp_la_LIBADD = $(SPEEXDSP_LIBS)
libupipe_speexdsp_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_speexdsp.pc

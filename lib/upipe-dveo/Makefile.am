lib_LTLIBRARIES = libupipe_dveo.la

libupipe_dveo_la_SOURCES = upipe_dveo_asi_source.c upipe_dveo_asi_sink.c asi_ioctl.h
libupipe_dveo_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_dveo_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_dveo_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_dveo.pc

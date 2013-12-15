lib_LTLIBRARIES = libupipe_filters.la

libupipe_filters_la_SOURCES = upipe_filter_blend.c
libupipe_filters_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_filters_la_CFLAGS = -Wall
libupipe_filters_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_filters.pc

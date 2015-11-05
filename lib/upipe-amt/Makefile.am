lib_LTLIBRARIES = libupipe_amt.la

libupipe_amt_la_SOURCES = upipe_amt_source.c
libupipe_amt_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_amt_la_CFLAGS = -Wall
libupipe_amt_la_LIBADD = -lamt
libupipe_amt_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_amt.pc

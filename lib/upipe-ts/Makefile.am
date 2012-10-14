if HAVE_BITSTREAM
lib_LTLIBRARIES = libupipe_ts.la
endif

libupipe_ts_la_SOURCES = upipe_ts_sync.c upipe_ts_split.c upipe_ts_decaps.c
libupipe_ts_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_ts_la_CFLAGS = -Wall

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_ts.pc

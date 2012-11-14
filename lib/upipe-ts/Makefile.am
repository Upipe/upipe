if HAVE_BITSTREAM
lib_LTLIBRARIES = libupipe_ts.la
endif

noinst_HEADERS = upipe_ts_psid.h
libupipe_ts_la_SOURCES = upipe_ts_sync.c upipe_ts_split.c upipe_ts_decaps.c upipe_ts_pesd.c upipe_ts_psim.c upipe_ts_psi_split.c upipe_ts_patd.c
libupipe_ts_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_ts_la_CFLAGS = -Wall

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_ts.pc

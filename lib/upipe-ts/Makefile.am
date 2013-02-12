if HAVE_BITSTREAM
lib_LTLIBRARIES = libupipe_ts.la
endif

noinst_HEADERS = upipe_ts_psi_decoder.h
libupipe_ts_la_SOURCES = uprobe_ts_log.c upipe_ts_sync.c upipe_ts_check.c upipe_ts_split.c upipe_ts_decaps.c upipe_ts_pes_decaps.c upipe_ts_psi_merge.c upipe_ts_psi_split.c upipe_ts_pat_decoder.c upipe_ts_pmt_decoder.c upipe_ts_demux.c
libupipe_ts_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_ts_la_CFLAGS = -Wall

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_ts.pc

lib_LTLIBRARIES = libupipe_ts.la

noinst_HEADERS = upipe_ts_psi_decoder.h
libupipe_ts_la_SOURCES = \
	upipe_ts_check.c \
	upipe_ts_decaps.c \
	upipe_ts_eit_decoder.c \
	upipe_ts_nit_decoder.c \
	upipe_ts_pes_decaps.c \
	upipe_ts_pat_decoder.c \
	upipe_ts_pmt_decoder.c \
	upipe_ts_psi_join.c \
	upipe_ts_psi_merge.c \
	upipe_ts_psi_split.c \
	upipe_ts_scte35_decoder.c \
	upipe_ts_scte35_probe.c \
	upipe_ts_sdt_decoder.c \
	upipe_ts_tdt_decoder.c \
	upipe_ts_split.c \
	upipe_ts_sync.c \
	upipe_ts_demux.c \
	upipe_ts_tstd.c \
	upipe_ts_encaps.c \
	upipe_ts_pes_encaps.c \
	upipe_ts_psi_generator.c \
	upipe_ts_si_generator.c \
	upipe_ts_mux.c

libupipe_ts_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_ts_la_CFLAGS = -Wall
libupipe_ts_la_LIBADD = $(top_builddir)/lib/upipe-modules/libupipe_modules.la \
			@LTLIBICONV@
libupipe_ts_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_ts.pc

lib_LTLIBRARIES = libupipe_framers.la

noinst_HEADERS = upipe_framers_common.h \
	upipe_h26x_common.h

libupipe_framers_la_SOURCES = \
	upipe_framers_common.c \
	upipe_h26x_common.c \
	upipe_h264_framer.c \
	upipe_h265_framer.c \
	upipe_mpgv_framer.c \
	upipe_a52_framer.c \
	upipe_opus_framer.c \
	upipe_mpga_framer.c \
	upipe_telx_framer.c \
	upipe_dvbsub_framer.c \
	upipe_s302_framer.c \
	upipe_s337_framer.c \
	upipe_video_trim.c

libupipe_framers_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_framers_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_framers.pc

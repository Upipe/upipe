NULL =
lib_LTLIBRARIES = libupipe_framers.la

libupipe_framers_la_SOURCES = \
	upipe_auto_framer.c \
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
	upipe_s337_decaps.c \
	upipe_s337_framer.c \
	upipe_video_trim.c \
	upipe_id3v2_framer.c \
	$(NULL)

libupipe_framers_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_framers_la_CFLAGS = $(AM_CFLAGS) $(BITSTREAM_CFLAGS)
libupipe_framers_la_LIBADD = $(top_builddir)/lib/upipe-modules/libupipe_modules.la
libupipe_framers_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_framers.pc

lib_LTLIBRARIES = libupipe_filters.la

libupipe_filters_la_SOURCES = \
	upipe_filter_blend.c \
	upipe_filter_decode.c \
	upipe_filter_encode.c \
	upipe_filter_format.c \
	upipe_filter_ebur128.c \
	upipe_audio_max.c \
	upipe_audio_bar.c \
	upipe_audio_graph.c \
	ebur128/ebur128.c \
	ebur128/ebur128.h \
	upipe_zoneplate.c \
	upipe_zoneplate_source.c \
	zoneplate/videotestsrc.c \
	zoneplate/videotestsrc.h

if HAVE_BITSTREAM
libupipe_filters_la_SOURCES += upipe_filter_vanc.c \
    upipe_rtp_feedback.c \
    upipe_rtcp_fb_receiver.c
libupipe_filters_la_CFLAGS = $(AM_CFLAGS) $(BITSTREAM_CFLAGS)
endif

libupipe_filters_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_filters_la_LIBADD = $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
libupipe_filters_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_filters.pc

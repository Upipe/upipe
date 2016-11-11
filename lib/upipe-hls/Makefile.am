lib_LTLIBRARIES = libupipe_hls.la

libupipe_hls_la_SOURCES = \
    upipe_hls_master.c \
    upipe_hls_variant.c \
    upipe_hls.c \
    upipe_hls_buffer.c \
    upipe_hls_audio.c \
    upipe_hls_void.c \
    upipe_hls_video.c \
    upipe_hls_playlist.c

libupipe_hls_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_hls_la_CFLAGS = -Wall
libupipe_hls_la_LIBADD = \
	$(top_builddir)/lib/upipe-modules/libupipe_modules.la \
	$(top_builddir)/lib/upipe-ts/libupipe_ts.la \
	$(top_builddir)/lib/upipe-framers/libupipe_framers.la

libupipe_hls_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_hls.pc

lib_LTLIBRARIES = libupipe_srt.la

libupipe_srt_la_SOURCES = upipe_srt_handshake.c \
	upipe_srt_sender.c \
	upipe_srt_receiver.c

libupipe_srt_la_CPPFLAGS = -I$(top_builddir) -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_srt_la_CFLAGS = $(AM_CFLAGS) $(SRT_FLAGS) $(BITSTREAM_CFLAGS)
libupipe_srt_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(SRT_LIBS)
libupipe_srt_la_LDFLAGS = -no-undefined

if HAVE_GCRYPT
libupipe_srt_la_CPPFLAGS += $(GCRYPT_CFLAGS)
libupipe_srt_la_LIBADD += $(GCRYPT_LIBS)
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_srt.pc

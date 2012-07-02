if HAVE_AVFORMAT
lib_LTLIBRARIES = libupipe_av.la
endif

libupipe_av_la_SOURCES = upipe_av.c upipe_av_internal.h upipe_av_codecs.c upipe_av_codecs.pl upipe_avformat_source.c
libupipe_av_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_av_la_CFLAGS = -Wall @AVFORMAT_CFLAGS@
libupipe_av_la_LIBS = @AVFORMAT_LIBS@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_av.pc

upipe_av_codecs.c:
	$(srcdir)/upipe_av_codecs.pl @AVFORMAT_CFLAGS@ > $@

FORCE:

if HAVE_AVFORMAT
lib_LTLIBRARIES = libupipe_av.la
BUILT_SOURCES = upipe_av_codecs.h
endif

nodist_libupipe_av_la_SOURCES = upipe_av_codecs.h
CLEANFILES = upipe_av_codecs.h
libupipe_av_la_SOURCES = upipe_av.c upipe_av_internal.h upipe_av_codecs.c upipe_avformat_sink.c upipe_avformat_source.c upipe_avcodec_decode.c upipe_avcodec_encode.c upipe_av_codecs.pl avcodec_include.h
libupipe_av_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include -I$(builddir)
libupipe_av_la_CFLAGS = -Wall @AVFORMAT_CFLAGS@
libupipe_av_la_LIBADD = @AVFORMAT_LIBS@
libupipe_av_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_av.pc

upipe_av_codecs.h: upipe_av_codecs.pl avcodec_include.h
	CPP="$(CPP)" CFLAGS="$(CFLAGS) $(CPPFLAGS) @AVFORMAT_CFLAGS@" $(PERL) $(srcdir)/upipe_av_codecs.pl $(srcdir)/avcodec_include.h > $@

FORCE:

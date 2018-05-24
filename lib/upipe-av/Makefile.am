NULL =
lib_LTLIBRARIES = libupipe_av.la
BUILT_SOURCES = upipe_av_codecs.h

nodist_libupipe_av_la_SOURCES = upipe_av_codecs.h
CLEANFILES = upipe_av_codecs.h
libupipe_av_la_SOURCES = \
	upipe_av.c \
	upipe_av_internal.h \
	upipe_av_codecs.c \
	upipe_avformat_sink.c \
	upipe_avformat_source.c \
	upipe_avcodec_encode.c \
	upipe_av_codecs.pl \
	avcodec_include.h \
	ubuf_av.c \
	$(NULL)

libupipe_av_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include -I$(builddir)
libupipe_av_la_CFLAGS = $(AM_CFLAGS) $(AVFORMAT_CFLAGS)
libupipe_av_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la \
			$(top_builddir)/lib/upipe-modules/libupipe_modules.la \
			$(AVFORMAT_LIBS)
libupipe_av_la_LDFLAGS = -no-undefined

if HAVE_BITSTREAM
libupipe_av_la_SOURCES += upipe_avcodec_decode.c
libupipe_av_la_CFLAGS += $(BITSTREAM_CFLAGS)
endif

if HAVE_AVFILTER
libupipe_av_la_SOURCES += upipe_avfilter.c
libupipe_av_la_CFLAGS += $(AVFILTER_CFLAGS)
libupipe_av_la_LIBADD += $(AVFILTER_LIBS)
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_av.pc

@AMDEP_TRUE@include ./$(DEPDIR)/upipe_av_codecs_h.Plo

@AMDEP_TRUE@$(DEPDIR)/upipe_av_codecs_h.Plo:
@am__fastdepCC_TRUE@	$(AM_V_at)$(MKDIR_P) $(DEPDIR)
@am__fastdepCC_TRUE@	@echo '# dummy' >$@-t && $(am__mv) $@-t $@

upipe_av_codecs.h: upipe_av_codecs.pl avcodec_include.h
@am__fastdepCC_TRUE@	$(AM_V_CC) $(CPP) $(CFLAGS) $(CPPFLAGS) @AVFORMAT_CFLAGS@ -M $(srcdir)/avcodec_include.h -MT $@ -o $@.Tpo
@am__fastdepCC_TRUE@	$(AM_V_at)$(MKDIR_P) $(DEPDIR)
@am__fastdepCC_TRUE@	$(AM_V_at)$(am__mv) $@.Tpo $(DEPDIR)/upipe_av_codecs_h.Plo
	$(AM_V_GEN) \
	  CPP="$(CPP)" \
	  CFLAGS="$(CFLAGS) $(CPPFLAGS) @AVFORMAT_CFLAGS@" \
	  $(PERL) $(srcdir)/upipe_av_codecs.pl $(srcdir)/avcodec_include.h > $@

distclean-local:
@am__fastdepCC_TRUE@	-rm -f ./$(DEPDIR)/upipe_av_codecs_h.Plo

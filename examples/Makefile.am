AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
AM_CFLAGS = -Wall
LDADD = $(top_builddir)/lib/upipe/libupipe.la
UPIPEMODULES_LIBS = $(top_builddir)/lib/upipe-modules/libupipe_modules.la
UPUMPEV_LIBS = -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
UPIPEAV_LIBS = $(top_builddir)/lib/upipe-av/libupipe_av.la @AVFORMAT_LIBS@
UPIPESWS_LIBS = $(SWSCALE_LIBS) $(top_builddir)/lib/upipe-swscale/libupipe_sws.la
UPIPEGL_LIBS = $(GLX_LIBS) $(top_builddir)/lib/upipe-gl/libupipe_gl.la
UPIPEFILTERS_LIBS = $(top_builddir)/lib/upipe-filters/libupipe_filters.la
UPIPEFRAMERS_LIBS = $(top_builddir)/lib/upipe-framers/libupipe_framers.la
UPIPETS_LIBS = $(top_builddir)/lib/upipe-ts/libupipe_ts.la
UPIPEX264_LIBS = $(X264_LIBS) $(top_builddir)/lib/upipe-x264/libupipe_x264.la
UPIPEALSA_LIBS = -lasound $(top_builddir)/lib/upipe-alsa/libupipe_alsa.la

noinst_PROGRAMS = 

udpmulticat_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS)
glxplay_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPESWS_LIBS) $(UPIPEAV_LIBS) $(UPIPEGL_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS)
#transcode_video_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEAV_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS) $(UPIPEX264_LIBS)
upipe_duration_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS)
alsaplay_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPEAV_LIBS) $(UPIPEALSA_LIBS)
extract_pic_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPEAV_LIBS) $(UPIPESWS_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPETS_LIBS)

if HAVE_EV
noinst_PROGRAMS += udpmulticat

if HAVE_BITSTREAM
noinst_PROGRAMS += upipe_duration extract_pic
endif

if HAVE_AVFORMAT

if HAVE_BITSTREAM
if HAVE_GLX
noinst_PROGRAMS += glxplay
endif
endif

if HAVE_ALSA
noinst_PROGRAMS += alsaplay
endif

if HAVE_X264
#noinst_PROGRAMS += transcode_video
endif

endif
endif

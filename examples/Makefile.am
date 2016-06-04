AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/upipe/libupipe.la
UPIPEMODULES_LIBS = $(top_builddir)/lib/upipe-modules/libupipe_modules.la
UPIPEPTHREAD_LIBS = $(top_builddir)/lib/upipe-pthread/libupipe_pthread.la
UPUMPEV_LIBS = -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
UPIPEAV_LIBS = $(top_builddir)/lib/upipe-av/libupipe_av.la @AVFORMAT_LIBS@
UPIPESWS_LIBS = $(SWSCALE_LIBS) $(top_builddir)/lib/upipe-swscale/libupipe_swscale.la
UPIPESWR_LIBS = $(SWRESAMPLE_LIBS) $(top_builddir)/lib/upipe-swresample/libupipe_swresample.la
UPIPEGL_LIBS = $(GLX_LIBS) $(top_builddir)/lib/upipe-gl/libupipe_gl.la
UPIPEFILTERS_LIBS = $(top_builddir)/lib/upipe-filters/libupipe_filters.la
UPIPEFRAMERS_LIBS = $(top_builddir)/lib/upipe-framers/libupipe_framers.la
UPIPETS_LIBS = $(top_builddir)/lib/upipe-ts/libupipe_ts.la
UPIPEHLS_LIBS = $(top_builddir)/lib/upipe-hls/libupipe_hls.la
UPIPEX264_LIBS = $(X264_LIBS) $(top_builddir)/lib/upipe-x264/libupipe_x264.la
UPIPEALSA_LIBS = -lasound $(top_builddir)/lib/upipe-alsa/libupipe_alsa.la
UPIPEBMD_LIBS = $(top_builddir)/lib/upipe-blackmagic/libupipe_blackmagic.la -lstdc++

noinst_PROGRAMS = 

udpmulticat_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS)
hls2rtp_LDADD= $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS) $(UPIPEPTHREAD_LIBS) $(UPIPEHLS_LIBS)
hls2rtp_CFLAGS= -fno-strict-aliasing
glxplay_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEPTHREAD_LIBS) $(UPIPESWS_LIBS) $(UPIPEAV_LIBS) $(UPIPEGL_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS) -lpthread
uplay_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEPTHREAD_LIBS) $(UPIPESWS_LIBS) $(UPIPEAV_LIBS) $(UPIPEGL_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS) -lpthread
if HAVE_ALSA
uplay_LDADD += $(UPIPEALSA_LIBS)
endif
transcode_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEAV_LIBS) $(UPIPESWR_LIBS) $(UPIPESWS_LIBS) $(UPIPEFILTERS_LIBS)
upipe_duration_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS)
alsaplay_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPEAV_LIBS) $(UPIPEALSA_LIBS)
extract_pic_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPEAV_LIBS) $(UPIPESWS_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPETS_LIBS)
blackmagic_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEAV_LIBS) $(UPIPESWS_LIBS) $(UPIPEBMD_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPESWR_LIBS)
ts2es_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS)
decrypt_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS)


if HAVE_EV
if HAVE_WRITEV
noinst_PROGRAMS += udpmulticat
noinst_PROGRAMS += decrypt
noinst_PROGRAMS += hls2rtp
endif

if HAVE_BITSTREAM
noinst_PROGRAMS += upipe_duration
noinst_PROGRAMS += ts2es
endif

if HAVE_AVFORMAT
if HAVE_SWSCALE
if HAVE_SWRESAMPLE
noinst_PROGRAMS += transcode

if HAVE_DLFCN_H
noinst_PROGRAMS += blackmagic
endif
endif # swresample
endif # swscale

if HAVE_BITSTREAM
if HAVE_WRITEV
noinst_PROGRAMS += extract_pic
endif

if HAVE_GLX
noinst_PROGRAMS += glxplay
noinst_PROGRAMS += uplay
endif

if HAVE_ALSA
noinst_PROGRAMS += alsaplay
endif
endif # bitstream

endif # avformat
endif # ev

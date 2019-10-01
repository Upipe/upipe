AM_CPPFLAGS = -I$(top_builddir) -I$(top_builddir)/include -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/upipe/libupipe.la
UPIPEMODULES_LIBS = $(top_builddir)/lib/upipe-modules/libupipe_modules.la
UPIPEPTHREAD_LIBS = $(top_builddir)/lib/upipe-pthread/libupipe_pthread.la
UPUMPEV_LIBS = -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
UPIPEAV_LIBS = $(top_builddir)/lib/upipe-av/libupipe_av.la @AVFORMAT_LIBS@ @AVFILTER_LIBS@
UPIPEAV_CFLAGS = $(AVUTIL_CFLAGS)
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
UPIPEOSX_LIBS = $(top_builddir)/lib/upipe-osx/libupipe_osx.la
UPIPEDVBCSA_LIBS = $(top_builddir)/lib/upipe-dvbcsa/libupipe_dvbcsa.la
UPIPEDVB_LIBS = $(top_builddir)/lib/upipe-dvb/libupipe_dvb.la

noinst_PROGRAMS = 

fec_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPETS_LIBS)
rist_rx_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFILTERS_LIBS)
rist_tx_CFLAGS = $(AM_CFLAGS) $(BITSTREAM_CFLAGS)
rist_tx_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFILTERS_LIBS)
udpmulticat_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS)
multicatudp_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEPTHREAD_LIBS) -lpthread
hls2rtp_LDADD= $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS) $(UPIPEHLS_LIBS) $(UPIPEPTHREAD_LIBS) -lpthread
hls2rtp_CFLAGS= -fno-strict-aliasing
glxplay_CFLAGS = $(SWSCALE_CFLAGS)
glxplay_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEPTHREAD_LIBS) $(UPIPESWS_LIBS) $(UPIPEAV_LIBS) $(UPIPEGL_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS) -lpthread
uplay_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEPTHREAD_LIBS) $(UPIPESWS_LIBS) $(UPIPESWR_LIBS) $(UPIPEAV_LIBS) $(UPIPEGL_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS) -lpthread
dvbsrc_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEPTHREAD_LIBS) $(UPIPEDVB_LIBS) -lpthread
if HAVE_ALSA
uplay_LDADD += $(UPIPEALSA_LIBS)
endif
if HAVE_AUDIOTOOLBOX
uplay_LDADD += $(UPIPEOSX_LIBS)
endif
uplay_CFLAGS = $(AM_CFLAGS) $(UPIPEAV_CFLAGS)
transcode_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEAV_LIBS) $(UPIPESWR_LIBS) $(UPIPESWS_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPEFRAMERS_LIBS)
upipe_duration_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS)
alsaplay_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPEAV_LIBS) $(UPIPEALSA_LIBS) $(UPIPESWR_LIBS) $(UPIPEFILTERS_LIBS)
extract_pic_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPEAV_LIBS) $(UPIPESWS_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPETS_LIBS)
blackmagic_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEAV_LIBS) $(UPIPESWS_LIBS) $(UPIPEBMD_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPESWR_LIBS)
ts2es_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS)
decrypt_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS)
mpthree2rtp_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPEAV_LIBS) $(UPIPESWR_LIBS)
ts2mpthreemulticat_CFLAGS = $(AM_CFLAGS) $(BITSTREAM_CFLAGS)
ts2mpthreemulticat_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPETS_LIBS) 
grid_LDADD = $(LDADD) $(UPIPEMODULES_LIBS) $(UPUMPEV_LIBS) $(UPIPETS_LIBS) $(UPIPEFRAMERS_LIBS) $(UPIPEFILTERS_LIBS) $(UPIPEAV_LIBS) $(UPIPEX264_LIBS) $(UPIPEPTHREAD_LIBS) $(UPIPESWS_LIBS) $(UPIPESWR_LIBS)
grid_CFLAGS = $(AM_CFLAGS) $(SWSCALE_CFLAGS) $(UPIPEAV_CFLAGS)
ts_encrypt_LDADD = $(LDADD) $(UPIPEMODULES_LIBS) $(UPUMPEV_LIBS) $(UPIPETS_LIBS) $(UPIPEDVBCSA_LIBS) $(UPIPEPTHREAD_LIBS) -lpthread
ts_encrypt_CFLAGS = $(AM_CFLAGS)

if HAVE_GCRYPT
ts_encrypt_CFLAGS += $(LIBGCRYPT_CFLAGS)
ts_encrypt_LDADD += $(LIBGCRYPT_LIBS)
endif

if HAVE_EV
if HAVE_WRITEV
noinst_PROGRAMS += udpmulticat multicatudp
noinst_PROGRAMS += decrypt
if HAVE_BITSTREAM
noinst_PROGRAMS += hls2rtp fec rist_rx rist_tx
endif
endif

if HAVE_BITSTREAM
noinst_PROGRAMS += upipe_duration
noinst_PROGRAMS += ts2es
noinst_PROGRAMS += ts2mpthreemulticat
if HAVE_AVFORMAT
noinst_PROGRAMS += mpthree2rtp
endif
if HAVE_DVBCSA
noinst_PROGRAMS += ts_encrypt
endif
endif

if HAVE_DVBV5
noinst_PROGRAMS += dvbsrc
endif

if HAVE_AVFORMAT
if HAVE_SWSCALE
if HAVE_SWRESAMPLE
if HAVE_BITSTREAM
if HAVE_AVFILTER
noinst_PROGRAMS += transcode
endif
if HAVE_X264
noinst_PROGRAMS += grid
endif
endif

if HAVE_DLFCN_H
noinst_PROGRAMS += blackmagic
endif
endif # swresample

if HAVE_BITSTREAM
if HAVE_WRITEV
noinst_PROGRAMS += extract_pic
endif

if HAVE_GLX
noinst_PROGRAMS += glxplay
noinst_PROGRAMS += uplay
endif
endif
endif # swscale

if HAVE_BITSTREAM
if HAVE_ALSA
noinst_PROGRAMS += alsaplay
endif
endif # bitstream

endif # avformat
endif # ev

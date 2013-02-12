AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
AM_CFLAGS = -Wall
LDADD = $(top_builddir)/lib/upipe/libupipe.la
UPIPEMODULES_LIBS = $(top_builddir)/lib/upipe-modules/libupipe_modules.la
UPUMPEV_LIBS = -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
UPIPEAV_LIBS = $(top_builddir)/lib/upipe-av/libupipe_av.la @AVFORMAT_LIBS@
UPIPESWS_LIBS = $(SWSCALE_LIBS) $(top_builddir)/lib/upipe-swscale/libupipe_sws.la
UPIPEGL_LIBS = $(GLX_LIBS) $(top_builddir)/lib/upipe-gl/libupipe_gl.la

noinst_PROGRAMS = 

if HAVE_EV
noinst_PROGRAMS += udpmulticat

if HAVE_GLX
noinst_PROGRAMS += glxplay
endif
endif

udpmulticat_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS)
glxplay_LDADD = $(LDADD) $(UPUMPEV_LIBS) $(UPIPEMODULES_LIBS) $(UPIPESWS_LIBS) $(UPIPEAV_LIBS) $(UPIPEGL_LIBS)

lib_LTLIBRARIES = libupipe_x264.la

libupipe_x264_la_SOURCES = upipe_x264.c
libupipe_x264_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_x264_la_CFLAGS = -Wall $(X264_CFLAGS)
libupipe_x264_la_LIBADD = $(X264_LIBS)
libupipe_x264_la_LDFLAGS = -no-undefined

if HAVE_X264_OBE
libupipe_x264_la_CPPFLAGS += -DHAVE_X264_OBE
endif
if HAVE_X264_MPEG2
libupipe_x264_la_CPPFLAGS += -DHAVE_X264_MPEG2
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_x264.pc

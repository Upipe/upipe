if HAVE_X264
lib_LTLIBRARIES = libupipe_x264.la
endif

libupipe_x264_la_SOURCES = upipe_x264.c
libupipe_x264_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_x264_la_CFLAGS = -Wall $(X264_CFLAGS)
libupipe_x264_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(X264_LIBS)

if HAVE_X264_OBE
libupipe_x264_la_CPPFLAGS += -DHAVE_X264_OBE
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_x264.pc

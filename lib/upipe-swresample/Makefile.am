if HAVE_SWRESAMPLE
lib_LTLIBRARIES = libupipe_swr.la
endif

libupipe_swr_la_SOURCES = upipe_swr.c
libupipe_swr_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_swr_la_CFLAGS = -Wall $(SWRESAMPLE_CFLAGS)
libupipe_swr_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(SWRESAMPLE_LIBS)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_swr.pc

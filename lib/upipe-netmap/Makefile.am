lib_LTLIBRARIES = libupipe_netmap.la

libupipe_netmap_la_SOURCES = upipe_netmap_source.c \
    $(NULL)
libupipe_netmap_la_CPPFLAGS = $(BITSTREAM_CFLAGS) -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_netmap_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_netmap_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_netmap.pc

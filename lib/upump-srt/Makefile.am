lib_LTLIBRARIES = libupump_srt.la

libupump_srt_la_SOURCES = upump_srt.c
libupump_srt_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupump_srt_la_CFLAGS = $(AM_CFLAGS) $(SRT_CFLAGS)
libupump_srt_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(SRT_LIBS)
libupump_srt_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupump_srt.pc

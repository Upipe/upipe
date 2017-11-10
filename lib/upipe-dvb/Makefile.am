NULL =
lib_LTLIBRARIES = libupipe_dvb.la

libupipe_dvb_la_SOURCES = upipe_dvbsrc.c

libupipe_dvb_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_dvb_la_LDFLAGS = -no-undefined
libupipe_dvb_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la -ldvbv5

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_dvb.pc

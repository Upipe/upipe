lib_LTLIBRARIES = libupipe_alsa.la

libupipe_alsa_la_SOURCES = upipe_alsa_sink.c upipe_alsa_source.c
libupipe_alsa_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_alsa_la_CFLAGS = -Wall
libupipe_alsa_la_LIBADD = -lasound
libupipe_alsa_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_alsa.pc

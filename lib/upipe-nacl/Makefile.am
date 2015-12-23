lib_LTLIBRARIES = libupipe_nacl.la

libupipe_nacl_la_SOURCES = upipe_nacl_graphics2d.c upipe_nacl_audio.c
libupipe_nacl_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_nacl_la_CFLAGS = -Wall
libupipe_nacl_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_nacl.pc

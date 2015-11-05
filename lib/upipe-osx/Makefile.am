lib_LTLIBRARIES = libupipe_osx.la

libupipe_osx_la_SOURCES = 

if HAVE_AUDIOTOOLBOX
libupipe_osx_la_SOURCES += upipe_osx_audioqueue_sink.c
endif
libupipe_osx_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_osx_la_CFLAGS = -Wall 
libupipe_osx_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_osx_la_LDFLAGS = -no-undefined -framework AudioToolbox -framework CoreFoundation

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_osx.pc

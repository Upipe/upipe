if HAVE_BLACKMAGIC
lib_LTLIBRARIES = libupipe_blackmagic.la
endif

libupipe_blackmagic_la_SOURCES = upipe_blackmagic_source.c
libupipe_blackmagic_la_SOURCES += blackmagic_wrap.cpp blackmagic_wrap.h
libupipe_blackmagic_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_blackmagic_la_CFLAGS = -Wall 
libupipe_blackmagic_la_CXXFLAGS = -Wall 
libupipe_blackmagic_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la -lDeckLinkAPI

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_blackmagic.pc

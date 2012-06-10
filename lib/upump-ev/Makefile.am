if HAVE_EV
lib_LTLIBRARIES = libupump_ev.la
endif

libupump_ev_la_SOURCES = upump_ev.c
libupump_ev_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupump_ev_la_CFLAGS = -Wall
libupump_ev_la_LIBS = $(top_builddir)/lib/upipe/libupipe.la -lev

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupump_ev.pc

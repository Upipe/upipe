lib_LTLIBRARIES = libupipe_framers.la

noinst_HEADERS = upipe_framers_common.h
libupipe_framers_la_SOURCES = \
	upipe_framers_common.c \
	upipe_h264_framer.c \
	upipe_mp2v_framer.c
libupipe_framers_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_framers_la_CFLAGS = -Wall

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_framers.pc

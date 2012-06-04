lib_LTLIBRARIES = libupipe.la

libupipe_la_SOURCES = ulog_std.c uprobe_print.c uclock_std.c uref_std.c ubuf_block.c ubuf_pic.c ubuf_sound.c upump_common.c
libupipe_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_la_CFLAGS = -Wall
libupipe_la_LIBADD = -lrt

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe.pc

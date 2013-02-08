lib_LTLIBRARIES = libupipe.la

libupipe_la_SOURCES = uprobe_prefix.c uprobe_stdio.c uprobe_log.c uprobe_upump_mgr.c uprobe_uref_mgr.c uclock_std.c umem_alloc.c umem_pool.c udict_inline.c ubuf_block_common.c ubuf_block_mem.c ubuf_pic_common.c ubuf_pic_mem.c uref_std.c upump_common.c
libupipe_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_la_CFLAGS = -Wall
libupipe_la_LIBADD = @libadd_rt_lib@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe.pc

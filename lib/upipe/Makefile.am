lib_LTLIBRARIES = libupipe.la

libupipe_la_SOURCES = \
	uclock_std.c \
	umem_alloc.c \
	umem_pool.c \
	ubuf_block_mem.c \
	ubuf_mem.c \
	ubuf_mem_common.c \
	ubuf_pic_common.c \
	ubuf_pic.c \
	ubuf_pic_mem.c \
	ubuf_sound_common.c \
	ubuf_sound_mem.c \
	udict_inline.c \
	uref_std.c \
	uref_uri.c \
	upipe_dump.c \
	uprobe.c \
	uprobe_dejitter.c \
	uprobe_loglevel.c \
	uprobe_prefix.c \
	uprobe_select_flows.c \
	uprobe_source_mgr.c \
	uprobe_stdio.c \
	uprobe_stdio_color.c \
	uprobe_syslog.c \
	uprobe_transfer.c \
	uprobe_ubuf_mem.c \
	uprobe_ubuf_mem_pool.c \
	uprobe_uclock.c \
	uprobe_upump_mgr.c \
	uprobe_uref_mgr.c \
	upump_common.c \
	uuri.c \
	ucookie.c \
	ustring.c

libupipe_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_la_LIBADD = @libadd_rt_lib@ -lm
libupipe_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe.pc

lib_LTLIBRARIES = libupipe_pthread.la

libupipe_pthread_la_SOURCES = \
	upipe_pthread_transfer.c \
	uprobe_pthread_upump_mgr.c \
	uprobe_pthread_assert.c

libupipe_pthread_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_pthread_la_CFLAGS = -Wall @PTHREAD_CFLAGS@
libupipe_pthread_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la @PTHREAD_LIBS@
libupipe_pthread_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_pthread.pc

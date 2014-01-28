if HAVE_PTHREAD
lib_LTLIBRARIES = libupipe_pthread.la
endif

libupipe_pthread_la_SOURCES = \
	uprobe_pthread_upump_mgr.c

libupipe_pthread_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_pthread_la_CFLAGS = -Wall @PTHREAD_CFLAGS@
libupipe_pthread_la_LIBADD = @PTHREAD_LIBS@
libupipe_pthread_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_pthread.pc

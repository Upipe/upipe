NULL =
lib_LTLIBRARIES = libupipe_openssl.la

libupipe_openssl_la_SOURCES = \
	https_source_hook_openssl.h \
	https_source_hook_openssl.c \
	uprobe_https_openssl.c \
	$(NULL)

libupipe_openssl_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_openssl_la_CFLAGS = $(OPENSSL_CFLAGS)
libupipe_openssl_la_LIBADD = $(OPENSSL_LIBS) \
			     $(top_builddir)/lib/upipe/libupipe.la \
			     $(top_builddir)/lib/upipe-modules/libupipe_modules.la \
			     $(NULL)
libupipe_openssl_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_openssl.pc

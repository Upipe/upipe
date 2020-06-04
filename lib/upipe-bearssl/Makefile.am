NULL =
lib_LTLIBRARIES = libupipe_bearssl.la

libupipe_bearssl_la_SOURCES = \
	https_source_hook.h \
	https_source_hook.c \
	uprobe_https.c \
	$(NULL)

libupipe_bearssl_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_bearssl_la_LIBADD = -lm -lbearssl \
			     $(top_builddir)/lib/upipe/libupipe.la \
			     $(top_builddir)/lib/upipe-modules/libupipe_modules.la \
			     $(NULL)

libupipe_bearssl_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_bearssl.pc

lib_LTLIBRARIES = libupipe_pciesdi.la

libupipe_pciesdi_la_SOURCES = upipe_pciesdi_source.c \
	upipe_pciesdi_sink.c \
	csr.h \
	sdi.h \
	sdi_config.h \
	flags.h \
	libsdi.h \
	libsdi.c

libupipe_pciesdi_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_pciesdi_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_pciesdi_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_pciesdi.pc

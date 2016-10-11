if HAVE_ZVBI
lib_LTLIBRARIES = libupipe_zvbi.la
endif

libupipe_zvbi_la_SOURCES = upipe_zvbienc.c
libupipe_zvbi_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_zvbi_la_CFLAGS = -Wall $(ZVBI_CFLAGS)
libupipe_zvbi_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(ZVBI_LIBS)
libupipe_zvbi_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_zvbi.pc

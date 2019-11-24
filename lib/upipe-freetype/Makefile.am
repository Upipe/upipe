lib_LTLIBRARIES = libupipe_freetype.la

libupipe_freetype_la_SOURCES = upipe_freetype.c \
			       uprobe_freetype.c
libupipe_freetype_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_freetype_la_CFLAGS = $(FREETYPE_CFLAGS)
libupipe_freetype_la_LIBADD = $(FREETYPE_LIBS) $(top_builddir)/lib/upipe/libupipe.la
libupipe_freetype_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_freetype.pc

if HAVE_ECORE
lib_LTLIBRARIES = libupump_ecore.la
endif

libupump_ecore_la_SOURCES = upump_ecore.c
libupump_ecore_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include @ECORE_CFLAGS@
libupump_ecore_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la @ECORE_LIBS@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupump_ecore.pc

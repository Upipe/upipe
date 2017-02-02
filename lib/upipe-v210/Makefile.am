lib_LTLIBRARIES = libupipe_v210.la

libupipe_v210_la_SOURCES = upipe_v210enc.c
if HAVE_X86_ASM
libupipe_v210_la_SOURCES += v210enc.asm v210enc.h
endif
libupipe_v210_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_v210_la_CFLAGS = $(AM_CFLAGS) $(AVUTIL_CFLAGS)
libupipe_v210_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_v210_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_v210.pc

.asm.lo:
	$(LIBTOOL) --mode=compile --tag=CC $(YASM) $(YASMFLAGS) $< -o $@

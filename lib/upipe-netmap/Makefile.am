lib_LTLIBRARIES = libupipe_netmap.la

libupipe_netmap_la_SOURCES = upipe_netmap_source.c \
    upipe_netmap_sink.c \
    sdi.c \
    sdi.h \
    sdienc.asm \
    ../upipe-hbrmt/sdienc.asm \
    ../upipe-hbrmt/sdidec.asm \
    ../upipe-hbrmt/rfc4175_dec.asm \
    ../upipe-hbrmt/sdidec.c \
    $(NULL)
libupipe_netmap_la_CPPFLAGS = $(AVUTIL_CFLAGS) -I$(top_builddir) -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_netmap_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_netmap_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_netmap.pc

.asm.lo:
	$(LIBTOOL) --mode=compile --tag=CC $(NASM) $(NASMFLAGS) $< -o $@

lib_LTLIBRARIES = libupipe_hbrmt.la

libupipe_hbrmt_la_SOURCES = upipe_pack10bit.c \
    upipe_unpack10bit.c \
    upipe_hbrmt_dec.c \
    upipe_hbrmt_common.h \
    $(NULL)
if HAVE_X86_ASM
libupipe_hbrmt_la_SOURCES += sdidec.asm \
    sdidec.h \
    sdienc.asm \
    sdienc.h
endif
libupipe_hbrmt_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_hbrmt_la_CFLAGS =
libupipe_hbrmt_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_hbrmt_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_hbrmt.pc

.asm.lo:
	$(LIBTOOL) --mode=compile --tag=CC $(YASM) $(YASMFLAGS) $< -o $@

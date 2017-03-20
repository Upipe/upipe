lib_LTLIBRARIES = libupipe_hbrmt.la

libupipe_hbrmt_la_SOURCES = upipe_pack10bit.c \
    upipe_unpack10bit.c \
    upipe_sdi_enc.c \
    upipe_sdi_dec.c \
    upipe_hbrmt_dec.c \
    sdidec.c \
    upipe_unpack_rfc4175.c \
    $(NULL)
if HAVE_X86_ASM
libupipe_hbrmt_la_SOURCES += \
    rfc4175_dec.asm \
    rfc4175_dec.h \
    rfc4175_enc.asm \
    rfc4175_enc.h \
    sdidec.asm \
    sdidec.h \
    sdienc.asm \
    sdienc.h
endif
libupipe_hbrmt_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_hbrmt_la_CFLAGS = $(AM_CFLAGS) $(AVUTIL_CFLAGS)
libupipe_hbrmt_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_hbrmt_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_hbrmt.pc

.asm.lo:
	$(LIBTOOL) --mode=compile --tag=CC $(YASM) $(YASMFLAGS) $< -o $@

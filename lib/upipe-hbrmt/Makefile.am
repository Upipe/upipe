lib_LTLIBRARIES = libupipe_hbrmt.la

libupipe_hbrmt_la_SOURCES = upipe_pack10bit.c \
    upipe_unpack10bit.c \
    upipe_sdi_enc.c \
    upipe_sdi_dec.c \
    upipe_hbrmt_dec.c \
    sdidec.c \
    sdidec.h \
    sdienc.c \
    sdienc.h \
    upipe_unpack_rfc4175.c \
    ../upipe-blackmagic/sdi.c \
    ../upipe-blackmagic/sdi.h \
    rfc4175.c \
    $(NULL)

libupipe_hbrmt_la_CPPFLAGS = -I$(top_builddir) -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_hbrmt_la_CFLAGS = $(AM_CFLAGS) $(AVUTIL_CFLAGS) $(BITSTREAM_CFLAGS) -I$(top_srcdir)/lib/upipe-blackmagic
libupipe_hbrmt_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_hbrmt_la_LDFLAGS = -no-undefined

if HAVE_X86ASM
libupipe_hbrmt_la_SOURCES += \
    rfc4175_dec.asm \
    rfc4175_dec.h \
    rfc4175_enc.asm \
    rfc4175_enc.h \
    sdidec.asm \
    sdienc.asm
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_hbrmt.pc

V_ASM = $(V_ASM_@AM_V@)
V_ASM_ = $(V_ASM_@AM_DEFAULT_VERBOSITY@)
V_ASM_0 = @echo "  ASM     " $@;

.asm.lo:
	$(V_ASM)$(LIBTOOL) $(AM_V_lt) --mode=compile --tag=CC $(NASM) $(NASMFLAGS) $< -o $@

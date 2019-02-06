lib_LTLIBRARIES = libupipe_pciesdi.la

libupipe_pciesdi_la_SOURCES = upipe_pciesdi_source.c \
	upipe_pciesdi_sink.c \
	levelb.c \
	csr.h \
	sdi.h \
	sdi_config.h \
	flags.h \
	libsdi.h \
	libsdi.c

if HAVE_X86ASM
libupipe_pciesdi_la_SOURCES += levelb.asm
endif

libupipe_pciesdi_la_CPPFLAGS = -I$(top_builddir) -I$(top_builddir)/include -I$(top_srcdir)/include $(BITSTREAM_CFLAGS)
libupipe_pciesdi_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la $(top_builddir)/lib/upipe-hbrmt/libupipe_hbrmt.la
libupipe_pciesdi_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_pciesdi.pc

V_ASM = $(V_ASM_@AM_V@)
V_ASM_ = $(V_ASM_@AM_DEFAULT_VERBOSITY@)
V_ASM_0 = @echo "  ASM     " $@;

.asm.lo:
	$(V_ASM)$(LIBTOOL) $(AM_V_lt) --mode=compile --tag=CC $(NASM) $(NASMFLAGS) $< -o $@

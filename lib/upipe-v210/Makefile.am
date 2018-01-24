lib_LTLIBRARIES = libupipe_v210.la

libupipe_v210_la_SOURCES = upipe_v210dec.c v210dec.c upipe_v210enc.c v210enc.c
libupipe_v210_la_CPPFLAGS = -I$(top_builddir) -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_v210_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_v210_la_LDFLAGS = -no-undefined
if HAVE_X86ASM
libupipe_v210_la_SOURCES += v210dec.asm v210dec.h v210enc.asm v210enc.h
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_v210.pc

V_ASM = $(V_ASM_@AM_V@)
V_ASM_ = $(V_ASM_@AM_DEFAULT_VERBOSITY@)
V_ASM_0 = @echo "  ASM     " $@;

.asm.lo:
	$(V_ASM)$(LIBTOOL) $(AM_V_lt) --mode=compile --tag=CC $(NASM) $(NASMFLAGS) $< -o $@

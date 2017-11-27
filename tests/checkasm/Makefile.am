check_PROGRAMS = checkasm

TESTS = checkasm

checkasm_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/include -I$(top_builddir) -I$(top_builddir)/include $(AVUTIL_CFLAGS)
checkasm_LDADD = $(LDADD) $(AVUTIL_LIBS) \
    $(top_builddir)/lib/upipe-v210/libupipe_v210_la-v210dec.o \
    $(top_builddir)/lib/upipe-v210/libupipe_v210_la-v210enc.o \
    $(top_builddir)/lib/upipe-v210/v210dec.o \
    $(top_builddir)/lib/upipe-v210/v210enc.o

checkasm_SOURCES = checkasm.c checkasm.h timer.h \
    uyvy_input.c \
    v210dec.c \
    v210enc.c

if HAVE_BITSTREAM
checkasm_LDADD += \
    $(top_builddir)/lib/upipe-hbrmt/libupipe_hbrmt_la-rfc4175.o \
    $(top_builddir)/lib/upipe-hbrmt/libupipe_hbrmt_la-sdidec.o \
    $(top_builddir)/lib/upipe-hbrmt/libupipe_hbrmt_la-sdienc.o \
    $(top_builddir)/lib/upipe-hbrmt/rfc4175_dec.o \
    $(top_builddir)/lib/upipe-hbrmt/rfc4175_enc.o \
    $(top_builddir)/lib/upipe-hbrmt/sdidec.o \
    $(top_builddir)/lib/upipe-hbrmt/sdienc.o

checkasm_SOURCES += sdidec.c
checkasm_CPPFLAGS += -DHAVE_SDI
endif

if HAVE_X86ASM
checkasm_SOURCES += checkasm_x86.asm timer_x86.h
endif

V_ASM = $(V_ASM_@AM_V@)
V_ASM_ = $(V_ASM_@AM_DEFAULT_VERBOSITY@)
V_ASM_0 = @echo "  ASM     " $@;

# NOTE: the extension is .o
.asm.o:
	$(V_ASM)$(LIBTOOL) $(AM_V_lt) --mode=compile --tag=CC $(NASM) $(NASMFLAGS) $< -o $@

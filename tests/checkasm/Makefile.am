check_PROGRAMS = checkasm

TESTS = checkasm

checkasm_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/include -I$(top_builddir) -I$(top_builddir)/include $(AVUTIL_CFLAGS)
checkasm_LDADD = $(LDADD) $(AVUTIL_LIBS) \
    $(top_builddir)/lib/upipe-v210/libupipe_v210.la

checkasm_SOURCES = checkasm.c checkasm.h timer.h \
    v210enc.c

if HAVE_X86ASM
checkasm_SOURCES += checkasm_x86.asm timer_x86.h
endif

V_ASM = $(V_ASM_@AM_V@)
V_ASM_ = $(V_ASM_@AM_DEFAULT_VERBOSITY@)
V_ASM_0 = @echo "  ASM     " $@;

# NOTE: the extension is .o
.asm.o:
	$(V_ASM)$(LIBTOOL) $(AM_V_lt) --mode=compile --tag=CC $(NASM) $(NASMFLAGS) $< -o $@

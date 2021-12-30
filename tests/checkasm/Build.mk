tests = checkasm
checkasm-deps = x86asm

checkasm-src = \
    checkasm.c \
    checkasm.h \
    planar10_input.c \
    planar8_input.c \
    sdi_input.c \
    timer.h \
    uyvy_input.c \
    v210_input.c

checkasm-src += \
    $(if $(have_x86asm),checkasm_x86.asm timer_x86.h)

checkasm-nasmflags = $(if $(have_pic),-DPIC)
checkasm-cppflags = -I$(top_srcdir) -I$(top_builddir)
checkasm-libs = libavutil

$(builddir)/checkasm: \
    $(top_builddir)/lib/upipe-v210/v210enc.o \
    $(top_builddir)/lib/upipe-v210/v210dec.o \
    $(top_builddir)/lib/upipe-v210/x86/v210enc.o \
    $(top_builddir)/lib/upipe-v210/x86/v210dec.o \
    $(top_builddir)/lib/upipe-hbrmt/sdienc.o \
    $(top_builddir)/lib/upipe-hbrmt/sdidec.o \
    $(top_builddir)/lib/upipe-hbrmt/x86/sdienc.o \
    $(top_builddir)/lib/upipe-hbrmt/x86/sdidec.o

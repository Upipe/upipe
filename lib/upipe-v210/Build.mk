lib-targets = libupipe_v210

libupipe_v210-desc = v210 interface module
libupipe_v210-so-version = 1.0.0

libupipe_v210-includes = \
    upipe_v210dec.h \
    upipe_v210enc.h

libupipe_v210-src = \
    upipe_v210dec.c \
    upipe_v210enc.c \
    v210dec.c \
    v210dec.h \
    v210enc.c \
    v210enc.h

libupipe_v210-src += \
    $(if $(have_x86asm),x86/v210dec.asm) \
    $(if $(have_x86asm),x86/v210enc.asm)

libupipe_v210-libs = libupipe

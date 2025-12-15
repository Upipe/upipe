lib-targets = libupipe_hbrmt

libupipe_hbrmt-desc = HBRMT (High Bit Rate Media Transport) modules
libupipe_hbrmt-so-version = 1.0.0

libupipe_hbrmt-includes = \
    upipe_pack10bit.h \
    upipe_unpack10bit.h

libupipe_hbrmt-src = \
    sdidec.c \
    sdidec.h \
    sdienc.c \
    sdienc.h \
    upipe_pack10bit.c \
    upipe_unpack10bit.c

libupipe_hbrmt-src += \
    $(if $(have_x86asm),x86/sdidec.asm) \
    $(if $(have_x86asm),x86/sdienc.asm)

libupipe_hbrmt-libs = libupipe

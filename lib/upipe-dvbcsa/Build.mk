configs += libdvbcsa
libdvbcsa-includes = dvbcsa/dvbcsa.h
libdvbcsa-ldlibs = -ldvbcsa

lib-targets = libupipe_dvbcsa

libupipe_dvbcsa-desc = libdvbcsa interface module

libupipe_dvbcsa-includes = \
    upipe_dvbcsa_common.h \
    upipe_dvbcsa_decrypt.h \
    upipe_dvbcsa_encrypt.h \
    upipe_dvbcsa_split.h

libupipe_dvbcsa-src = \
    common.h \
    upipe_dvbcsa_decrypt.c \
    upipe_dvbcsa_encrypt.c \
    upipe_dvbcsa_split.c

libupipe_dvbcsa-libs = libupipe libupipe_modules libupipe_ts bitstream libdvbcsa
libupipe_dvbcsa-opt-libs = libgcrypt

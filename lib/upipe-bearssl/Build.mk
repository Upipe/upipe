configs += bearssl
bearssl-includes = bearssl.h
bearssl-ldlibs = -lbearssl

lib-targets = libupipe_bearssl

libupipe_bearssl-desc = BearSSL TLS modules
libupipe_bearssl-so-version = 1.0.0

libupipe_bearssl-src = \
    https_source_hook_bearssl.c \
    https_source_hook_bearssl.h \
    uprobe_https_bearssl.c

libupipe_bearssl-includes = uprobe_https_bearssl.h
libupipe_bearssl-libs = libupipe libupipe_modules bearssl
libupipe_bearssl-ldlibs = -lm

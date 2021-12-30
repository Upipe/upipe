lib-targets = libupipe_openssl

libupipe_openssl-desc = OpenSSL TLS modules

libupipe_openssl-src = \
    https_source_hook_openssl.c \
    https_source_hook_openssl.h \
    uprobe_https_openssl.c

libupipe_openssl-includes = uprobe_https_openssl.h
libupipe_openssl-libs = libupipe libupipe_modules openssl

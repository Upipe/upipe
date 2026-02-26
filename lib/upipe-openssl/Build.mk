lib-targets = libupipe_openssl

libupipe_openssl-desc = OpenSSL TLS modules
libupipe_openssl-so-version = 1.0.0

libupipe_openssl-src = \
    uprobe_https_openssl.c

libupipe_openssl-src-private = \
    https_source_hook_openssl.c \
    https_source_hook_openssl.h

libupipe_openssl-includes = uprobe_https_openssl.h
libupipe_openssl-libs = libupipe libupipe_modules openssl

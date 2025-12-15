configs += libev
libev-includes = ev.h
libev-ldlibs = -lev

lib-targets = libupump_ev

libupump_ev-desc = libev event loop wrapper
libupump_ev-so-version = 1.0.0
libupump_ev-includes = upump_ev.h
libupump_ev-src = upump_ev.c
libupump_ev-cflags = -Wno-strict-aliasing
libupump_ev-libs = libupipe libev

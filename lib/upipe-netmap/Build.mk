configs += netmap
netmap-includes = net/netmap.h

lib-targets = libupipe_netmap

libupipe_netmap-desc = netmap interface module
libupipe_netmap-includes = upipe_netmap_source.h
libupipe_netmap-src = upipe_netmap_source.c
libupipe_netmap-deps = netmap
libupipe_netmap-libs = libupipe

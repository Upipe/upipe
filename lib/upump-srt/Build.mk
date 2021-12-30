configs += timerfd
timerfd-includes = sys/timerfd.h
timerfd-functions = timerfd_create

lib-targets = libupump_srt

libupump_srt-desc = libsrt event loop wrapper
libupump_srt-includes = upump_srt.h
libupump_srt-src = upump_srt.c
libupump_srt-deps = timerfd
libupump_srt-libs = libupipe srt

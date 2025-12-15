configs += amt
amt-includes = amt.h
amt-cppflags = -pthread
amt-ldflags = -pthread
amt-ldlibs = -lamt

lib-targets = libupipe_amt

libupipe_amt-desc = Automatic Multicast Tunneling module
libupipe_amt-so-version = 1.0.0
libupipe_amt-includes = upipe_amt_source.h
libupipe_amt-src = upipe_amt_source.c
libupipe_amt-libs = amt

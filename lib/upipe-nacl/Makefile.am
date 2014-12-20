if HAVE_NACL
lib_LTLIBRARIES = libupipe_nacl.la
endif


libupipe_nacl_la_SOURCES = upipe_display.c upipe_sound.c #upipe_src_tcp_chrome.c upipe_src_udp_chrome.c upipe_filter_ebur128.c
libupipe_nacl_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_nacl_la_CFLAGS = -Wall
libupipe_nacl_la_LIBADD = $(top_builddir)/lib/upipe-ebur128/libupipe_ebur128.la
libupipe_nacl_la_LDFLAGS = -no-undefined


pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_nacl.pc

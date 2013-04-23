lib_LTLIBRARIES = libupipe_modules.la

libupipe_modules_la_SOURCES = \
	upipe_file_source.c \
	upipe_file_sink.c \
	upipe_dup.c \
	upipe_queue_source.c \
	upipe_queue_sink.c \
	upipe_udp_source.c \
	upipe_http_source.c \
    http-parser/http_parser.c \
    http-parser/http_parser.h \
	upipe_genaux.c \
	upipe_multicat_sink.c \
	upipe_null.c \
	upipe_skip.c \
	upipe_setattr.c \
	upipe_setrap.c
libupipe_modules_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_modules_la_CFLAGS = -Wall

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_modules.pc

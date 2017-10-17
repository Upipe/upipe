lib_LTLIBRARIES = libupipe_dvbcsa.la

libupipe_dvbcsa_la_SOURCES = upipe_dvbcsa_decrypt.c \
			     upipe_dvbcsa_bs_decrypt.c \
			     upipe_dvbcsa_encrypt.c \
			     upipe_dvbcsa_bs_encrypt.c \
			     upipe_dvbcsa_split.c
libupipe_dvbcsa_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_dvbcsa_la_CFLAGS = $(AM_CFLAGS)
libupipe_dvbcsa_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la
libupipe_dvbcsa_la_LDFLAGS = -no-undefined -ldvbcsa

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_dvbcsa.pc

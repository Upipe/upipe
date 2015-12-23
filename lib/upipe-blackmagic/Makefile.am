lib_LTLIBRARIES = libupipe_blackmagic.la

libupipe_blackmagic_la_SOURCES = ubuf_pic_blackmagic.cpp ubuf_sound_blackmagic.cpp upipe_blackmagic_source.cpp
EXTRA_libupipe_blackmagic_la_SOURCES = include/DeckLinkAPIConfiguration.h include/DeckLinkAPIDeckControl.h include/DeckLinkAPIDiscovery.h include/DeckLinkAPIDispatch.cpp include/DeckLinkAPI.h include/DeckLinkAPIModes.h include/DeckLinkAPITypes.h include/DeckLinkAPIVersion.h include/LinuxCOM.h
libupipe_blackmagic_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_blackmagic_la_CFLAGS = -Wall 
libupipe_blackmagic_la_CXXFLAGS = -Wall 
libupipe_blackmagic_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la -ldl

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_blackmagic.pc

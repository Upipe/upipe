NULL =
lib_LTLIBRARIES = libupipe_blackmagic.la

libupipe_blackmagic_la_SOURCES = ubuf_pic_blackmagic.cpp \
                                 ubuf_sound_blackmagic.cpp \
                                 upipe_blackmagic_source.cpp \
                                 upipe_blackmagic_extract_vanc.cpp \
                                 include/DeckLinkAPIDispatch.cpp \
                                 $(NULL)

if HAVE_BITSTREAM
libupipe_blackmagic_la_SOURCES += upipe_blackmagic_sink.cpp \
                                  sdi.c
endif

EXTRA_libupipe_blackmagic_la_SOURCES = include/DeckLinkAPIConfiguration.h \
                                       include/DeckLinkAPIDeckControl.h \
                                       include/DeckLinkAPIDiscovery.h \
                                       include/DeckLinkAPI.h \
                                       include/DeckLinkAPIModes.h \
                                       include/DeckLinkAPITypes.h \
                                       include/DeckLinkAPIVersion.h \
                                       include/LinuxCOM.h \
                                       sdi.h


libupipe_blackmagic_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_blackmagic_la_CXXFLAGS = $(AM_CXXFLAGS) -Wno-missing-declarations
libupipe_blackmagic_la_LIBADD = $(top_builddir)/lib/upipe/libupipe.la -ldl -lpthread
if HAVE_ZVBI
libupipe_blackmagic_la_CPPFLAGS += $(ZVBI_CFLAGS)
libupipe_blackmagic_la_LIBADD += $(ZVBI_LIBS)
endif
if HAVE_BITSTREAM
libupipe_blackmagic_la_CPPFLAGS += $(BITSTREAM_CFLAGS)
endif

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_blackmagic.pc

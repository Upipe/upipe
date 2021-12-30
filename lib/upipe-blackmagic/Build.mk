lib-targets = libupipe_blackmagic

libupipe_blackmagic-desc = Blackmagic Decklink modules

libupipe_blackmagic-includes = \
    ubuf_pic_blackmagic.h \
    ubuf_sound_blackmagic.h \
    upipe_blackmagic_extract_vanc.h \
    upipe_blackmagic_source.h

libupipe_blackmagic-src = \
    include/DeckLinkAPI.h \
    include/DeckLinkAPIConfiguration.h \
    include/DeckLinkAPIDeckControl.h \
    include/DeckLinkAPIDiscovery.h \
    include/DeckLinkAPIDispatch.cpp \
    include/DeckLinkAPIModes.h \
    include/DeckLinkAPITypes.h \
    include/DeckLinkAPIVersion.h \
    include/LinuxCOM.h \
    ubuf_pic_blackmagic.cpp \
    ubuf_sound_blackmagic.cpp \
    upipe_blackmagic_extract_vanc.cpp \
    upipe_blackmagic_source.cpp

have_upipe_bmd_sink = $(have_bitstream)

libupipe_blackmagic-includes += \
    $(if $(have_upipe_bmd_sink),upipe_blackmagic_sink.h)

libupipe_blackmagic-src += \
    $(if $(have_upipe_bmd_sink),sdi.c sdi.h upipe_blackmagic_sink.cpp)

libupipe_blackmagic-cxxflags = \
    -Wno-missing-declarations \
    $(call try_cxx,-Wno-missing-prototypes)

libupipe_blackmagic-libs = libupipe pthread
libupipe_blackmagic-ldlibs = -ldl
libupipe_blackmagic-opt-libs = bitstream zvbi-0.2

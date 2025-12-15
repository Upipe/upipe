lib-targets = libupipe_filters

libupipe_filters-desc = filters
libupipe_filters-so-version = 1.0.0

libupipe_filters-includes = \
    upipe_audio_bar.h \
    upipe_audio_graph.h \
    upipe_audio_max.h \
    upipe_filter_blend.h \
    upipe_filter_decode.h \
    upipe_filter_encode.h \
    upipe_filter_format.h \
    upipe_opus_encaps.h \
    upipe_zoneplate.h \
    upipe_zoneplate_source.h \
    uref_opus_flow.h

libupipe_filters-src = \
    upipe_audio_bar.c \
    upipe_audio_graph.c \
    upipe_audio_max.c \
    upipe_filter_blend.c \
    upipe_filter_decode.c \
    upipe_filter_encode.c \
    upipe_filter_format.c \
    upipe_opus_encaps.c \
    upipe_zoneplate.c \
    upipe_zoneplate_source.c \
    zoneplate/videotestsrc.c \
    zoneplate/videotestsrc.h

have_upipe_vanc    = $(have_bitstream)
have_upipe_rtcpfb  = $(have_bitstream)
have_upipe_rtpfb   = $(have_bitstream)

libupipe_filters-includes += \
    $(if $(have_upipe_vanc),upipe_filter_vanc.h) \
    $(if $(have_upipe_rtcpfb),upipe_rtcp_fb_receiver.h) \
    $(if $(have_upipe_rtpfb),upipe_rtp_feedback.h)

libupipe_filters-src += \
    $(if $(have_upipe_vanc),upipe_filter_vanc.c) \
    $(if $(have_upipe_rtcpfb),upipe_rtcp_fb_receiver.c) \
    $(if $(have_upipe_rtpfb),upipe_rtp_feedback.c)

libupipe_filters-ldlibs = -lm
libupipe_filters-libs = libupipe libupipe_modules
libupipe_filters-opt-libs = bitstream

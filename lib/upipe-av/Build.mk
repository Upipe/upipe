lib-targets = libupipe_av

libupipe_av-desc = FFmpeg modules
libupipe_av-so-version = 1.0.0

libupipe_av-includes = \
    ubuf_av.h \
    upipe_av.h \
    upipe_av_pixfmt.h \
    upipe_av_samplefmt.h \
    upipe_avformat_sink.h \
    upipe_avformat_source.h \
    uref_av_flow.h

libupipe_av-src = \
    ubuf_av.c \
    upipe_av.c \
    upipe_av_codecs.c \
    upipe_av_internal.h \
    upipe_avformat_sink.c \
    upipe_avformat_source.c

have_upipe_avcdec = $(have_bitstream)
have_upipe_avcenc = $(have_bitstream)
have_upipe_avfilt = $(have_libavfilter)

libupipe_av-includes += \
    $(if $(have_upipe_avcdec),upipe_avcodec_decode.h) \
    $(if $(have_upipe_avcenc),upipe_avcodec_encode.h) \
    $(if $(have_upipe_avfilt),upipe_avfilter.h uref_avfilter_flow.h)

libupipe_av-src += \
    $(if $(have_upipe_avcdec),upipe_avcodec_decode.c) \
    $(if $(have_upipe_avcenc),upipe_avcodec_encode.c) \
    $(if $(have_upipe_avfilt),upipe_avfilter.c)

libupipe_av-libs = libupipe libupipe_modules libavformat libavcodec libavutil
libupipe_av-opt-libs = bitstream libavfilter

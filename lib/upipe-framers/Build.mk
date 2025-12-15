lib-targets = libupipe_framers

libupipe_framers-desc = framers
libupipe_framers-so-version = 1.0.0

libupipe_framers-includes = \
    upipe_a52_framer.h \
    upipe_auto_framer.h \
    upipe_dvbsub_framer.h \
    upipe_framers_common.h \
    upipe_h264_framer.h \
    upipe_h265_framer.h \
    upipe_h26x_common.h \
    upipe_id3v2_framer.h \
    upipe_mpga_framer.h \
    upipe_mpgv_framer.h \
    upipe_opus_framer.h \
    upipe_s302_framer.h \
    upipe_s337_decaps.h \
    upipe_s337_framer.h \
    upipe_telx_framer.h \
    upipe_video_trim.h \
    uref_dvbsub_flow.h \
    uref_h264.h \
    uref_h264_flow.h \
    uref_h265.h \
    uref_h265_flow.h \
    uref_h26x.h \
    uref_h26x_flow.h \
    uref_mpga_flow.h \
    uref_mpgv.h \
    uref_mpgv_flow.h

libupipe_framers-src = \
    upipe_a52_framer.c \
    upipe_auto_framer.c \
    upipe_dvbsub_framer.c \
    upipe_framers_common.c \
    upipe_h264_framer.c \
    upipe_h265_framer.c \
    upipe_h26x_common.c \
    upipe_id3v2_framer.c \
    upipe_mpga_framer.c \
    upipe_mpgv_framer.c \
    upipe_opus_framer.c \
    upipe_s302_framer.c \
    upipe_s337_decaps.c \
    upipe_s337_framer.c \
    upipe_telx_framer.c \
    upipe_video_trim.c

libupipe_framers-libs = \
    libupipe \
    libupipe_modules \
    bitstream

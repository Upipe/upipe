lib-targets = libupipe_x264

libupipe_x264-desc = x264 interface module
libupipe_x264-includes = upipe_x264.h
libupipe_x264-src = upipe_x264.c
libupipe_x264-libs = libupipe libupipe_framers x264 bitstream

configs += x264-obe
x264-obe-functions = x264_speedcontrol_sync
x264-obe-libs = x264

configs += x264-mpeg2
x264-mpeg2-functions = x264_param_default_mpeg2
x264-mpeg2-libs = x264

libupipe_x264-cppflags = \
    $(if $(have_x264-obe),-DHAVE_X264_OBE) \
    $(if $(have_x264-mpeg2),-DHAVE_X264_MPEG2)

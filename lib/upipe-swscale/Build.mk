lib-targets = libupipe_swscale

libupipe_swscale-desc = swscale interface module
libupipe_swscale-so-version = 1.0.0
libupipe_swscale-includes = upipe_sws.h upipe_sws_thumbs.h
libupipe_swscale-src = upipe_sws.c upipe_sws_thumbs.c
libupipe_swscale-libs = libupipe libswscale libavutil

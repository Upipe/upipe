lib-targets = libupipe_hls

libupipe_hls-desc = HLS modules
libupipe_hls-so-version = 1.0.0

libupipe_hls-includes = \
    upipe_hls.h \
    upipe_hls_audio.h \
    upipe_hls_buffer.h \
    upipe_hls_master.h \
    upipe_hls_playlist.h \
    upipe_hls_variant.h \
    upipe_hls_video.h \
    upipe_hls_void.h \
    uref_hls.h

libupipe_hls-src = \
    upipe_hls.c \
    upipe_hls_audio.c \
    upipe_hls_buffer.c \
    upipe_hls_master.c \
    upipe_hls_playlist.c \
    upipe_hls_variant.c \
    upipe_hls_video.c \
    upipe_hls_void.c

libupipe_hls-libs = \
    libupipe \
    libupipe_modules \
    libupipe_ts \
    libupipe_framers \
    bitstream

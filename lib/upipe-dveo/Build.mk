lib-targets = libupipe_dveo

libupipe_dveo-desc = DVEO modules
libupipe_dveo-so-version = 1.0.0
libupipe_dveo-includes = upipe_dveo_asi_sink.h upipe_dveo_asi_source.h
libupipe_dveo-src = asi_ioctl.h upipe_dveo_asi_sink.c upipe_dveo_asi_source.c
libupipe_dveo-libs = libupipe

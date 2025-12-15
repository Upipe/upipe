configs += nacl
nacl-includes = ppapi/c/ppb.h

lib-targets = libupipe_nacl

libupipe_nacl-desc = NaCl interface modules
libupipe_nacl-so-version = 1.0.0
libupipe_nacl-includes = upipe_nacl_audio.h upipe_nacl_graphics2d.h
libupipe_nacl-src = upipe_nacl_audio.c upipe_nacl_graphics2d.c
libupipe_nacl-deps = nacl

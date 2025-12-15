configs += audiotoolbox
audiotoolbox-includes = AudioToolbox/AudioToolbox.h
audiotoolbox-ldlibs = -framework AudioToolbox

lib-targets = libupipe_osx

libupipe_osx-desc = osx-specific module
libupipe_osx-so-version = 1.0.0
libupipe_osx-includes = upipe_osx_audioqueue_sink.h
libupipe_osx-src = upipe_osx_audioqueue_sink.c
libupipe_osx-libs = libupipe audiotoolbox
libupipe_osx-ldlibs = -framework CoreFoundation

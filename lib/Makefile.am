SUBDIRS = \
	upipe \
	upipe-modules \
	upipe-filters \
	upipe-dveo

if HAVE_QTWEBKIT
SUBDIRS += upipe-qt
endif

if HAVE_OSX_DARWIN
SUBDIRS += upipe-osx
endif

if HAVE_ALSA
SUBDIRS += upipe-alsa
endif

if HAVE_AMT
SUBDIRS += upipe-amt
endif

if HAVE_NACL
SUBDIRS += upipe-nacl
endif

if HAVE_GLX
SUBDIRS += upipe-gl
endif

if HAVE_SWSCALE
SUBDIRS += upipe-swscale
endif

if HAVE_SWRESAMPLE
SUBDIRS += upipe-swresample
endif

if HAVE_SPEEXDSP
SUBDIRS += upipe-speexdsp
endif

if HAVE_DLFCN_H
SUBDIRS += upipe-blackmagic
endif

if HAVE_AVUTIL
SUBDIRS += upipe-v210
endif

if HAVE_BITSTREAM
SUBDIRS += upipe-ts upipe-framers upipe-hls
if HAVE_X264
SUBDIRS += upipe-x264
endif
endif

if HAVE_AVFORMAT
SUBDIRS += upipe-av
endif

if HAVE_PTHREAD
SUBDIRS += upipe-pthread
endif

if HAVE_EV
SUBDIRS += upump-ev
endif

if HAVE_ECORE
SUBDIRS += upump-ecore
endif

if HAVE_ZVBI
SUBDIRS += upipe-zvbi
endif

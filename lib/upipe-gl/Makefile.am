lib_LTLIBRARIES = libupipe_gl.la

libupipe_gl_la_SOURCES = \
    upipe_gl_sink_common.c \
    upipe_glx_sink.c \
    uprobe_gl_sink_cube.c
libupipe_gl_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_gl_la_CFLAGS = -Wall $(GLX_CFLAGS)
libupipe_gl_la_LIBADD = $(GLX_LIBS)
libupipe_gl_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_gl.pc

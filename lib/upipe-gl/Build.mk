lib-targets = libupipe_gl

libupipe_gl-desc = OpenGL module

libupipe_gl-includes = \
    upipe_gl_sink_common.h \
    upipe_glx_sink.h \
    uprobe_gl_sink.h \
    uprobe_gl_sink_cube.h

libupipe_gl-src = \
    upipe_gl_sink_common.c \
    upipe_glx_sink.c \
    uprobe_gl_sink.c \
    uprobe_gl_sink_cube.c

libupipe_gl-libs = gl glu x11

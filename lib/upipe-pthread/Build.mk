lib-targets = libupipe_pthread

libupipe_pthread-desc = pthread modules

libupipe_pthread-includes = \
    umutex_pthread.h \
    upipe_pthread_transfer.h \
    uprobe_pthread_assert.h \
    uprobe_pthread_upump_mgr.h

libupipe_pthread-src = \
    umutex_pthread.c \
    upipe_pthread_transfer.c \
    uprobe_pthread_assert.c \
    uprobe_pthread_upump_mgr.c

libupipe_pthread-libs = libupipe libupipe_modules pthread

LOG_COMPILER = $(srcdir)/valgrind_wrapper.sh
AM_LOG_FLAGS = $(srcdir)

dist_check_SCRIPTS = \
	ulog_std_test.sh \
	uprobe_print_test.sh \
	upipe_file_test.sh \
	valgrind_wrapper.sh

dist_check_DATA = ulog_std_test.txt uprobe_print_test.txt

check_PROGRAMS = \
	ulog_std_test \
	uprobe_print_test \
	ubuf_block_test \
	ubuf_pic_test \
	ubuf_sound_test \
	uref_std_test \
	uref_attr_test \
	uref_block_test \
	uref_pic_test \
	uref_sound_test \
	uclock_std_test

TESTS = \
	ubuf_block_test \
	ubuf_pic_test \
	ubuf_sound_test \
	uref_std_test \
	uref_attr_test \
	uref_block_test \
	uref_pic_test \
	uref_sound_test \
	ulog_std_test.sh \
	uprobe_print_test.sh \
	uclock_std_test

if HAVE_EV
check_PROGRAMS += \
	upump_ev_test \
	upool_uqueue_test \
	upipe_file_test \
	upipe_dup_test \
	upipe_queue_test

TESTS += \
	upump_ev_test \
	upool_uqueue_test \
	upipe_file_test.sh \
	upipe_dup_test \
	upipe_queue_test
endif

AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
AM_CFLAGS = -Wall
LDADD = $(top_builddir)/lib/upipe/libupipe.la

upump_ev_test_LDADD = $(top_builddir)/lib/upipe/libupipe.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
upool_uqueue_test_CFLAGS = -pthread
upool_uqueue_test_LDADD = $(top_builddir)/lib/upipe/libupipe.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
upipe_file_test_LDADD = $(top_builddir)/lib/upipe/libupipe.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_dup_test_LDADD = $(top_builddir)/lib/upipe/libupipe.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_queue_test_LDADD = $(top_builddir)/lib/upipe/libupipe.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la

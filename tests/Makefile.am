LOG_COMPILER = $(srcdir)/valgrind_wrapper.sh
AM_LOG_FLAGS = $(srcdir)

dist_check_SCRIPTS = \
	ulog_stdio_test.sh \
	ulog_sub_test.sh \
	udict_inline_test.sh \
	uprobe_log_test.sh \
	upipe_file_test.sh \
	upipe_multicat_test.sh \
	uprobe_ts_log_test.sh \
	valgrind_wrapper.sh

dist_check_DATA = \
	ulog_stdio_test.txt \
	ulog_sub_test.txt \
	uprobe_log_test.txt \
	uprobe_ts_log_test.txt

check_PROGRAMS = \
	ulog_stdio_test \
	ulog_sub_test \
	uprobe_log_test \
	uprobe_uref_mgr_test \
	umem_alloc_test \
	umem_pool_test \
	udict_inline_test \
	ubuf_block_mem_test \
	ubuf_pic_mem_test \
	uref_std_test \
	uclock_std_test \
	upipe_genaux_test \
	upipe_dup_test

TESTS = \
	umem_alloc_test \
	umem_pool_test \
	udict_inline_test.sh \
	ubuf_block_mem_test \
	ubuf_pic_mem_test \
	uref_std_test \
	ulog_stdio_test.sh \
	ulog_sub_test.sh \
	uprobe_log_test.sh \
	uprobe_uref_mgr_test \
	uclock_std_test \
	upipe_genaux_test \
	upipe_dup_test

if HAVE_EV
check_PROGRAMS += \
	upump_ev_test \
	ulifo_uqueue_test \
	udeal_test \
	uprobe_upump_mgr_test \
	upipe_file_test \
	upipe_queue_test \
	upipe_multicat_test \
	upipe_udp_test

TESTS += \
	upump_ev_test \
	ulifo_uqueue_test \
	udeal_test \
	uprobe_upump_mgr_test \
	upipe_file_test.sh \
	upipe_queue_test \
	upipe_multicat_test.sh \
	upipe_udp_test
endif

if HAVE_AVFORMAT
check_PROGRAMS += \
	upipe_avformat_test \
	upipe_avcodec_test
endif

if HAVE_SWSCALE
check_PROGRAMS += \
    upipe_sws_test
#TESTS += \
#    upipe_sws_test
endif

if HAVE_BITSTREAM
check_PROGRAMS += \
	uprobe_ts_log_test \
	upipe_ts_sync_test \
	upipe_ts_check_test \
	upipe_ts_split_test \
	upipe_ts_decaps_test \
	upipe_ts_pesd_test \
	upipe_ts_psim_test \
	upipe_ts_psi_split_test \
	upipe_ts_patd_test \
	upipe_ts_pmtd_test \
	upipe_ts_demux_test
TESTS += \
	uprobe_ts_log_test.sh \
	upipe_ts_sync_test \
	upipe_ts_check_test \
	upipe_ts_split_test \
	upipe_ts_decaps_test \
	upipe_ts_pesd_test \
	upipe_ts_psim_test \
	upipe_ts_psi_split_test \
	upipe_ts_patd_test \
	upipe_ts_pmtd_test
endif

AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
AM_CFLAGS = -Wall
LDADD = $(top_builddir)/lib/upipe/libupipe.la

upump_ev_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
ulifo_uqueue_test_CFLAGS = -pthread
ulifo_uqueue_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
udeal_test_CFLAGS = -pthread
udeal_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
uprobe_upump_mgr_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
upipe_file_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_udp_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_multicat_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_dup_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_genaux_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_queue_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_avformat_test_CFLAGS = @AVFORMAT_CFLAGS@
upipe_avformat_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-av/libupipe_av.la @AVFORMAT_LIBS@
upipe_avcodec_test_CFLAGS = @AVFORMAT_CFLAGS@
upipe_avcodec_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-av/libupipe_av.la @AVFORMAT_LIBS@
upipe_sws_test_CFLAGS = $(SWSCALE_CFLAGS)
upipe_sws_test_LDADD = $(LDADD) $(SWSCALE_LIBS) $(top_builddir)/lib/upipe-swscale/libupipe_sws.la
uprobe_ts_log_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_sync_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_check_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_split_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_decaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pesd_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psim_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psi_split_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_patd_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pmtd_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_demux_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la

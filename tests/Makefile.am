LOG_COMPILER = $(srcdir)/valgrind_wrapper.sh
AM_LOG_FLAGS = $(srcdir)

dist_check_SCRIPTS = \
	uprobe_stdio_test.sh \
	uprobe_prefix_test.sh \
	udict_inline_test.sh \
	uprobe_log_test.sh \
	upipe_file_test.sh \
	upipe_multicat_test.sh \
	uprobe_ts_log_test.sh \
	valgrind_wrapper.sh

dist_check_DATA = \
	valgrind.supp \
	valgrind_osx.supp \
	udict_inline_test.txt \
	uprobe_stdio_test.txt \
	uprobe_prefix_test.txt \
	uprobe_log_test.txt \
	uprobe_ts_log_test.txt

check_PROGRAMS = \
	uprobe_stdio_test \
	uprobe_prefix_test \
	uprobe_log_test \
	uprobe_dejitter_test \
	uprobe_select_flows_test \
	uprobe_uref_mgr_test \
	umem_alloc_test \
	umem_pool_test \
	udict_inline_test \
	ubuf_block_mem_test \
	ubuf_pic_mem_test \
	uref_std_test \
	uclock_std_test \
	upipe_trickplay_test \
	upipe_dup_test \
	upipe_genaux_test \
	upipe_null_test \
	upipe_setrap_test \
	upipe_skip_test \
	upipe_setattr_test \
	upipe_match_attr_test \
	upipe_probe_uref_test \
	upipe_multicat_probe_test \
	upipe_chunk_stream_test \
	upipe_htons_test \
	upipe_filter_blend_test

TESTS = \
	umem_alloc_test \
	umem_pool_test \
	udict_inline_test.sh \
	ubuf_block_mem_test \
	ubuf_pic_mem_test \
	uref_std_test \
	uprobe_stdio_test.sh \
	uprobe_prefix_test.sh \
	uprobe_log_test.sh \
	uprobe_dejitter_test \
	uprobe_select_flows_test \
	uprobe_uref_mgr_test \
	uclock_std_test \
	upipe_trickplay_test \
	upipe_dup_test \
	upipe_genaux_test \
	upipe_null_test \
	upipe_setrap_test \
	upipe_skip_test \
	upipe_setattr_test \
	upipe_match_attr_test \
	upipe_probe_uref_test \
	upipe_multicat_probe_test \
	upipe_chunk_stream_test \
	upipe_htons_test \
	upipe_filter_blend_test

if HAVE_EV
check_PROGRAMS += \
	upump_ev_test \
	ulifo_uqueue_test \
	udeal_test \
	uprobe_upump_mgr_test \
	upipe_file_test \
	upipe_queue_test \
	upipe_multicat_test \
	upipe_http_src_test \
	upipe_udp_test \
	upipe_transfer_test

TESTS += \
	upump_ev_test \
	ulifo_uqueue_test \
	udeal_test \
	uprobe_upump_mgr_test \
	upipe_file_test.sh \
	upipe_queue_test \
	upipe_multicat_test.sh \
	upipe_udp_test \
	upipe_transfer_test

# avcodec/avformat tests currently depend on ev
if HAVE_AVFORMAT
check_PROGRAMS += \
	upipe_avformat_test \
	upipe_avcodec_decode_test \
	upipe_avcodec_test
TESTS += \
	upipe_avcodec_test
endif

if HAVE_GLX
check_PROGRAMS += upipe_glx_sink_test
endif

if HAVE_ALSA
check_PROGRAMS += upipe_alsa_sink_test
endif
endif


if HAVE_SWSCALE
check_PROGRAMS += \
	upipe_sws_test
TESTS += \
	upipe_sws_test
endif

if HAVE_SWRESAMPLE
check_PROGRAMS += \
	upipe_swr_test
TESTS += \
	upipe_swr_test
endif

if HAVE_BITSTREAM
check_PROGRAMS += \
	uprobe_ts_log_test \
	upipe_ts_sync_test \
	upipe_ts_check_test \
	upipe_ts_split_test \
	upipe_ts_join_test \
	upipe_ts_decaps_test \
	upipe_ts_encaps_test \
	upipe_ts_pes_decaps_test \
	upipe_ts_pes_encaps_test \
	upipe_ts_psi_generator_test \
	upipe_ts_psi_inserter_test \
	upipe_ts_psi_merge_test \
	upipe_ts_psi_split_test \
	upipe_ts_pat_decoder_test \
	upipe_ts_pmt_decoder_test \
	upipe_ts_demux_test \
	upipe_ts_test \
	upipe_ts_aggregate_test \
	upipe_mpgv_framer_test \
	upipe_mpga_framer_test \
	upipe_a52_framer_test \
	upipe_rtp_prepend_test
TESTS += \
	uprobe_ts_log_test.sh \
	upipe_ts_sync_test \
	upipe_ts_check_test \
	upipe_ts_split_test \
	upipe_ts_join_test \
	upipe_ts_decaps_test \
	upipe_ts_encaps_test \
	upipe_ts_pes_decaps_test \
	upipe_ts_pes_encaps_test \
	upipe_ts_psi_generator_test \
	upipe_ts_psi_inserter_test \
	upipe_ts_psi_merge_test \
	upipe_ts_psi_split_test \
	upipe_ts_pat_decoder_test \
	upipe_ts_pmt_decoder_test \
	upipe_ts_demux_test \
	upipe_ts_aggregate_test \
	upipe_mpgv_framer_test \
	upipe_a52_framer_test \
	upipe_rtp_prepend_test
endif

if HAVE_X264
check_PROGRAMS += upipe_x264_test
TESTS += upipe_x264_test
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
upipe_transfer_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_multicat_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_http_src_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_trickplay_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_dup_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_genaux_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_null_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_skip_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_setattr_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_match_attr_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_probe_uref_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_multicat_probe_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_setrap_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_rtp_prepend_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_chunk_stream_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_htons_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_queue_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_mpgv_framer_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-framers/libupipe_framers.la
upipe_mpga_framer_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-framers/libupipe_framers.la
upipe_a52_framer_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-framers/libupipe_framers.la

upipe_avformat_test_CFLAGS = @AVFORMAT_CFLAGS@
upipe_avformat_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-av/libupipe_av.la @AVFORMAT_LIBS@
upipe_avcodec_test_CFLAGS = @AVFORMAT_CFLAGS@
upipe_avcodec_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upipe-av/libupipe_av.la @AVFORMAT_LIBS@
upipe_avcodec_decode_test_CFLAGS = @AVFORMAT_CFLAGS@
upipe_avcodec_decode_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-av/libupipe_av.la @AVFORMAT_LIBS@

upipe_sws_test_CFLAGS = $(SWSCALE_CFLAGS)
upipe_sws_test_LDADD = $(LDADD) $(SWSCALE_LIBS) $(top_builddir)/lib/upipe-swscale/libupipe_sws.la

upipe_swr_test_CFLAGS = $(SWRESAMPLE_CFLAGS)
upipe_swr_test_LDADD = $(LDADD) $(SWRESAMPLE_LIBS) $(top_builddir)/lib/upipe-swresample/libupipe_swr.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la

uprobe_ts_log_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_sync_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_check_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_split_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_join_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_decaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_encaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pes_decaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pes_encaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psi_generator_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psi_inserter_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psi_merge_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psi_split_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pat_decoder_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pmt_decoder_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_demux_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la $(top_builddir)/lib/upipe-framers/libupipe_framers.la
upipe_ts_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la $(top_builddir)/lib/upipe-framers/libupipe_framers.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_ts_aggregate_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la

upipe_glx_sink_test_LDADD = $(LDADD) $(GLX_LIBS) $(top_builddir)/lib/upipe-gl/libupipe_gl.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
upipe_glx_sink_test_CFLAGS = $(GLX_CFLAGS)
upipe_filter_blend_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-filters/libupipe_filters.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la

upipe_x264_test_LDADD = $(LDADD) $(X264_LIBS) $(top_builddir)/lib/upipe-x264/libupipe_x264.la
upipe_x264_test_CFLAGS = $(X264_CFLAGS) -Wall

upipe_alsa_sink_test_LDADD = $(LDADD) -lasound $(top_builddir)/lib/upipe-alsa/libupipe_alsa.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la

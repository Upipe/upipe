LOG_COMPILER = $(srcdir)/valgrind_wrapper.sh
AM_LOG_FLAGS = $(srcdir)

dist_check_SCRIPTS = \
	uprobe_stdio_test.sh \
	uprobe_stdio_color_test.sh \
	uprobe_syslog_test.sh \
	uprobe_prefix_test.sh \
	udict_inline_test.sh \
	upipe_file_test.sh \
	upipe_seq_src_test.sh \
	upipe_multicat_test.sh \
	upipe_ts_test.sh \
	valgrind_wrapper.sh \
	uref_uri_test.sh \
	ustring_test.sh \
	upipe_m3u_reader_test.sh

dist_check_DATA = \
	valgrind.supp \
	valgrind_osx.supp \
	udict_inline_test.txt \
	uprobe_stdio_test.txt \
	uprobe_stdio_color_test.txt \
	uprobe_prefix_test.txt \
	upipe_ts_test.ts \
	upipe_h264_framer_test.h \
	uref_uri_test.txt \
	ustring_test.txt \
	upipe_m3u_reader_test_files/1.m3u \
	upipe_m3u_reader_test_files/1.m3u.logs \
	upipe_m3u_reader_test_files/2.m3u \
	upipe_m3u_reader_test_files/2.m3u.logs \
	upipe_m3u_reader_test_files/3.m3u \
	upipe_m3u_reader_test_files/3.m3u.logs \
	upipe_m3u_reader_test_files/4.m3u \
	upipe_m3u_reader_test_files/4.m3u.logs \
	upipe_m3u_reader_test_files/5.m3u \
	upipe_m3u_reader_test_files/5.m3u.logs \
	upipe_m3u_reader_test_files/6.m3u \
	upipe_m3u_reader_test_files/6.m3u.logs \
	upipe_m3u_reader_test_files/7.m3u \
	upipe_m3u_reader_test_files/7.m3u.logs \
	upipe_m3u_reader_test_files/8.m3u \
	upipe_m3u_reader_test_files/8.m3u.logs \
	upipe_m3u_reader_test_files/9.m3u \
	upipe_m3u_reader_test_files/9.m3u.logs

check_PROGRAMS = \
	ulist_test \
	ubits_test \
	ustring_test \
	uuri_test \
	ucookie_test \
	uprobe_stdio_test \
	uprobe_stdio_color_test \
	uprobe_syslog_test \
	uprobe_prefix_test \
	uprobe_dejitter_test \
	uprobe_select_flows_test \
	uprobe_ubuf_mem_test \
	uprobe_ubuf_mem_pool_test \
	uprobe_uclock_test \
	uprobe_uref_mgr_test \
	umem_alloc_test \
	umem_pool_test \
	udict_inline_test \
	ubuf_block_mem_test \
	ubuf_pic_mem_test \
	ubuf_sound_mem_test \
	uref_std_test \
	uref_uri_test \
	uclock_std_test \
	upipe_play_test \
	upipe_trickplay_test \
	upipe_even_test \
	upipe_null_test \
	upipe_dup_test \
	upipe_genaux_test \
	upipe_multicat_probe_test \
	upipe_probe_uref_test \
	upipe_delay_test \
	upipe_skip_test \
	upipe_aggregate_test \
	upipe_convert_to_block_test \
	upipe_htons_test \
	upipe_chunk_stream_test \
	upipe_setflowdef_test \
	upipe_setattr_test \
	upipe_setrap_test \
	upipe_match_attr_test \
	upipe_blit_test \
	upipe_crop_test \
	upipe_audio_split_test \
	upipe_videocont_test \
	upipe_audiocont_test \
	upipe_filter_ebur128_test \
	upipe_audio_max_test \
	upipe_audio_bar_test \
	upipe_audio_graph_test \
	upipe_filter_blend_test

TESTS = \
	ulist_test \
	ubits_test \
	uuri_test \
	ustring_test.sh \
	ucookie_test \
	umem_alloc_test \
	umem_pool_test \
	udict_inline_test.sh \
	ubuf_block_mem_test \
	ubuf_pic_mem_test \
	ubuf_sound_mem_test \
	uprobe_stdio_test.sh \
	uprobe_stdio_color_test.sh \
	uprobe_syslog_test.sh \
	uprobe_prefix_test.sh \
	uprobe_dejitter_test \
	uprobe_select_flows_test \
	uprobe_ubuf_mem_test \
	uprobe_ubuf_mem_pool_test \
	uprobe_uclock_test \
	uprobe_uref_mgr_test \
	uref_std_test \
	uref_uri_test.sh \
	uclock_std_test \
	upipe_null_test \
	upipe_play_test \
	upipe_trickplay_test \
	upipe_even_test \
	upipe_dup_test \
	upipe_genaux_test \
	upipe_multicat_probe_test \
	upipe_probe_uref_test \
	upipe_delay_test \
	upipe_skip_test \
	upipe_aggregate_test \
	upipe_convert_to_block_test \
	upipe_htons_test \
	upipe_chunk_stream_test \
	upipe_setflowdef_test \
	upipe_setattr_test \
	upipe_setrap_test \
	upipe_match_attr_test \
	upipe_blit_test \
	upipe_crop_test \
	upipe_audio_split_test \
	upipe_videocont_test \
	upipe_audiocont_test \
	upipe_filter_ebur128_test \
	upipe_audio_max_test \
	upipe_audio_bar_test \
	upipe_audio_graph_test \
	upipe_filter_blend_test

if HAVE_SPEEXDSP
check_PROGRAMS += \
	upipe_speexdsp_test
TESTS += \
	upipe_speexdsp_test
endif

if HAVE_EV
check_PROGRAMS += \
	upump_ev_test \
	ulifo_uqueue_test \
	udeal_test \
	uprobe_upump_mgr_test \
	upipe_transfer_test \
	upipe_file_test \
	upipe_seq_src_test \
	upipe_queue_test \
	upipe_udp_test \
	upipe_http_src_test \
	upipe_multicat_test \
	upipe_blank_source_test \
	upipe_time_limit_test \
	upipe_worker_linear_test \
	upipe_worker_sink_test \
	upipe_worker_source_test \
	upipe_worker_test \
	upipe_m3u_reader_test

TESTS += \
	upump_ev_test \
	ulifo_uqueue_test \
	udeal_test \
	uprobe_upump_mgr_test \
	upipe_transfer_test \
	upipe_file_test.sh \
	upipe_seq_src_test.sh \
	upipe_queue_test \
	upipe_udp_test \
	upipe_multicat_test.sh \
	upipe_blank_source_test \
	upipe_time_limit_test \
	upipe_worker_linear_test \
	upipe_worker_sink_test \
	upipe_worker_source_test \
	upipe_worker_test \
	upipe_m3u_reader_test.sh

if HAVE_PTHREAD
check_PROGRAMS += \
	uprobe_pthread_upump_mgr_test
TESTS += \
	uprobe_pthread_upump_mgr_test
endif

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
	upipe_rtp_decaps_test \
	upipe_rtp_prepend_test \
	upipe_rtp_test \
	upipe_mpgv_framer_test \
	upipe_mpga_framer_test \
	upipe_h264_framer_test \
	upipe_a52_framer_test \
	upipe_video_trim_test \
	upipe_ts_check_test \
	upipe_ts_decaps_test \
	upipe_ts_eit_decoder_test \
	upipe_ts_nit_decoder_test \
	upipe_ts_pes_decaps_test \
	upipe_ts_pat_decoder_test \
	upipe_ts_pmt_decoder_test \
	upipe_ts_psi_join_test \
	upipe_ts_psi_merge_test \
	upipe_ts_psi_split_test \
	upipe_ts_scte35_decoder_test \
	upipe_ts_scte35_generator_test \
	upipe_ts_sdt_decoder_test \
	upipe_ts_tdt_decoder_test \
	upipe_ts_split_test \
	upipe_ts_sync_test \
	upipe_ts_demux_test \
	upipe_ts_pid_filter_test \
	upipe_ts_encaps_test \
	upipe_ts_pes_encaps_test \
	upipe_ts_psi_generator_test \
	upipe_ts_si_generator_test \
	upipe_ts_tstd_test \
	upipe_s337_encaps_test \
	upipe_pack10_test \
	upipe_unpack10_test \
	$(NULL)
TESTS += \
	upipe_rtp_decaps_test \
	upipe_rtp_prepend_test \
	upipe_rtp_test \
	upipe_mpgv_framer_test \
	upipe_mpga_framer_test \
	upipe_h264_framer_test \
	upipe_a52_framer_test \
	upipe_video_trim_test \
	upipe_ts_check_test \
	upipe_ts_decaps_test \
	upipe_ts_eit_decoder_test \
	upipe_ts_nit_decoder_test \
	upipe_ts_pes_decaps_test \
	upipe_ts_pat_decoder_test \
	upipe_ts_pmt_decoder_test \
	upipe_ts_psi_join_test \
	upipe_ts_psi_merge_test \
	upipe_ts_psi_split_test \
	upipe_ts_scte35_decoder_test \
	upipe_ts_scte35_generator_test \
	upipe_ts_sdt_decoder_test \
	upipe_ts_tdt_decoder_test \
	upipe_ts_split_test \
	upipe_ts_sync_test \
	upipe_ts_demux_test \
	upipe_ts_pid_filter_test \
	upipe_ts_encaps_test \
	upipe_ts_pes_encaps_test \
	upipe_ts_psi_generator_test \
	upipe_ts_si_generator_test \
	upipe_ts_tstd_test \
	upipe_s337_encaps_test \
	upipe_pack10_test \
	upipe_unpack10_test \
	$(NULL)

if HAVE_EV
check_PROGRAMS += \
	upipe_ts_scte35_probe_test \
	upipe_ts_test
TESTS += \
	upipe_ts_scte35_probe_test \
	upipe_ts_test.sh
endif

if HAVE_X264
check_PROGRAMS += \
	upipe_x264_test \
	upipe_h264_framer_test_build
TESTS += upipe_x264_test
endif
endif

if HAVE_ECORE
check_PROGRAMS += upump_ecore_test
TESTS += upump_ecore_test
endif

if HAVE_QTWEBKIT
if HAVE_EV
check_PROGRAMS += upipe_qt_html_test
TESTS += upipe_qt_html_test
endif
endif

if HAVE_AVUTIL
check_PROGRAMS += \
	upipe_v210dec_test upipe_v210enc_test
TESTS += \
	upipe_v210dec_test upipe_v210enc_test
endif

AM_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
LDADD = $(top_builddir)/lib/upipe/libupipe.la

upump_ev_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
ulifo_uqueue_test_CFLAGS = $(AM_CFLAGS) -pthread
ulifo_uqueue_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
udeal_test_CFLAGS = $(AM_CFLAGS) -pthread
udeal_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
uprobe_upump_mgr_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
upipe_file_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_udp_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_transfer_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la -lpthread
upipe_worker_linear_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upipe-pthread/libupipe_pthread.la -lpthread
upipe_worker_sink_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upipe-pthread/libupipe_pthread.la -lpthread
upipe_worker_source_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upipe-pthread/libupipe_pthread.la -lpthread
upipe_worker_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upipe-pthread/libupipe_pthread.la -lpthread
upipe_multicat_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_http_src_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_blank_source_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_time_limit_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_play_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_trickplay_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_even_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_dup_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_genaux_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_delay_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_null_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_skip_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_aggregate_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_convert_to_block_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_setflowdef_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_setattr_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_match_attr_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_probe_uref_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_multicat_probe_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_setrap_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_rtp_decaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_rtp_prepend_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_rtp_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upipe-framers/libupipe_framers.la
upipe_chunk_stream_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_htons_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_blit_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_crop_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_qt_html_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-qt/libupipe_qt.la -L/usr/lib/x86_64-linux-gnu -lQtCore -lQtGui -lQtWebKit -lpthread $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-pthread/libupipe_pthread.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
upipe_audio_split_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_videocont_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_audiocont_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_queue_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
uprobe_pthread_upump_mgr_test_LDADD = $(LDADD) -lev -lpthread $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-pthread/libupipe_pthread.la
upipe_mpgv_framer_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-framers/libupipe_framers.la
upipe_mpga_framer_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-framers/libupipe_framers.la
upipe_a52_framer_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-framers/libupipe_framers.la
upipe_video_trim_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-framers/libupipe_framers.la
upipe_h264_framer_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-framers/libupipe_framers.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_s337_encaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_pack10_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-hbrmt/libupipe_hbrmt.la
upipe_unpack10_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-hbrmt/libupipe_hbrmt.la
upipe_v210dec_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-v210/libupipe_v210.la
upipe_v210enc_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-v210/libupipe_v210.la

upipe_avformat_test_CFLAGS = $(AM_CFLAGS) $(AVFORMAT_CFLAGS)
upipe_avformat_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-av/libupipe_av.la $(AVFORMAT_LIBS)
upipe_avcodec_test_CFLAGS = $(AM_CFLAGS) $(AVFORMAT_CFLAGS)
upipe_avcodec_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upipe-av/libupipe_av.la $(AVFORMAT_LIBS) -lpthread
upipe_avcodec_decode_test_CFLAGS = $(AM_CFLAGS) $(AVFORMAT_CFLAGS)
upipe_avcodec_decode_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la $(top_builddir)/lib/upipe-av/libupipe_av.la $(AVFORMAT_LIBS) -lpthread

upipe_sws_test_CFLAGS = $(AM_CFLAGS) $(SWSCALE_CFLAGS)
upipe_sws_test_LDADD = $(LDADD) $(SWSCALE_LIBS) $(top_builddir)/lib/upipe-swscale/libupipe_swscale.la

upipe_swr_test_CFLAGS = $(AM_CFLAGS) $(SWRESAMPLE_CFLAGS)
upipe_swr_test_LDADD = $(LDADD) $(SWRESAMPLE_LIBS) $(top_builddir)/lib/upipe-swresample/libupipe_swresample.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la

upipe_ts_sync_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_check_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_split_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_decaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_eit_decoder_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_encaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_nit_decoder_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pes_decaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pes_encaps_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psi_generator_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psi_join_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psi_merge_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_psi_split_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pat_decoder_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_pmt_decoder_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_scte35_decoder_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_scte35_generator_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_scte35_probe_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
upipe_ts_sdt_decoder_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_si_generator_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_tdt_decoder_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_demux_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la $(top_builddir)/lib/upipe-framers/libupipe_framers.la
upipe_ts_pid_filter_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la
upipe_ts_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la $(top_builddir)/lib/upipe-framers/libupipe_framers.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_ts_tstd_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-ts/libupipe_ts.la

upipe_glx_sink_test_LDADD = $(LDADD) $(GLX_LIBS) $(top_builddir)/lib/upipe-gl/libupipe_gl.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
upipe_glx_sink_test_CFLAGS = $(AM_CFLAGS) $(GLX_CFLAGS)
upipe_filter_blend_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-filters/libupipe_filters.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_filter_ebur128_test_LDADD = $(LDADD) -lm $(top_builddir)/lib/upipe-filters/libupipe_filters.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_audio_max_test_LDADD = $(LDADD) -lm $(top_builddir)/lib/upipe-filters/libupipe_filters.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_audio_bar_test_LDADD = $(LDADD) -lm $(top_builddir)/lib/upipe-filters/libupipe_filters.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_audio_graph_test_LDADD = $(LDADD) -lm $(top_builddir)/lib/upipe-filters/libupipe_filters.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
upipe_speexdsp_test_LDADD = $(LDADD) $(top_builddir)/lib/upipe-speexdsp/libupipe_speexdsp.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la

upipe_x264_test_LDADD = $(LDADD) $(X264_LIBS) $(top_builddir)/lib/upipe-x264/libupipe_x264.la
upipe_x264_test_CFLAGS = $(AM_CFLAGS) $(X264_CFLAGS)
upipe_h264_framer_test_build_LDADD = $(LDADD) $(X264_LIBS) $(top_builddir)/lib/upipe-x264/libupipe_x264.la
upipe_h264_framer_test_build_CFLAGS = $(AM_CFLAGS) $(X264_CFLAGS)

upipe_alsa_sink_test_LDADD = $(LDADD) -lasound $(top_builddir)/lib/upipe-alsa/libupipe_alsa.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la -lev $(top_builddir)/lib/upump-ev/libupump_ev.la
upump_ecore_test_LDADD = $(LDADD) $(ECORE_LIBS) $(top_builddir)/lib/upump-ecore/libupump_ecore.la
upump_ecore_test_CFLAGS = $(AM_CFLAGS) $(ECORE_CFLAGS)
upipe_m3u_reader_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la
ustring_test_CFLAGS = $(AM_CFLAGS) -fno-inline
upipe_seq_src_test_LDADD = $(LDADD) -lev $(top_builddir)/lib/upump-ev/libupump_ev.la $(top_builddir)/lib/upipe-modules/libupipe_modules.la

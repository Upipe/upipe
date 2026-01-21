log-compiler := $(abspath $(srcdir)/valgrind_wrapper.sh)
log-flags := $(abspath $(srcdir))

log-env = $(if $(have_san),DISABLE_VALGRIND=1) \
          $(if $(have_ubsan),UBSAN_OPTIONS="print_stacktrace=1") \
          $(if $(have_apple),_DYLD_LIBRARY_PATH=$(_LD_LIBRARY_PATH))

distfiles = valgrind_wrapper.sh valgrind.supp \
            udict_inline_test.txt \
            upipe_ts_test.ts \
            uprobe_prefix_test.txt \
            uprobe_stdio_test.txt \
            uref_dump_test.txt \
            uref_uri_test.txt \
            ustring_test.txt \
            $(foreach i,1 2 3 4 5 6 7 8 9, \
              upipe_m3u_reader_test_files/$i.m3u \
              upipe_m3u_reader_test_files/$i.m3u.logs)

tests += ubits_test
ubits_test-src = ubits_test.c

tests += ubuf_av_sound_test
ubuf_av_sound_test-src = ubuf_av_sound_test.c
ubuf_av_sound_test-libs = libupipe libupipe_av libavutil

tests += ubuf_block_mem_test
ubuf_block_mem_test-src = ubuf_block_mem_test.c
ubuf_block_mem_test-libs = libupipe

tests += ubuf_pic_clear_test
ubuf_pic_clear_test-src = ubuf_pic_clear_test.c
ubuf_pic_clear_test-libs = libupipe

tests += ubuf_pic_mem_test
ubuf_pic_mem_test-src = ubuf_pic_mem_test.c
ubuf_pic_mem_test-libs = libupipe

tests += ubuf_sound_mem_test
ubuf_sound_mem_test-src = ubuf_sound_mem_test.c
ubuf_sound_mem_test-libs = libupipe

tests += uclock_std_test
uclock_std_test-src = uclock_std_test.c
uclock_std_test-libs = libupipe

tests += ucookie_test
ucookie_test-src = ucookie_test.c
ucookie_test-libs = libupipe

tests += udeal_test
udeal_test-src = udeal_test.c
udeal_test-libs = libupump_ev pthread

tests += udict_inline_test.sh
udict_inline_test.sh-deps = udict_inline_test

test-targets += udict_inline_test
udict_inline_test-src = udict_inline_test.c
udict_inline_test-libs = libupipe

tests += ulifo_uqueue_test
ulifo_uqueue_test-src = ulifo_uqueue_test.c
ulifo_uqueue_test-libs = libupump_ev libev pthread

tests += ulist_test
ulist_test-src = ulist_test.c

tests += umem_alloc_test
umem_alloc_test-src = umem_alloc_test.c
umem_alloc_test-libs = libupipe

tests += umem_pool_test
umem_pool_test-src = umem_pool_test.c
umem_pool_test-libs = libupipe

tests += upipe_a52_framer_test
upipe_a52_framer_test-src = upipe_a52_framer_test.c
upipe_a52_framer_test-libs = libupipe libupipe_framers bitstream

tests += upipe_aggregate_test
upipe_aggregate_test-src = upipe_aggregate_test.c
upipe_aggregate_test-libs = libupipe libupipe_modules

test-targets += upipe_alsa_sink_test
upipe_alsa_sink_test-src = upipe_alsa_sink_test.c
upipe_alsa_sink_test-libs = libupipe libupipe_modules libupipe_alsa libupump_ev

tests += upipe_audio_bar_test
upipe_audio_bar_test-src = upipe_audio_bar_test.c
upipe_audio_bar_test-libs = libupipe libupipe_filters

tests += upipe_audio_blank_test
upipe_audio_blank_test-src = upipe_audio_blank_test.c
upipe_audio_blank_test-libs = libupipe libupipe_modules

tests += upipe_audio_copy_test
upipe_audio_copy_test-src = upipe_audio_copy_test.c
upipe_audio_copy_test-libs = libupipe libupipe_modules

tests += upipe_audio_graph_test
upipe_audio_graph_test-src = upipe_audio_graph_test.c
upipe_audio_graph_test-libs = libupipe libupipe_filters

tests += upipe_audio_max_test
upipe_audio_max_test-src = upipe_audio_max_test.c
upipe_audio_max_test-libs = libupipe libupipe_filters

tests += upipe_audio_merge_test
upipe_audio_merge_test-src = upipe_audio_merge_test.c
upipe_audio_merge_test-libs = libupipe libupipe_modules

tests += upipe_audio_split_test
upipe_audio_split_test-src = upipe_audio_split_test.c
upipe_audio_split_test-libs = libupipe libupipe_modules

tests += upipe_audiocont_test
upipe_audiocont_test-src = upipe_audiocont_test.c
upipe_audiocont_test-libs = libupipe libupipe_modules

tests += upipe_auto_inner_test
upipe_auto_inner_test-src = upipe_auto_inner_test.c
upipe_auto_inner_test-libs = libupipe libupipe_modules

test-targets += upipe_auto_source_test
upipe_auto_source_test-src = upipe_auto_source_test.c
upipe_auto_source_test-libs = libupipe libupipe_modules libupump_ev
upipe_auto_source_test-opt-libs = libupipe_bearssl libupipe_openssl

test-targets += upipe_avcodec_decode_test
upipe_avcodec_decode_test-src = upipe_avcodec_decode_test.c
upipe_avcodec_decode_test-deps = upipe_avcdec
upipe_avcodec_decode_test-libs = libupipe libupipe_modules libupipe_av \
                                 libupump_ev libavformat libavcodec pthread

tests += upipe_avcodec_test
upipe_avcodec_test-src = upipe_avcodec_test.c
upipe_avcodec_test-deps = upipe_avcdec
upipe_avcodec_test-libs = libupipe libupipe_modules libupipe_av libupump_ev \
                          pthread

test-targets += upipe_avformat_test
upipe_avformat_test-src = upipe_avformat_test.c
upipe_avformat_test-libs = libupipe libupipe_av libupump_ev

tests += upipe_avfilter_test
upipe_avfilter_test-src = upipe_avfilter_test.c
upipe_avfilter_test-libs = libupipe libupipe_modules libupipe_av libupump_ev

tests += upipe_blank_source_test
upipe_blank_source_test-src = upipe_blank_source_test.c
upipe_blank_source_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_blit_test
upipe_blit_test-src = upipe_blit_test.c
upipe_blit_test-libs = libupipe libupipe_modules

tests += upipe_block_to_sound_test
upipe_block_to_sound_test-src = upipe_block_to_sound_test.c
upipe_block_to_sound_test-libs = libupipe libupipe_modules

tests += upipe_chunk_stream_test
upipe_chunk_stream_test-src = upipe_chunk_stream_test.c
upipe_chunk_stream_test-libs = libupipe libupipe_modules

tests += upipe_convert_to_block_test
upipe_convert_to_block_test-src = upipe_convert_to_block_test.c
upipe_convert_to_block_test-libs = libupipe libupipe_modules

tests += upipe_crop_test
upipe_crop_test-src = upipe_crop_test.c
upipe_crop_test-libs = libupipe libupipe_modules

tests += upipe_delay_test
upipe_delay_test-src = upipe_delay_test.c
upipe_delay_test-libs = libupipe libupipe_modules

tests += upipe_dtsdi_test.sh
upipe_dtsdi_test.sh-deps = upipe_dtsdi_test

test-targets += upipe_dtsdi_test
upipe_dtsdi_test-src = upipe_dtsdi_test.c
upipe_dtsdi_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_dup_test
upipe_dup_test-src = upipe_dup_test.c
upipe_dup_test-libs = libupipe libupipe_modules

tests += upipe_dvbcsa_test
upipe_dvbcsa_test-src = upipe_dvbcsa_test.c
upipe_dvbcsa_test-deps = libupipe_dvbcsa

tests += upipe_ebur128_test
upipe_ebur128_test-src = upipe_ebur128_test.c
upipe_ebur128_test-libs = libupipe libupipe_modules libupipe_ebur128
upipe_ebur128_test-ldlibs = -lm

tests += upipe_even_test
upipe_even_test-src = upipe_even_test.c
upipe_even_test-libs = libupipe libupipe_modules

tests += upipe_file_test.sh
upipe_file_test.sh-deps = upipe_file_test

test-targets += upipe_file_test
upipe_file_test-src = upipe_file_test.c
upipe_file_test-deps = upipe_fsink
upipe_file_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_filter_blend_test
upipe_filter_blend_test-src = upipe_filter_blend_test.c
upipe_filter_blend_test-libs = libupipe libupipe_modules libupipe_filters

tests += upipe_genaux_test
upipe_genaux_test-src = upipe_genaux_test.c
upipe_genaux_test-libs = libupipe libupipe_modules

test-targets += upipe_glx_sink_test
upipe_glx_sink_test-src = upipe_glx_sink_test.c
upipe_glx_sink_test-libs = libupipe libupipe_gl libupump_ev

tests += upipe_grid_test
upipe_grid_test-src = upipe_grid_test.c
upipe_grid_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_h264_framer_test
upipe_h264_framer_test-src = upipe_h264_framer_test.c upipe_h264_framer_test.h
upipe_h264_framer_test-libs = libupipe libupipe_framers bitstream

test-targets += upipe_h264_framer_test_build
upipe_h264_framer_test_build-src = upipe_h264_framer_test_build.c
upipe_h264_framer_test_build-libs = libupipe libupipe_x264 bitstream

tests += upipe_htons_test
upipe_htons_test-src = upipe_htons_test.c
upipe_htons_test-libs = libupipe libupipe_modules

test-targets += upipe_http_src_test
upipe_http_src_test-src = upipe_http_src_test.c
upipe_http_src_test-libs = libupipe libupipe_modules libupump_ev
upipe_http_src_test-opt-libs = libupipe_bearssl libupipe_openssl

tests += upipe_m3u_reader_test.sh
upipe_m3u_reader_test.sh-deps = upipe_m3u_reader_test

test-targets += upipe_m3u_reader_test
upipe_m3u_reader_test-src = upipe_m3u_reader_test.c
upipe_m3u_reader_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_match_attr_test
upipe_match_attr_test-src = upipe_match_attr_test.c
upipe_match_attr_test-libs = libupipe libupipe_modules

tests += upipe_mpga_framer_test
upipe_mpga_framer_test-src = upipe_mpga_framer_test.c
upipe_mpga_framer_test-libs = libupipe libupipe_framers bitstream

tests += upipe_mpgv_framer_test
upipe_mpgv_framer_test-src = upipe_mpgv_framer_test.c
upipe_mpgv_framer_test-libs = libupipe libupipe_framers bitstream

tests += upipe_multicat_probe_test
upipe_multicat_probe_test-src = upipe_multicat_probe_test.c
upipe_multicat_probe_test-libs = libupipe libupipe_modules

tests += upipe_multicat_test.sh
upipe_multicat_test.sh-deps = upipe_multicat_test

test-targets += upipe_multicat_test
upipe_multicat_test-src = upipe_multicat_test.c
upipe_multicat_test-deps = upipe_fsink
upipe_multicat_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_null_test
upipe_null_test-src = upipe_null_test.c
upipe_null_test-libs = libupipe libupipe_modules

tests += upipe_pack10_test
upipe_pack10_test-src = upipe_pack10_test.c
upipe_pack10_test-libs = libupipe libupipe_hbrmt

tests += upipe_play_test
upipe_play_test-src = upipe_play_test.c
upipe_play_test-libs = libupipe libupipe_modules

tests += upipe_probe_uref_test
upipe_probe_uref_test-src = upipe_probe_uref_test.c
upipe_probe_uref_test-libs = libupipe libupipe_modules

tests += upipe_qt_html_test
upipe_qt_html_test-src = upipe_qt_html_test.c
upipe_qt_html_test-libs = libupipe libupipe_pthread libupipe_qt libupump_ev

tests += upipe_queue_test
upipe_queue_test-src = upipe_queue_test.c
upipe_queue_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_row_join_test
upipe_row_join_test-src = upipe_row_join_test.c
upipe_row_join_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_row_split_test
upipe_row_split_test-src = upipe_row_split_test.c
upipe_row_split_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_rtp_decaps_test
upipe_rtp_decaps_test-src = upipe_rtp_decaps_test.c
upipe_rtp_decaps_test-libs = libupipe libupipe_modules bitstream

tests += upipe_rtp_prepend_test
upipe_rtp_prepend_test-src = upipe_rtp_prepend_test.c
upipe_rtp_prepend_test-libs = libupipe libupipe_modules bitstream

tests += upipe_rtp_test
upipe_rtp_test-src = upipe_rtp_test.c
upipe_rtp_test-libs = libupipe libupipe_modules libupipe_framers libupump_ev \
                      bitstream

tests += upipe_s337_encaps_test
upipe_s337_encaps_test-src = upipe_s337_encaps_test.c
upipe_s337_encaps_test-libs = libupipe libupipe_modules bitstream

tests += upipe_separate_fields_test
upipe_separate_fields_test-src = upipe_separate_fields_test.c
upipe_separate_fields_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_seq_src_test.sh
upipe_seq_src_test.sh-deps = upipe_seq_src_test

test-targets += upipe_seq_src_test
upipe_seq_src_test-src = upipe_seq_src_test.c
upipe_seq_src_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_setattr_test
upipe_setattr_test-src = upipe_setattr_test.c
upipe_setattr_test-libs = libupipe libupipe_modules

tests += upipe_setflowdef_test
upipe_setflowdef_test-src = upipe_setflowdef_test.c
upipe_setflowdef_test-libs = libupipe libupipe_modules

tests += upipe_setrap_test
upipe_setrap_test-src = upipe_setrap_test.c
upipe_setrap_test-libs = libupipe libupipe_modules

tests += upipe_skip_test
upipe_skip_test-src = upipe_skip_test.c
upipe_skip_test-libs = libupipe libupipe_modules

tests += upipe_speexdsp_test
upipe_speexdsp_test-src = upipe_speexdsp_test.c
upipe_speexdsp_test-libs = libupipe libupipe_speexdsp

tests += upipe_swr_test
upipe_swr_test-src = upipe_swr_test.c
upipe_swr_test-libs = libupipe libupipe_modules libupipe_swresample

tests += upipe_sws_test
upipe_sws_test-src = upipe_sws_test.c
upipe_sws_test-libs = libupipe libupipe_swscale libswscale libavutil

tests += upipe_time_limit_test
upipe_time_limit_test-src = upipe_time_limit_test.c
upipe_time_limit_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_transfer_test
upipe_transfer_test-src = upipe_transfer_test.c
upipe_transfer_test-libs = libupipe libupipe_modules libupump_ev pthread

tests += upipe_trickplay_test
upipe_trickplay_test-src = upipe_trickplay_test.c
upipe_trickplay_test-libs = libupipe libupipe_modules

tests += upipe_ts_check_test
upipe_ts_check_test-src = upipe_ts_check_test.c
upipe_ts_check_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_decaps_test
upipe_ts_decaps_test-src = upipe_ts_decaps_test.c
upipe_ts_decaps_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_demux_test
upipe_ts_demux_test-src = upipe_ts_demux_test.c
upipe_ts_demux_test-libs = libupipe libupipe_ts libupipe_framers bitstream

tests += upipe_ts_eit_decoder_test
upipe_ts_eit_decoder_test-src = upipe_ts_eit_decoder_test.c
upipe_ts_eit_decoder_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_encaps_test
upipe_ts_encaps_test-src = upipe_ts_encaps_test.c
upipe_ts_encaps_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_nit_decoder_test
upipe_ts_nit_decoder_test-src = upipe_ts_nit_decoder_test.c
upipe_ts_nit_decoder_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_pat_decoder_test
upipe_ts_pat_decoder_test-src = upipe_ts_pat_decoder_test.c
upipe_ts_pat_decoder_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_pes_decaps_test
upipe_ts_pes_decaps_test-src = upipe_ts_pes_decaps_test.c
upipe_ts_pes_decaps_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_pes_encaps_test
upipe_ts_pes_encaps_test-src = upipe_ts_pes_encaps_test.c
upipe_ts_pes_encaps_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_pid_filter_test
upipe_ts_pid_filter_test-src = upipe_ts_pid_filter_test.c
upipe_ts_pid_filter_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_pmt_decoder_test
upipe_ts_pmt_decoder_test-src = upipe_ts_pmt_decoder_test.c
upipe_ts_pmt_decoder_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_psi_generator_test
upipe_ts_psi_generator_test-src = upipe_ts_psi_generator_test.c
upipe_ts_psi_generator_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_psi_join_test
upipe_ts_psi_join_test-src = upipe_ts_psi_join_test.c
upipe_ts_psi_join_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_psi_merge_test
upipe_ts_psi_merge_test-src = upipe_ts_psi_merge_test.c
upipe_ts_psi_merge_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_psi_split_test
upipe_ts_psi_split_test-src = upipe_ts_psi_split_test.c
upipe_ts_psi_split_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_scte35_decoder_test
upipe_ts_scte35_decoder_test-src = upipe_ts_scte35_decoder_test.c
upipe_ts_scte35_decoder_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_scte35_generator_test
upipe_ts_scte35_generator_test-src = upipe_ts_scte35_generator_test.c
upipe_ts_scte35_generator_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_scte35_probe_test
upipe_ts_scte35_probe_test-src = upipe_ts_scte35_probe_test.c
upipe_ts_scte35_probe_test-libs = libupipe libupipe_ts libupump_ev bitstream \
                                  libev

tests += upipe_ts_sdt_decoder_test
upipe_ts_sdt_decoder_test-src = upipe_ts_sdt_decoder_test.c
upipe_ts_sdt_decoder_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_si_generator_test
upipe_ts_si_generator_test-src = upipe_ts_si_generator_test.c
upipe_ts_si_generator_test-libs = libupipe libupipe_ts bitstream
upipe_ts_si_generator_test-opt-libs = libiconv

tests += upipe_ts_split_test
upipe_ts_split_test-src = upipe_ts_split_test.c
upipe_ts_split_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_sync_test
upipe_ts_sync_test-src = upipe_ts_sync_test.c
upipe_ts_sync_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_tdt_decoder_test
upipe_ts_tdt_decoder_test-src = upipe_ts_tdt_decoder_test.c
upipe_ts_tdt_decoder_test-libs = libupipe libupipe_ts bitstream

tests += upipe_ts_test.sh
upipe_ts_test.sh-deps = upipe_ts_test

test-targets += upipe_ts_test
upipe_ts_test-src = upipe_ts_test.c
upipe_ts_test-deps = upipe_fsink
upipe_ts_test-libs = libupipe libupipe_modules libupipe_ts libupipe_framers \
                     libupump_ev

tests += upipe_ts_tstd_test
upipe_ts_tstd_test-src = upipe_ts_tstd_test.c
upipe_ts_tstd_test-libs = libupipe libupipe_ts

tests += upipe_udp_test
upipe_udp_test-src = upipe_udp_test.c
upipe_udp_test-deps = upipe_udpsink
upipe_udp_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_unpack10_test
upipe_unpack10_test-src = upipe_unpack10_test.c
upipe_unpack10_test-libs = libupipe libupipe_hbrmt

tests += upipe_v210dec_test
upipe_v210dec_test-src = upipe_v210dec_test.c
upipe_v210dec_test-libs = libupipe libupipe_v210 libavutil

tests += upipe_v210enc_test
upipe_v210enc_test-src = upipe_v210enc_test.c
upipe_v210enc_test-libs = libupipe libupipe_v210 libavutil

tests += upipe_video_blank_test
upipe_video_blank_test-src = upipe_video_blank_test.c
upipe_video_blank_test-libs = libupipe libupipe_modules

tests += upipe_video_trim_test
upipe_video_trim_test-src = upipe_video_trim_test.c
upipe_video_trim_test-libs = libupipe libupipe_framers bitstream

tests += upipe_videocont_test
upipe_videocont_test-src = upipe_videocont_test.c
upipe_videocont_test-libs = libupipe libupipe_modules

tests += upipe_void_source_test
upipe_void_source_test-src = upipe_void_source_test.c
upipe_void_source_test-libs = libupipe libupipe_modules libupump_ev

tests += upipe_worker_linear_test
upipe_worker_linear_test-src = upipe_worker_linear_test.c
upipe_worker_linear_test-libs = libupipe libupipe_modules libupipe_pthread \
                                libupump_ev pthread

tests += upipe_worker_sink_test
upipe_worker_sink_test-src = upipe_worker_sink_test.c
upipe_worker_sink_test-libs = libupipe libupipe_modules libupipe_pthread \
                              libupump_ev pthread

tests += upipe_worker_source_test
upipe_worker_source_test-src = upipe_worker_source_test.c
upipe_worker_source_test-libs = libupipe libupipe_modules libupipe_pthread \
                                libupump_ev pthread

test-targets += upipe_worker_stress_test
upipe_worker_stress_test-src = upipe_worker_stress_test.c
upipe_worker_stress_test-libs = libupipe libupipe_modules libupipe_pthread \
                                libupump_ev pthread

tests += upipe_worker_test
upipe_worker_test-src = upipe_worker_test.c
upipe_worker_test-libs = libupipe libupipe_modules libupipe_pthread \
                         libupump_ev pthread

tests += upipe_x264_test
upipe_x264_test-src = upipe_x264_test.c
upipe_x264_test-libs = libupipe libupipe_x264

tests += upipe_x265_test
upipe_x265_test-src = upipe_x265_test.c
upipe_x265_test-libs = libupipe libupipe_x265
check-$(builddir)/upipe_x265_test: log-env += ASAN_OPTIONS="detect_leaks=0"

tests += upipe_zoneplate_source_test
upipe_zoneplate_source_test-src = upipe_zoneplate_source_test.c
upipe_zoneplate_source_test-libs = libupipe libupipe_filters libupump_ev

tests += uprobe_dejitter_test
uprobe_dejitter_test-src = uprobe_dejitter_test.c
uprobe_dejitter_test-libs = libupipe

tests += uprobe_prefix_test.sh
uprobe_prefix_test.sh-deps = uprobe_prefix_test

test-targets += uprobe_prefix_test
uprobe_prefix_test-src = uprobe_prefix_test.c
uprobe_prefix_test-libs = libupipe

tests += uprobe_pthread_upump_mgr_test
uprobe_pthread_upump_mgr_test-src = uprobe_pthread_upump_mgr_test.c
uprobe_pthread_upump_mgr_test-libs = libupipe_pthread libupump_ev pthread

tests += uprobe_select_flows_test
uprobe_select_flows_test-src = uprobe_select_flows_test.c
uprobe_select_flows_test-libs = libupipe

tests += uprobe_stdio_test.sh
uprobe_stdio_test.sh-deps = uprobe_stdio_test

test-targets += uprobe_stdio_test
uprobe_stdio_test-src = uprobe_stdio_test.c
uprobe_stdio_test-libs = libupipe

tests += uprobe_syslog_test.sh
uprobe_syslog_test.sh-deps = uprobe_syslog_test

test-targets += uprobe_syslog_test
uprobe_syslog_test-src = uprobe_syslog_test.c
uprobe_syslog_test-libs = libupipe

tests += uprobe_ubuf_mem_pool_test
uprobe_ubuf_mem_pool_test-src = uprobe_ubuf_mem_pool_test.c
uprobe_ubuf_mem_pool_test-libs = libupipe

tests += uprobe_ubuf_mem_test
uprobe_ubuf_mem_test-src = uprobe_ubuf_mem_test.c
uprobe_ubuf_mem_test-libs = libupipe

tests += uprobe_uclock_test
uprobe_uclock_test-src = uprobe_uclock_test.c
uprobe_uclock_test-libs = libupipe

tests += uprobe_upump_mgr_test
uprobe_upump_mgr_test-src = uprobe_upump_mgr_test.c
uprobe_upump_mgr_test-libs = libupipe libupump_ev

tests += uprobe_uref_mgr_test
uprobe_uref_mgr_test-src = uprobe_uref_mgr_test.c
uprobe_uref_mgr_test-libs = libupipe

tests += upump_ecore_test
upump_ecore_test-src = upump_ecore_test.c upump_common_test.c \
                       upump_common_test.h
upump_ecore_test-libs = libupump_ecore

tests += upump_ev_test
upump_ev_test-src = upump_ev_test.c upump_common_test.c upump_common_test.h
upump_ev_test-libs = libupump_ev

tests += upump_srt_test
upump_srt_test-src = upump_srt_test.c upump_common_test.c upump_common_test.h
upump_srt_test-libs = libupump_srt srt

$(builddir)/upump_common_test.o: CFLAGS += $(call try_cc,-Wno-logical-op)

tests += uref_dump_test.sh
uref_dump_test.sh-deps = uref_dump_test

test-targets += uref_dump_test
uref_dump_test-src = uref_dump_test.c
uref_dump_test-libs = libupipe

tests += uref_std_test
uref_std_test-src = uref_std_test.c
uref_std_test-libs = libupipe

tests += uref_uri_test.sh
uref_uri_test.sh-deps = uref_uri_test

test-targets += uref_uri_test
uref_uri_test-src = uref_uri_test.c
uref_uri_test-libs = libupipe

tests += ustring_test.sh
ustring_test.sh-deps = ustring_test

test-targets += ustring_test
ustring_test-src = ustring_test.c

tests += uuri_test
uuri_test-src = uuri_test.c
uuri_test-libs = libupipe

subdirs = checkasm

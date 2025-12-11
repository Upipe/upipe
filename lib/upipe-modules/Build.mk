lib-targets = libupipe_modules

libupipe_modules-desc = base modules

libupipe_modules-includes = \
    upipe_aes_decrypt.h \
    upipe_aggregate.h \
    upipe_audio_blank.h \
    upipe_audio_copy.h \
    upipe_audio_merge.h \
    upipe_audio_split.h \
    upipe_audiocont.h \
    upipe_auto_inner.h \
    upipe_auto_source.h \
    upipe_blank_source.h \
    upipe_blit.h \
    upipe_block_to_sound.h \
    upipe_buffer.h \
    upipe_burst.h \
    upipe_chunk_stream.h \
    upipe_convert_to_block.h \
    upipe_crop.h \
    upipe_dejitter.h \
    upipe_delay.h \
    upipe_discard_blocking.h \
    upipe_dtsdi.h \
    upipe_dump.h \
    upipe_dup.h \
    upipe_even.h \
    upipe_file_source.h \
    upipe_genaux.h \
    upipe_graph.h \
    upipe_grid.h \
    upipe_htons.h \
    upipe_http_source.h \
    upipe_idem.h \
    upipe_m3u_reader.h \
    upipe_match_attr.h \
    upipe_multicat_probe.h \
    upipe_multicat_sink.h \
    upipe_multicat_source.h \
    upipe_noclock.h \
    upipe_nodemux.h \
    upipe_ntsc_prepend.h \
    upipe_null.h \
    upipe_play.h \
    upipe_probe_uref.h \
    upipe_queue_sink.h \
    upipe_queue_source.h \
    upipe_rate_limit.h \
    upipe_row_join.h \
    upipe_row_split.h \
    upipe_rtp_h264.h \
    upipe_rtp_mpeg4.h \
    upipe_rtp_pcm_pack.h \
    upipe_rtp_pcm_unpack.h \
    upipe_segment_source.h \
    upipe_separate_fields.h \
    upipe_sequential_source.h \
    upipe_setattr.h \
    upipe_setflowdef.h \
    upipe_setrap.h \
    upipe_sine_wave_source.h \
    upipe_skip.h \
    upipe_stream_switcher.h \
    upipe_subpic_schedule.h \
    upipe_sync.h \
    upipe_time_limit.h \
    upipe_transfer.h \
    upipe_trickplay.h \
    upipe_udp_source.h \
    upipe_video_blank.h \
    upipe_videocont.h \
    upipe_void_source.h \
    upipe_worker.h \
    upipe_worker_linear.h \
    upipe_worker_sink.h \
    upipe_worker_source.h \
    uprobe_blit_prepare.h \
    uprobe_http_redirect.h \
    uref_aes_flow.h \
    uref_graph.h \
    uref_graph_flow.h \
    uref_http_flow.h

libupipe_modules-src = \
    http-parser/http_parser.c \
    http-parser/http_parser.h \
    http_source_hook.c \
    http_source_hook.h \
    upipe_aes_decrypt.c \
    upipe_aggregate.c \
    upipe_audio_blank.c \
    upipe_audio_copy.c \
    upipe_audio_merge.c \
    upipe_audio_split.c \
    upipe_audiocont.c \
    upipe_auto_inner.c \
    upipe_auto_source.c \
    upipe_blank_source.c \
    upipe_blit.c \
    upipe_block_to_sound.c \
    upipe_buffer.c \
    upipe_burst.c \
    upipe_chunk_stream.c \
    upipe_convert_to_block.c \
    upipe_crop.c \
    upipe_dejitter.c \
    upipe_delay.c \
    upipe_discard_blocking.c \
    upipe_dtsdi.c \
    upipe_dump.c \
    upipe_dup.c \
    upipe_even.c \
    upipe_file_source.c \
    upipe_genaux.c \
    upipe_graph.c \
    upipe_grid.c \
    upipe_htons.c \
    upipe_http_source.c \
    upipe_idem.c \
    upipe_m3u_reader.c \
    upipe_match_attr.c \
    upipe_multicat_probe.c \
    upipe_multicat_sink.c \
    upipe_multicat_source.c \
    upipe_noclock.c \
    upipe_nodemux.c \
    upipe_ntsc_prepend.c \
    upipe_null.c \
    upipe_play.c \
    upipe_probe_uref.c \
    upipe_queue.c \
    upipe_queue.h \
    upipe_queue_sink.c \
    upipe_queue_source.c \
    upipe_rate_limit.c \
    upipe_row_join.c \
    upipe_row_split.c \
    upipe_rtp_h264.c \
    upipe_rtp_mpeg4.c \
    upipe_rtp_pcm_pack.c \
    upipe_rtp_pcm_unpack.c \
    upipe_segment_source.c \
    upipe_separate_fields.c \
    upipe_sequential_source.c \
    upipe_setattr.c \
    upipe_setflowdef.c \
    upipe_setrap.c \
    upipe_sine_wave_source.c \
    upipe_skip.c \
    upipe_stream_switcher.c \
    upipe_subpic_schedule.c \
    upipe_sync.c \
    upipe_time_limit.c \
    upipe_transfer.c \
    upipe_trickplay.c \
    upipe_udp.c \
    upipe_udp.h \
    upipe_udp_source.c \
    upipe_video_blank.c \
    upipe_videocont.c \
    upipe_void_source.c \
    upipe_worker.c \
    uprobe_blit_prepare.c \
    uprobe_http_redirect.c

have_upipe_fsink          = $(have_writev)
have_upipe_udpsink        = $(have_writev)
have_upipe_id3v2          = $(have_bitstream)
have_upipe_id3v2_encaps   = $(have_bitstream)
have_upipe_id3v2_decaps   = $(have_bitstream)
have_upipe_rtcp           = $(have_bitstream)
have_upipe_rtpd           = $(have_bitstream)
have_upipe_rtp_anc_unpack = $(have_bitstream)
have_upipe_rtp_demux      = $(have_bitstream)
have_upipe_rtp_prepend    = $(have_bitstream)
have_upipe_rtpr           = $(have_bitstream)
have_upipe_rtpsrc         = $(have_bitstream)
have_upipe_s337_encaps    = $(have_bitstream)
have_upipe_vancd          = $(have_bitstream)

libupipe_modules-includes += \
    $(if $(have_upipe_fsink),upipe_file_sink.h) \
    $(if $(have_upipe_id3v2),upipe_id3v2.h) \
    $(if $(have_upipe_id3v2_encaps),upipe_id3v2_encaps.h) \
    $(if $(have_upipe_id3v2_decaps),upipe_id3v2_decaps.h) \
    $(if $(have_upipe_rtcp),upipe_rtcp.h) \
    $(if $(have_upipe_rtp_anc_unpack),upipe_rtp_anc_unpack.h) \
    $(if $(have_upipe_rtp_demux),upipe_rtp_demux.h) \
    $(if $(have_upipe_rtpd),upipe_rtp_decaps.h) \
    $(if $(have_upipe_rtp_prepend),upipe_rtp_prepend.h) \
    $(if $(have_upipe_rtpr),upipe_rtp_reorder.h) \
    $(if $(have_upipe_rtpsrc),upipe_rtp_source.h) \
    $(if $(have_upipe_s337_encaps),upipe_s337_encaps.h) \
    $(if $(have_upipe_udpsink),upipe_udp_sink.h) \
    $(if $(have_upipe_vancd),upipe_vanc_decoder.h)

libupipe_modules-src += \
    $(if $(have_upipe_fsink),upipe_file_sink.c) \
    $(if $(have_upipe_id3v2),upipe_id3v2.c) \
    $(if $(have_upipe_id3v2_encaps),upipe_id3v2_encaps.c) \
    $(if $(have_upipe_id3v2_decaps),upipe_id3v2_decaps.c) \
    $(if $(have_upipe_rtcp),upipe_rtcp.c) \
    $(if $(have_upipe_rtp_anc_unpack),upipe_rtp_anc_unpack.c) \
    $(if $(have_upipe_rtp_demux),upipe_rtp_demux.c) \
    $(if $(have_upipe_rtpd),upipe_rtp_decaps.c) \
    $(if $(have_upipe_rtp_prepend),upipe_rtp_prepend.c) \
    $(if $(have_upipe_rtpr),upipe_rtp_reorder.c) \
    $(if $(have_upipe_rtpsrc),upipe_rtp_source.c) \
    $(if $(have_upipe_s337_encaps),upipe_s337_encaps.c) \
    $(if $(have_upipe_udpsink),upipe_udp_sink.c) \
    $(if $(have_upipe_vancd),upipe_vanc_decoder.c)

libupipe_modules-ldlibs = -lm
libupipe_modules-libs = libupipe
libupipe_modules-opt-libs = bitstream

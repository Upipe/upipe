NULL =
lib_LTLIBRARIES = libupipe_modules.la

libupipe_modules_la_SOURCES = \
	upipe_file_source.c \
	upipe_transfer.c \
	upipe_play.c \
	upipe_trickplay.c \
	upipe_even.c \
	upipe_dup.c \
	upipe_idem.c \
	upipe_null.c \
	upipe_queue.c \
	upipe_queue.h \
	upipe_queue_source.c \
	upipe_queue_sink.c \
	upipe_udp_source.c \
	upipe_udp.c \
	upipe_udp.h \
	http_source_hook.c \
	http_source_hook.h \
	upipe_http_source.c \
	http-parser/http_parser.c \
	http-parser/http_parser.h \
	upipe_genaux.c \
	upipe_multicat_source.c \
	upipe_multicat_sink.c \
	upipe_multicat_probe.c \
	upipe_probe_uref.c \
	upipe_noclock.c \
	upipe_nodemux.c \
	upipe_dejitter.c \
	upipe_delay.c \
	upipe_skip.c \
	upipe_aggregate.c \
	upipe_convert_to_block.c \
	upipe_htons.c \
	upipe_chunk_stream.c \
	upipe_setflowdef.c \
	upipe_setattr.c \
	upipe_setrap.c \
	upipe_match_attr.c \
	upipe_blit.c \
	uprobe_blit_prepare.c \
	upipe_crop.c \
	upipe_audio_split.c \
	upipe_videocont.c \
	upipe_audiocont.c \
	upipe_blank_source.c \
	upipe_sine_wave_source.c \
	upipe_worker.c \
	upipe_stream_switcher.c \
	upipe_rtp_h264.c \
	upipe_rtp_mpeg4.c \
	upipe_dump.c \
	uprobe_http_redirect.c \
	upipe_m3u_reader.c \
	upipe_auto_source.c \
	upipe_buffer.c \
	upipe_aes_decrypt.c \
	upipe_rate_limit.c \
	upipe_time_limit.c \
	upipe_burst.c \
	upipe_sequential_source.c \
	upipe_segment_source.c \
	upipe_subpic_schedule.c \
	upipe_ntsc_prepend.c \
	upipe_rtp_pcm_unpack.c \
	upipe_rtp_pcm_pack.c \
	upipe_dtsdi.c \
	upipe_void_source.c \
	upipe_video_blank.c \
	upipe_audio_blank.c \
	upipe_grid.c \
	upipe_sync.c \
	upipe_block_to_sound.c \
	upipe_audio_copy.c \
	upipe_auto_inner.c \
	upipe_row_split.c \
	upipe_separate_fields.c \
	upipe_row_join.c \
	upipe_discard_blocking.c \
	upipe_audio_merge.c \
	upipe_graph.c \
	$(NULL)

if HAVE_WRITEV
libupipe_modules_la_SOURCES += \
	upipe_file_sink.c \
	upipe_udp_sink.c
endif

if HAVE_BITSTREAM
libupipe_modules_la_SOURCES += \
	upipe_rtp_decaps.c \
	upipe_rtp_prepend.c \
	upipe_rtp_source.c \
	upipe_rtp_demux.c \
	upipe_rtcp.c \
	upipe_rtp_reorder.c \
	upipe_s337_encaps.c \
	upipe_vanc_decoder.c \
	upipe_id3v2_decaps.c \
	upipe_id3v2_encaps.c \
	upipe_id3v2.c \
	$(NULL)
libupipe_modules_la_CFLAGS = $(AM_CFLAGS) $(BITSTREAM_CFLAGS)
endif

libupipe_modules_la_CPPFLAGS = -I$(top_builddir)/include -I$(top_srcdir)/include
libupipe_modules_la_LIBADD = -lm $(top_builddir)/lib/upipe/libupipe.la

libupipe_modules_la_LDFLAGS = -no-undefined

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libupipe_modules.pc

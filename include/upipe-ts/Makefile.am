NULL =
myincludedir = $(includedir)/upipe-ts
myinclude_HEADERS = \
	upipe_ts.h \
	upipe_ts_align.h \
	upipe_ts_check.h \
	upipe_ts_decaps.h \
	upipe_ts_demux.h \
	upipe_ts_encaps.h \
	upipe_ts_pcr_interpolator.h \
	upipe_ts_mux.h \
	upipe_ts_eit_decoder.h \
	upipe_ts_nit_decoder.h \
	upipe_ts_cat_decoder.h \
	upipe_ts_pat_decoder.h \
	upipe_ts_pes_decaps.h \
	upipe_ts_pes_encaps.h \
	upipe_ts_pid_filter.h \
	upipe_ts_pmt_decoder.h \
	upipe_ts_psi_generator.h \
	upipe_ts_psi_join.h \
	upipe_ts_psi_merge.h \
	upipe_ts_psi_split.h \
	upipe_ts_scte104_decoder.h \
	upipe_ts_scte35_decoder.h \
	upipe_ts_scte35_generator.h \
	upipe_ts_scte35_probe.h \
	upipe_ts_sdt_decoder.h \
	upipe_ts_si_generator.h \
	upipe_ts_tdt_decoder.h \
	upipe_ts_split.h \
	upipe_ts_sync.h \
	upipe_ts_tstd.h \
	upipe_rtp_fec.h \
	uref_ts_attr.h \
	uref_ts_event.h \
	uref_ts_flow.h \
	uref_ts_scte104_flow.h \
	uref_ts_scte35.h \
	uref_ts_scte35_desc.h \
	$(NULL)

if HAVE_GCRYPT
if HAVE_TASN1
myinclude_HEADERS += upipe_ts_emm_decoder.h
endif
endif

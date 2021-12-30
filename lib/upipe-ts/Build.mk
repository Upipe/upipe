lib-targets = libupipe_ts

libupipe_ts-desc = MPEG-2 transport stream modules

libupipe_ts-includes = \
    upipe_rtp_fec.h \
    upipe_ts.h \
    upipe_ts_ait_decoder.h \
    upipe_ts_ait_generator.h \
    upipe_ts_align.h \
    upipe_ts_cat_decoder.h \
    upipe_ts_check.h \
    upipe_ts_decaps.h \
    upipe_ts_demux.h \
    upipe_ts_eit_decoder.h \
    upipe_ts_encaps.h \
    upipe_ts_metadata_generator.h \
    upipe_ts_mux.h \
    upipe_ts_nit_decoder.h \
    upipe_ts_pat_decoder.h \
    upipe_ts_pcr_interpolator.h \
    upipe_ts_pes_decaps.h \
    upipe_ts_pes_encaps.h \
    upipe_ts_pid_filter.h \
    upipe_ts_pmt_decoder.h \
    upipe_ts_psi_generator.h \
    upipe_ts_psi_join.h \
    upipe_ts_psi_merge.h \
    upipe_ts_psi_split.h \
    upipe_ts_scte104_decoder.h \
    upipe_ts_scte104_generator.h \
    upipe_ts_scte35_decoder.h \
    upipe_ts_scte35_generator.h \
    upipe_ts_scte35_probe.h \
    upipe_ts_sdt_decoder.h \
    upipe_ts_si_generator.h \
    upipe_ts_split.h \
    upipe_ts_sync.h \
    upipe_ts_tdt_decoder.h \
    upipe_ts_tot_decoder.h \
    upipe_ts_tstd.h \
    uref_ts_attr.h \
    uref_ts_event.h \
    uref_ts_flow.h \
    uref_ts_scte104_flow.h \
    uref_ts_scte35.h \
    uref_ts_scte35_desc.h

libupipe_ts-src = \
    upipe_rtp_fec.c \
    upipe_ts_ait_decoder.c \
    upipe_ts_ait_generator.c \
    upipe_ts_align.c \
    upipe_ts_cat_decoder.c \
    upipe_ts_check.c \
    upipe_ts_decaps.c \
    upipe_ts_demux.c \
    upipe_ts_eit_decoder.c \
    upipe_ts_encaps.c \
    upipe_ts_metadata_generator.c \
    upipe_ts_mux.c \
    upipe_ts_nit_decoder.c \
    upipe_ts_pat_decoder.c \
    upipe_ts_pcr_interpolator.c \
    upipe_ts_pes_decaps.c \
    upipe_ts_pes_encaps.c \
    upipe_ts_pid_filter.c \
    upipe_ts_pmt_decoder.c \
    upipe_ts_psi_decoder.h \
    upipe_ts_psi_generator.c \
    upipe_ts_psi_join.c \
    upipe_ts_psi_merge.c \
    upipe_ts_psi_split.c \
    upipe_ts_scte104_decoder.c \
    upipe_ts_scte104_generator.c \
    upipe_ts_scte35_decoder.c \
    upipe_ts_scte35_generator.c \
    upipe_ts_scte35_probe.c \
    upipe_ts_sdt_decoder.c \
    upipe_ts_si_generator.c \
    upipe_ts_split.c \
    upipe_ts_sync.c \
    upipe_ts_tdt_decoder.c \
    upipe_ts_tot_decoder.c \
    upipe_ts_tstd.c \
    uref_ts_scte35.c

configs += libiconv
libiconv-ldlibs = -liconv

configs += ts-crypt
ts-crypt-libs = libgcrypt libtasn1

libupipe_ts-includes += \
    $(if $(have_ts-crypt),upipe_ts_emm_decoder.h)

libupipe_ts-src += \
    $(if $(have_ts-crypt),rsa_asn1.c rsa_asn1.h upipe_ts_emm_decoder.c)

libupipe_ts-libs = libupipe libupipe_modules bitstream
libupipe_ts-opt-libs = libiconv $(if $(have_ts-crypt),libgcrypt libtasn1)

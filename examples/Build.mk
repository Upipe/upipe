noinst-targets += alsaplay
alsaplay-src = alsaplay.c
alsaplay-deps = upipe_avcdec
alsaplay-libs = libupipe libupipe_modules libupipe_filters libupipe_av \
                libupipe_swresample libupipe_alsa libupump_ev libupipe_framers

noinst-targets += blackmagic
blackmagic-src = blackmagic.c
blackmagic-deps = upipe_avcenc
blackmagic-libs = libupipe libupipe_modules libupipe_blackmagic libupipe_av \
                  libupipe_swscale libupipe_swresample libupipe_filters \
                  libupump_ev

noinst-targets += decrypt
decrypt-src = decrypt.c
decrypt-deps = upipe_fsink
decrypt-libs = libupipe libupipe_modules libupump_ev

noinst-targets += dvbsrc
dvbsrc-src = dvbsrc.c
dvbsrc-deps = upipe_udpsink
dvbsrc-libs = libupipe libupipe_modules libupipe_dvb libupump_ev

noinst-targets += extract_pic
extract_pic-src = extract_pic.c
extract_pic-deps = upipe_avcdec upipe_fsink
extract_pic-libs = libupipe libupipe_modules libupipe_filters libupipe_framers \
                   libupipe_ts libupipe_av libupipe_swscale libupump_ev

noinst-targets += fec
fec-src = fec.c
fec-deps = upipe_udpsink
fec-libs = libupipe libupipe_modules libupipe_ts libupump_ev

noinst-targets += frame
frame-src = frame.c
frame-libs = libupipe libupipe_modules libupipe_filters libupipe_framers \
             libupipe_ts libupipe_av libupump_ev

noinst-targets += glxplay
glxplay-src = glxplay.c
glxplay-deps = upipe_avcdec
glxplay-libs = libupipe libupipe_modules libupipe_filters libupipe_framers \
               libupipe_ts libupipe_gl libupipe_pthread libupipe_av \
               libupipe_swscale libupump_ev pthread

noinst-targets += grid
grid-src = grid.c
grid-deps = upipe_avcdec upipe_udpsink
grid-cppflags = $(cppflags_libswscale)
grid-libs = libupipe libupipe_modules libupipe_filters libupipe_framers \
            libupipe_ts libupipe_pthread libupipe_x264 libupipe_av \
            libupipe_swscale libupipe_swresample libupump_ev

noinst-targets += hls2rtp
hls2rtp-src = hls2rtp.c
hls2rtp-deps = upipe_fsink upipe_rtp_prepend
hls2rtp-opt-libs = libupipe_bearssl libupipe_openssl
hls2rtp-libs = libupipe libupipe_modules libupipe_hls libupipe_pthread \
               libupipe_ts libupump_ev

noinst-targets += mpthree2rtp
mpthree2rtp-src = mpthree2rtp.c
mpthree2rtp-deps = upipe_rtp_prepend upipe_udpsink
mpthree2rtp-libs = libupipe libupipe_modules libupipe_framers libupipe_av \
                   libupipe_swresample libupump_ev

noinst-targets += multicatudp
multicatudp-src = multicatudp.c
multicatudp-deps = upipe_fsink
multicatudp-libs = libupipe libupipe_modules libupipe_pthread libupump_ev

noinst-targets += pcap
pcap-src = pcap.c
pcap-libs = libupipe libupipe_modules libupipe_pcap libupump_ev

noinst-targets += rist_rx
rist_rx-src = rist_rx.c
rist_rx-deps = upipe_udpsink upipe_rtpfb
rist_rx-libs = libupipe libupipe_modules libupipe_filters libupump_ev

noinst-targets += rist_tx
rist_tx-src = rist_tx.c
rist_tx-deps = upipe_rtcpfb upipe_udpsink
rist_tx-libs = libupipe libupipe_modules libupipe_filters libupump_ev bitstream

noinst-targets += transcode
transcode-src = transcode.c
transcode-deps = upipe_avcdec upipe_avcenc upipe_avfilt
transcode-libs = libupipe libupipe_modules libupipe_framers libupipe_filters \
                 libupipe_av libupipe_swscale libupipe_swresample libupump_ev

noinst-targets += ts2es
ts2es-src = ts2es.c
ts2es-deps = upipe_fsink
ts2es-libs = libupipe libupipe_modules libupipe_ts libupipe_framers libupump_ev

noinst-targets += ts2mpthreemulticat
ts2mpthreemulticat-src = ts2mpthreemulticat.c
ts2mpthreemulticat-deps = upipe_rtpsrc upipe_fsink
ts2mpthreemulticat-libs = libupipe libupipe_modules libupipe_ts \
                          libupipe_framers libupump_ev bitstream

noinst-targets += ts_encrypt
ts_encrypt-src = ts_encrypt.c
ts_encrypt-deps = upipe_rtpsrc upipe_udpsink
ts_encrypt-opt-libs = libgcrypt
ts_encrypt-libs = libupipe libupipe_modules libupipe_dvbcsa libupipe_pthread \
                  libupipe_ts libupump_ev

noinst-targets += udpmulticat
udpmulticat-src = udpmulticat.c
udpmulticat-deps = upipe_fsink
udpmulticat-libs = libupipe libupipe_modules libupump_ev

noinst-targets += upipe_duration
upipe_duration-src = upipe_duration.c
upipe_duration-libs = libupipe libupipe_modules libupipe_ts libupipe_framers \
                      libupump_ev

noinst-targets += uplay
uplay-src = uplay.c
uplay-deps = upipe_rtpsrc upipe_avcdec
uplay-opt-libs = libupipe_alsa
uplay-libs = libupipe libupipe_modules libupipe_filters libupipe_framers \
             libupipe_ts libupipe_pthread libupipe_av libupipe_swscale \
             libupipe_swresample libupipe_gl libupump_ev

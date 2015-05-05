#ifndef RTCP_H_
# define RTCP_H_

# include <inttypes.h>

# define RTCP_RTP_VERSION     2
# define RTCP_PT_SR      200

static inline void rtcp_sr_set_rtp_version(uint8_t *p_rtcp_sr)
{
    p_rtcp_sr[0] = RTCP_RTP_VERSION << 6;
}

static inline void rtcp_sr_set_pt(uint8_t *p_rtcp_sr)
{
    p_rtcp_sr[1] = RTCP_PT_SR;
}

static inline void rtcp_sr_set_length(uint8_t *p_rtcp_sr,
                                      uint16_t length)
{
    p_rtcp_sr[2] = length >> 8;
    p_rtcp_sr[3] = length & 0xff;
}

static inline void rtcp_sr_set_ntp_time_msw(uint8_t *p_rtcp_sr,
                                            uint32_t ntp_time_msw)
{
    p_rtcp_sr[8] = (ntp_time_msw >> 24) & 0xff;
    p_rtcp_sr[9] = (ntp_time_msw >> 16) & 0xff;
    p_rtcp_sr[10] = (ntp_time_msw >> 8) & 0xff;
    p_rtcp_sr[11] = ntp_time_msw & 0xff;
}

static inline void rtcp_sr_set_ntp_time_lsw(uint8_t *p_rtcp_sr,
                                            uint32_t ntp_time_lsw)
{
    p_rtcp_sr[12] = (ntp_time_lsw >> 24) & 0xff;
    p_rtcp_sr[13] = (ntp_time_lsw >> 16) & 0xff;
    p_rtcp_sr[14] = (ntp_time_lsw >> 8) & 0xff;
    p_rtcp_sr[15] = ntp_time_lsw & 0xff;
}

static inline void rtcp_sr_set_rtp_time(uint8_t *p_rtcp_sr,
                                            uint32_t rtp_time)
{
    p_rtcp_sr[16] = (rtp_time >> 24) & 0xff;
    p_rtcp_sr[17] = (rtp_time >> 16) & 0xff;
    p_rtcp_sr[18] = (rtp_time >> 8) & 0xff;
    p_rtcp_sr[19] = rtp_time & 0xff;
}

static inline void rtcp_sr_set_packet_count(uint8_t *p_rtcp_sr,
                                            uint32_t packet_count)
{
    p_rtcp_sr[20] = (packet_count >> 24) & 0xff;
    p_rtcp_sr[21] = (packet_count >> 16) & 0xff;
    p_rtcp_sr[22] = (packet_count >> 8) & 0xff;
    p_rtcp_sr[23] = packet_count & 0xff;
}

static inline void rtcp_sr_set_octet_count(uint8_t *p_rtcp_sr,
                                            uint32_t octet_count)
{
    p_rtcp_sr[24] = (octet_count >> 24) & 0xff;
    p_rtcp_sr[25] = (octet_count >> 16) & 0xff;
    p_rtcp_sr[26] = (octet_count >> 8) & 0xff;
    p_rtcp_sr[27] = octet_count & 0xff;
}


#endif /* !RTCP_H_ */

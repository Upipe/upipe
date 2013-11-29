/*
 * Copyright (C) 2012-2013 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short Upipe internal helper functions for udp modules
 */

#include <upipe/upipe.h>
#include <stdint.h>

#define IP_HEADER_MINSIZE 20
#define UDP_HEADER_SIZE 8
#define RAW_HEADER_SIZE (IP_HEADER_MINSIZE + UDP_HEADER_SIZE)

static inline void ip_set_version(uint8_t *p_ip, uint8_t version)
{
    p_ip[0] &= ~0xf0;
    p_ip[0] |= (version & 0xf) << 4;
}

static inline void ip_set_ihl(uint8_t *p_ip, uint8_t ihl)
{
    p_ip[0] &= ~0xf;
    p_ip[0] |= (ihl & 0xf);
}

static inline void ip_set_tos(uint8_t *p_ip, uint8_t tos)
{
    p_ip[1] = tos;
}

static inline void ip_set_len(uint8_t *p_ip, uint16_t len)
{
    p_ip[2] = (len & 0xff00) >> 8;
    p_ip[3] = (len & 0xff);
}

static inline void ip_set_id(uint8_t *p_ip, uint16_t id)
{
    
    p_ip[4] = (id & 0xff00) >> 8;
    p_ip[5] = (id & 0xff);
}

static inline void ip_set_flag_reserved(uint8_t *p_ip, uint8_t flag)
{
    p_ip[6] &= ~0x80;
    p_ip[6] |= (flag & 1) << 7;
}

static inline void ip_set_flag_df(uint8_t *p_ip, uint8_t flag)
{
    p_ip[6] &= ~0x40;
    p_ip[6] |= (flag & 1) << 6;
}

static inline void ip_set_flag_mf(uint8_t *p_ip, uint8_t flag)
{
    p_ip[6] &= ~0x20;
    p_ip[6] |= (flag & 1) << 5;
}

static inline void ip_set_frag_offset(uint8_t *p_ip, uint16_t offset)
{
    p_ip[6] &= ~0x1f;
    p_ip[6] |= (offset & 0x1f00) >> 8;
    p_ip[7] = (offset & 0xff);
}

static inline void ip_set_ttl(uint8_t *p_ip, uint8_t ttl)
{
    p_ip[8] = ttl;
}

static inline void ip_set_proto(uint8_t *p_ip, uint8_t proto)
{
    p_ip[9] = proto;
}

static inline void ip_set_cksum(uint8_t *p_ip, uint16_t cksum)
{
    p_ip[10] = (cksum & 0xff00) >> 8;
    p_ip[11] = (cksum & 0xff);
}

static inline void ip_set_srcaddr(uint8_t *p_ip, uint32_t addr)
{
    p_ip[12] = (addr & 0xff000000) >> 24;
    p_ip[13] = (addr & 0x00ff0000) >> 16;
    p_ip[14] = (addr & 0x0000ff00) >>  8;
    p_ip[15] = (addr & 0x000000ff);
}

static inline void ip_set_dstaddr(uint8_t *p_ip, uint32_t addr)
{
    p_ip[16] = (addr & 0xff000000) >> 24;
    p_ip[17] = (addr & 0x00ff0000) >> 16;
    p_ip[18] = (addr & 0x0000ff00) >>  8;
    p_ip[19] = (addr & 0x000000ff);
}

static inline void udp_set_srcport(uint8_t *p_ip, uint16_t port)
{
    p_ip[0] = (port & 0xff00) >> 8;
    p_ip[1] = (port & 0xff);
}

static inline void udp_set_dstport(uint8_t *p_ip, uint16_t port)
{
    p_ip[2] = (port & 0xff00) >> 8;
    p_ip[3] = (port & 0xff);
}

static inline void udp_set_len(uint8_t *p_ip, uint16_t len)
{
    p_ip[4] = (len & 0xff00) >> 8;
    p_ip[5] = (len & 0xff);
}

static inline void udp_set_cksum(uint8_t *p_ip, uint16_t cksum)
{
    p_ip[6] = (cksum & 0xff00) >> 8;
    p_ip[7] = (cksum & 0xff);
}

/** @internal @This parses _uri and opens IPv4 & IPv6 sockets
 *
 * @param upipe description structure of the pipe
 * @param _uri socket URI
 * @param ttl packets time-to-live
 * @param bind_port bind port
 * @param connect_port connect port
 * @param weight weight (UNUSED)
 * @param use_tcp Set this to open a tcp socket (instead of udp)
 * @param use_raw open RAW socket (udp)
 * @param raw_header user-provided buffer for RAW header (ip+udp)
 * @return socket fd, or -1 in case of error
 */
int upipe_udp_open_socket(struct upipe *upipe, const char *_uri, int ttl,
                          uint16_t bind_port, uint16_t connect_port,
                          unsigned int *weight, bool *use_tcp,
                          bool *use_raw, uint8_t *raw_header);

void udp_raw_set_len(uint8_t *raw_header, uint16_t len);


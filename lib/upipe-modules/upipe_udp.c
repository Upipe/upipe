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

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#ifdef UPIPE_HAVE_NET_IF_H
#include <net/if.h>
#endif
#include "upipe_udp.h"

/** union sockaddru: wrapper to avoid strict-aliasing issues */
union sockaddru
{
    struct sockaddr_storage ss;
    struct sockaddr so;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
};

/** @internal @This fills ipv4/udp headers for RAW sockets
 *
 * @param upipe description structure of the pipe
 * @param dgram raw datagram
 * @param ipsrc source ip address
 * @param ipdst destination ip address
 * @param portsrc source port address
 * @param portdst destination port address
 * @param ttl datagram time-to-live
 * @param tos type of service
 * @param payload length
 */
static void upipe_udp_raw_fill_headers(struct upipe *upipe,
                                       uint8_t *header,
                                       in_addr_t ipsrc, in_addr_t ipdst,
                                       uint16_t portsrc, uint16_t portdst,
                                       uint8_t ttl, uint8_t tos, uint16_t len)
{
    ip_set_version(header, 4);
    ip_set_ihl(header, 5);
    ip_set_tos(header, tos);
    ip_set_len(header, len + UDP_HEADER_SIZE + IP_HEADER_MINSIZE);
    ip_set_id(header, 0);
    ip_set_flag_reserved(header, 0);
    ip_set_flag_mf(header, 0);
    ip_set_flag_df(header, 0);
    ip_set_frag_offset(header, 0);
    ip_set_ttl(header, ttl);
    ip_set_proto(header, IPPROTO_UDP);
    ip_set_cksum(header, 0);
    ip_set_srcaddr(header, ntohl(ipsrc));
    ip_set_dstaddr(header, ntohl(ipdst));

    header += IP_HEADER_MINSIZE;
    udp_set_srcport(header, portsrc);
    udp_set_dstport(header, portdst);
    udp_set_len(header, len + UDP_HEADER_SIZE);
    udp_set_cksum(header, 0);
}

void udp_raw_set_len(uint8_t *raw_header, uint16_t len)
{
    uint16_t iplen = len + UDP_HEADER_SIZE + IP_HEADER_MINSIZE;
    #if defined(__NetBSD__) || defined(__FreeBSD__) || defined(__APPLE__)
    iplen = htons(iplen);
    #endif
    ip_set_len(raw_header, iplen);
    raw_header += IP_HEADER_MINSIZE;
    udp_set_len(raw_header, len + UDP_HEADER_SIZE);
}

/** @internal @This returns the index of an interface
 *
 * @param upipe description structure of the pipe
 * @param name interface name
 * @return interface index
 */
static bool upipe_udp_get_ifindex(struct upipe *upipe, const char *name, int *ifrindex)
{
#if !defined(__APPLE__) && !defined(__native_client__)
    int fd;
    struct ifreq ifr;

    if (! (name && ifrindex && upipe) ) {
        return false;
    }

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        upipe_err_va(upipe, "unable to open socket (%m)");
        return false;
    }

    strncpy(ifr.ifr_name, name, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ-1] = '\0';

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        upipe_err_va(upipe, "unable to get interface index (%m)");
        close(fd);
        return false;
    }

    close(fd);

    *ifrindex = ifr.ifr_ifindex;
    return true;
#else
    upipe_err_va(upipe, "unable to get interface index (%m)");
    return false;
#endif
}

/** @internal @This prints socket characteristics for debug purposes
 *
 * @param upipe description structure of the pipe
 * @param text descriptive text
 * @param bind bind sockaddr union
 * @param connect connect sockaddr union
 */
static void upipe_udp_print_socket(struct upipe *upipe, const char *text, union sockaddru *bind,
                         union sockaddru *connect)
{
    if (bind->ss.ss_family == AF_INET) {
        upipe_dbg_va(upipe, "%s bind:%s:%u", text,
                     inet_ntoa(bind->sin.sin_addr), ntohs(bind->sin.sin_port));
    } else if (bind->ss.ss_family == AF_INET6) {
        char buf[INET6_ADDRSTRLEN];
        upipe_dbg_va(upipe, "%s bind:[%s]:%u", text,
                     inet_ntop(AF_INET6, &bind->sin6.sin6_addr, buf,
                               sizeof(buf)),
                     ntohs(bind->sin6.sin6_port));
    }

    if (connect->ss.ss_family == AF_INET) {
        upipe_dbg_va(upipe, "%s connect:%s:%u", text,
                     inet_ntoa(connect->sin.sin_addr),
                     ntohs(connect->sin.sin_port));
    } else if (connect->ss.ss_family == AF_INET6) {
        char buf[INET6_ADDRSTRLEN];
        upipe_dbg_va(upipe, "%s connect:[%s]:%u", text,
                     inet_ntop(AF_INET6, &connect->sin6.sin6_addr, buf,
                               sizeof(buf)),
                     ntohs(connect->sin6.sin6_port));
    }
}

/** @internal @This parses a host:port string
 *
 * @param upipe description structure of the pipe
 * @param _string string to be parsed
 * @param stringend end of string pointer
 * @param default_port default port
 * @param if_index interface index
 */
static bool upipe_udp_parse_node_service(struct upipe *upipe,
                                          char *_string, char **stringend,
                                          uint16_t default_port,
                                          int *if_index,
                                          struct sockaddr_storage *ss)
{
    int family = AF_INET;
    char port_buffer[6];
    char *string = strdup(_string);
    char *node, *port = NULL, *end;
    struct addrinfo *res = NULL;
    struct addrinfo hint;
    int ret;

    if (string[0] == '[') {
        family = AF_INET6;
        node = string + 1;
        end = strchr(node, ']');
        if (end == NULL) {
            upipe_warn_va(upipe, "invalid IPv6 address %s", _string);
            free(string);
            return false;
        }
        *end++ = '\0';

        char *intf = strrchr(node, '%');
        if (intf != NULL) {
            *intf++ = '\0';
            if (if_index != NULL) {
                if (!upipe_udp_get_ifindex(upipe, intf, if_index)) {
                    free(string);
                    return false;
                };
            }
        }
    } else {
        node = string;
        end = strpbrk(string, "@:,/");
    }

    if (end != NULL && end[0] == ':') {
        *end++ = '\0';
        port = end;
        end = strpbrk(port, "@:,/");
    }

    if (end != NULL) {
        *end = '\0';
        if (stringend != NULL)
            *stringend = _string + (end - string);
    } else if (stringend != NULL) {
        *stringend = _string + strlen(_string);
    }

    if (default_port != 0 && (port == NULL || !*port)) {
        sprintf(port_buffer, "%u", default_port);
        port = port_buffer;
    }

    if (node[0] == '\0') {
        node = "0.0.0.0";
    }

    if (family != AF_INET6) {
        /* Give a try to inet_aton because experience shows that getaddrinfo()
         * fails in certain cases, like when network is down. */
        struct in_addr addr;
        if (inet_aton(node, &addr) != 0) {
            struct sockaddr_in sin;
            memset(&sin, 0, sizeof (struct sockaddr_in));
            sin.sin_family = AF_INET;
            sin.sin_port = port != NULL ? ntohs(atoi(port)) : 0;
            sin.sin_addr = addr;
            memcpy(ss, &sin, sizeof(struct sockaddr_in));
            free(string);
            return true;
        }
    }

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = family;
    hint.ai_socktype = SOCK_DGRAM;
    hint.ai_protocol = 0;
    hint.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV | AI_ADDRCONFIG;
    if ((ret = getaddrinfo(node, port, &hint, &res)) != 0) {
        //upipe_warn_va(upipe, "getaddrinfo error: %s", gai_strerror(ret));
        free(string);
        return false;
    }

    memcpy(ss, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    free(string);
    return true;
}

/** @internal @This is a helper for @ref upipe_udp_open_socket.
 *
 * @param psz_string option string
 * @return duplicated string
 */
static char *config_stropt(char *psz_string)
{
    char *ret, *tmp;
    if (!psz_string || strlen(psz_string) == 0)
        return NULL;
    ret = tmp = strdup(psz_string);
    while (*tmp) {
        if (*tmp == '_')
            *tmp = ' ';
        if (*tmp == '/') {
            *tmp = '\0';
            break;
        }
        tmp++;
    }
    return ret;
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
                          bool *use_raw, uint8_t *raw_header)
{
    union sockaddru bind_addr, connect_addr;
    int fd, i;
    char *uri = strdup(_uri);
    char *token = uri;
    char *token2 = NULL;
    int bind_if_index = 0, connect_if_index = 0;
    in_addr_t if_addr = INADDR_ANY;
    in_addr_t src_addr = INADDR_ANY;
    uint16_t src_port = 4242;
    int tos = 0;
    bool b_tcp;
    bool b_raw;
    int family;
    socklen_t sockaddr_len;
#if !defined(__APPLE__) && !defined(__native_client__)
    char *ifname = NULL;
#endif

    if (!uri)
        return -1;

    memset(&bind_addr, 0, sizeof(union sockaddru));
    memset(&connect_addr, 0, sizeof(union sockaddru));

    bind_addr.ss.ss_family = AF_UNSPEC;
    connect_addr.ss.ss_family = AF_UNSPEC;

    if (use_tcp == NULL) {
        use_tcp = &b_tcp;
    }
    *use_tcp = false;
    if (use_raw == NULL) {
        use_raw = &b_raw;
    }
    *use_raw = false;

    token2 = strrchr(uri, ',');
    if (token2) {
        *token2++ = '\0';
        if (weight) {
            *weight = strtoul(token2, NULL, 0);
        }
    } else if (weight) {
        *weight = 1;
    }

    token2 = strchr(uri, '/');
    if (token2) {
        *token2 = '\0';
    }

    if (*token == '\0') {
        free(uri);
        return -1;
    }

    /* Hosts */
    if (token[0] != '@') {
        if (!upipe_udp_parse_node_service(upipe, token, &token, connect_port,
                                        &connect_if_index, &connect_addr.ss)) {
            free(uri);
            return -1;
        }
        /* required on some architectures */
        memset(&connect_addr.sin.sin_zero, 0, sizeof(connect_addr.sin.sin_zero));
    }

    if (token[0] == '@') {
        token++;
        if (!upipe_udp_parse_node_service(upipe, token, &token, bind_port,
                                        &bind_if_index, &bind_addr.ss)) {
            free(uri);
            return -1;
        }
        /* required on some architectures */
        memset(&bind_addr.sin.sin_zero, 0, sizeof(bind_addr.sin.sin_zero));
    }

    if (bind_addr.ss.ss_family == AF_UNSPEC &&
         connect_addr.ss.ss_family == AF_UNSPEC) {
        free(uri);
        return -1;
    }

    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);

    /* Weights and options */
    if (token2) {
        do {
            *token2++ = '\0';
#define IS_OPTION(option) (!strncasecmp(token2, option, strlen(option)))
#define ARG_OPTION(option) (token2 + strlen(option))
            if (IS_OPTION("ifindex=")) {
                bind_if_index = connect_if_index =
                    strtol(ARG_OPTION("ifindex="), NULL, 0);
            } else if (IS_OPTION("ifaddr=")) {
                char *option = config_stropt(ARG_OPTION("ifaddr="));
                if_addr = inet_addr(option);
                free( option );
#if !defined(__APPLE__) && !defined(__native_client__)
            } else if ( IS_OPTION("ifname=") ) {
                ifname = config_stropt( ARG_OPTION("ifname=") );
                if (strlen(ifname) >= IFNAMSIZ) {
                    ifname[IFNAMSIZ-1] = '\0';
                }
#endif
            } else if (IS_OPTION("srcaddr=")) {
                char *option = config_stropt(ARG_OPTION("srcaddr="));
                src_addr = inet_addr(option);
                free(option);
                *use_raw = true;
            } else if (IS_OPTION("srcport=")) {
                src_port = strtol(ARG_OPTION("srcport="), NULL, 0);
            } else if (IS_OPTION("ttl=")) {
                ttl = strtol(ARG_OPTION("ttl="), NULL, 0);
            } else if (IS_OPTION("tos=")) {
                tos = strtol(ARG_OPTION("tos="), NULL, 0);
            } else if (IS_OPTION("tcp")) {
                *use_tcp = true;
            } else {
                upipe_warn_va(upipe, "unrecognized option %s", token2);
            }
#undef IS_OPTION
#undef ARG_OPTION
        } while ((token2 = strchr(token2, '/')) != NULL);
    }

    if (unlikely(*use_tcp && *use_raw)) {
        upipe_warn(upipe, "RAW sockets not implemented for tcp");
        free(uri);
        return -1;
    }

    free(uri);

    /* Sanity checks */
    if (bind_addr.ss.ss_family != AF_UNSPEC
          && connect_addr.ss.ss_family != AF_UNSPEC
          && bind_addr.ss.ss_family != connect_addr.ss.ss_family) {
        upipe_err(upipe, "incompatible address types");
        return -1;
    }
    if (bind_addr.ss.ss_family != AF_UNSPEC) {
        family = bind_addr.ss.ss_family;
    } else if (connect_addr.ss.ss_family != AF_UNSPEC) {
        family = connect_addr.ss.ss_family;
    } else {
        upipe_err(upipe, "ambiguous address declaration");
        return -1;
    }
    sockaddr_len = (family == AF_INET) ? sizeof(struct sockaddr_in) :
                     sizeof(struct sockaddr_in6);

    if (bind_if_index && connect_if_index
          && bind_if_index != connect_if_index) {
        upipe_err(upipe, "incompatible bind and connect interfaces");
        return -1;
    }
    if (connect_if_index) bind_if_index = connect_if_index;
    else connect_if_index = bind_if_index;

    /* RAW header */
    if (*use_raw && raw_header) {
        upipe_udp_raw_fill_headers(upipe, raw_header,
                src_addr, connect_addr.sin.sin_addr.s_addr, src_port,
                ntohs(connect_addr.sin.sin_port), ttl, tos, 0);
    }


    /* Socket configuration */
    int sock_type = SOCK_DGRAM;
    if (*use_tcp) sock_type = SOCK_STREAM;
    if (*use_raw) sock_type = SOCK_RAW;
    int sock_proto = (*use_raw ? IPPROTO_RAW : 0);

    if ((fd = socket(family, sock_type, sock_proto)) < 0) {
        upipe_err_va(upipe, "unable to open socket (%m)");
        return -1;
    }
    #if !defined(__APPLE__) && !defined(__native_client__)
    if (*use_raw) {
        int hincl = 1;
        if (setsockopt(fd, IPPROTO_IP, IP_HDRINCL, &hincl, sizeof(hincl)) < 0) {
            upipe_err_va(upipe, "unable to set IP_HDRINCL");
            close(fd);
            return -1;
        }
    }
    #endif

    i = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&i,
                     sizeof(i)) == -1) {
        upipe_err_va(upipe, "unable to set socket (%m)");
        close(fd);
        return -1;
    }

    if (family == AF_INET6) {
        if (bind_if_index
              && setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                     (void *)&bind_if_index, sizeof(bind_if_index)) < 0) {
            upipe_err(upipe, "couldn't set interface index");
            upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
            close(fd);
            return -1;
        }

        if (bind_addr.ss.ss_family != AF_UNSPEC) {
            #if !defined(__APPLE__) && !defined(__native_client__)
            if (IN6_IS_ADDR_MULTICAST(&bind_addr.sin6.sin6_addr)) {
                struct ipv6_mreq imr;
                union sockaddru bind_addr_any = bind_addr;
                bind_addr_any.sin6.sin6_addr = in6addr_any;

                if (bind(fd, &bind_addr_any.so,
                           sizeof(bind_addr_any)) < 0) {
                    upipe_err(upipe, "couldn't bind");
                    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
                    close(fd);
                    return -1;
                }

                imr.ipv6mr_multiaddr = bind_addr.sin6.sin6_addr;
                imr.ipv6mr_interface = bind_if_index;

                /* Join Multicast group without source filter */
                if (setsockopt(fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                                 (char *)&imr, sizeof(struct ipv6_mreq)) < 0) {
                    upipe_err(upipe, "couldn't join multicast group");
                    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
                    close(fd);
                    return -1;
                }
            } else
            #endif
                goto normal_bind;
        }
    }
    else if (bind_addr.ss.ss_family != AF_UNSPEC) {
normal_bind:
        if (bind(fd, &bind_addr.so, sockaddr_len) < 0) {
            upipe_err(upipe, "couldn't bind");
            upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
            close(fd);
            return -1;
        }
    }

    if (!*use_tcp) {
        /* Increase the receive buffer size to 1/2MB (8Mb/s during 1/2s) to
         * avoid packet loss caused by scheduling problems */
        i = 0x80000;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *) &i, sizeof(i)))
            upipe_warn(upipe, "fail to increase receive buffer");

        /* Join the multicast group if the socket is a multicast address */
        if (bind_addr.ss.ss_family == AF_INET
              && IN_MULTICAST(ntohl(bind_addr.sin.sin_addr.s_addr))) {
#ifndef __native_client__
            if (connect_addr.ss.ss_family != AF_UNSPEC) {
                /* Source-specific multicast */
                struct ip_mreq_source imr;
                imr.imr_multiaddr = bind_addr.sin.sin_addr;
                imr.imr_interface.s_addr = if_addr;
                imr.imr_sourceaddr = connect_addr.sin.sin_addr;
                if (bind_if_index) {
                    upipe_warn(upipe, "ignoring ifindex option in SSM");
                }

                if (setsockopt(fd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
                            (char *)&imr, sizeof(struct ip_mreq_source)) < 0) {
                    upipe_err_va(upipe, "couldn't join multicast group (%m)");
                    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr,
                                 &connect_addr);
                    close(fd);
                    return -1;
                }
            } else if (bind_if_index) {
                /* Linux-specific interface-bound multicast */
                struct ip_mreqn imr;
                imr.imr_multiaddr = bind_addr.sin.sin_addr;
                imr.imr_address.s_addr = if_addr;
                imr.imr_ifindex = bind_if_index;

                if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                 (char *)&imr, sizeof(struct ip_mreqn)) < 0) {
                    upipe_err_va(upipe, "couldn't join multicast group (%m)");
                    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr,
                                 &connect_addr);
                    close(fd);
                    return -1;
                }
            } else
#endif
            {
                /* Regular multicast */
                struct ip_mreq imr;
                imr.imr_multiaddr = bind_addr.sin.sin_addr;
                imr.imr_interface.s_addr = if_addr;

                if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                 (char *)&imr, sizeof(struct ip_mreq)) < 0) {
                    upipe_err_va(upipe, "couldn't join multicast group (%m)");
                    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr,
                                 &connect_addr);
                    close(fd);
                    return -1;
                }
            }
#ifdef SO_BINDTODEVICE
            if (ifname) {
                /* linux specific, needs root or CAP_NET_RAW */
                if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
                               ifname, strlen(ifname) + 1) < 0) {
                    upipe_err_va(upipe, "couldn't bind to device %s (%m)",
                                 ifname);
                    free(ifname);
                    close(fd);
                    return -1;
                }
                ubase_clean_str(&ifname);
            }
#endif
        }
    }

    if (connect_addr.ss.ss_family != AF_UNSPEC) {
        if (connect(fd, &connect_addr.so, sockaddr_len) < 0) {
            upipe_err_va(upipe, "cannot connect socket (%m)");
            upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
            close(fd);
            return -1;
        }

        if (!*use_tcp) {
            if (ttl) {
                if (family == AF_INET
                      && IN_MULTICAST(ntohl(connect_addr.sin.sin_addr.s_addr))) {
                    if (setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL,
                                     (void *)&ttl, sizeof(ttl)) == -1) {
                        upipe_err_va(upipe, "couldn't set TTL (%m)");
                        upipe_udp_print_socket(upipe, "socket definition:", &bind_addr,
                                     &connect_addr);
                        close(fd);
                        return -1;
                    }
                }

                if (family == AF_INET6
                      && IN6_IS_ADDR_MULTICAST(&connect_addr.sin6.sin6_addr)) {
                    if (setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
                                     (void *)&ttl, sizeof(ttl)) == -1) {
                        upipe_err_va(upipe, "couldn't set TTL (%m)");
                        upipe_udp_print_socket(upipe, "socket definition:", &bind_addr,
                                     &connect_addr);
                        close(fd);
                        return -1;
                    }
                }
            }

            if (tos) {
                if (setsockopt(fd, IPPROTO_IP, IP_TOS,
                                 (void *)&tos, sizeof(tos)) == -1) {
                    upipe_err_va(upipe, "couldn't set TOS (%m)");
                    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr,
                                 &connect_addr);
                    close(fd);
                    return -1;
                }
            }
        }
    } else if (*use_tcp) {
        /* Open in listen mode - wait for an incoming connection */
        int new_fd;
        if (listen(fd, 1) < 0) {
            upipe_err_va(upipe, "couldn't listen (%m)");
            upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
            close(fd);
            return -1;
        }

        while ((new_fd = accept(fd, NULL, NULL)) < 0) {
            if (errno != EINTR) {
                upipe_err_va(upipe, "couldn't accept (%m)");
                upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
                close(fd);
                return -1;
            }
        }
        close(fd);
        return new_fd;
    }

    return fd;
}


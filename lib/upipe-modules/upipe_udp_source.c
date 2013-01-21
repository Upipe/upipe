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
 * @short Upipe source module for udp sockets
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uprobe.h>
#include <upipe/ulog.h>
#include <upipe/uclock.h>
#include <upipe/uref.h>
#include <upipe/uref_block.h>
#include <upipe/uref_block_flow.h>
#include <upipe/uref_clock.h>
#include <upipe/upump.h>
#include <upipe/ubuf.h>
#include <upipe/upipe.h>
#include <upipe/upipe_helper_upipe.h>
#include <upipe/upipe_helper_uref_mgr.h>
#include <upipe/upipe_helper_ubuf_mgr.h>
#include <upipe/upipe_helper_output.h>
#include <upipe/upipe_helper_upump_mgr.h>
#include <upipe/upipe_helper_uclock.h>
#include <upipe/upipe_helper_source_read_size.h>
#include <upipe-modules/upipe_udp_source.h>

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netdb.h>

#ifndef O_CLOEXEC
#   define O_CLOEXEC 0
#endif

/** default size of buffers when unspecified */
#define UBUF_DEFAULT_SIZE       4096

#define UDP_DEFAULT_TTL 0
#define UDP_DEFAULT_PORT 1234

/** union sockaddru: wrapper to avoid strict-aliasing issues */
union sockaddru
{
    struct sockaddr_storage ss;
    struct sockaddr so;
    struct sockaddr_in sin;
    struct sockaddr_in6 sin6;
};

/** @internal @This is the private context of a udp socket source pipe. */
struct upipe_udpsrc {
    /** uref manager */
    struct uref_mgr *uref_mgr;

    /** ubuf manager */
    struct ubuf_mgr *ubuf_mgr;
    /** pipe acting as output */
    struct upipe *output;
    /** flow definition packet */
    struct uref *flow_def;
    /** true if the flow definition has already been sent */
    bool flow_def_sent;

    /** upump manager */
    struct upump_mgr *upump_mgr;
    /** read watcher */
    struct upump *upump;
    /** uclock structure, if not NULL we are in live mode */
    struct uclock *uclock;
    /** read size */
    unsigned int read_size;

    /** udp socket descriptor */
    int fd;
    /** udp socket uri */
    char *uri;
    /** true if we have thrown the ready event */
    bool ready;

    /** refcount management structure */
    urefcount refcount;
    /** public upipe structure */
    struct upipe upipe;
};

UPIPE_HELPER_UPIPE(upipe_udpsrc, upipe)
UPIPE_HELPER_UREF_MGR(upipe_udpsrc, uref_mgr)

UPIPE_HELPER_UBUF_MGR(upipe_udpsrc, ubuf_mgr)
UPIPE_HELPER_OUTPUT(upipe_udpsrc, output, flow_def, flow_def_sent)

UPIPE_HELPER_UPUMP_MGR(upipe_udpsrc, upump_mgr, upump)
UPIPE_HELPER_UCLOCK(upipe_udpsrc, uclock)
UPIPE_HELPER_SOURCE_READ_SIZE(upipe_udpsrc, read_size)

/** @internal @This allocates a udp socket source pipe.
 *
 * @param mgr common management structure
 * @param uprobe structure used to raise events
 * @param ulog structure used to output logs
 * @return pointer to upipe or NULL in case of allocation error
 */
static struct upipe *upipe_udpsrc_alloc(struct upipe_mgr *mgr,
                                        struct uprobe *uprobe,
                                        struct ulog *ulog)
{
    struct upipe_udpsrc *upipe_udpsrc = malloc(sizeof(struct upipe_udpsrc));
    if (unlikely(upipe_udpsrc == NULL))
        return NULL;
    struct upipe *upipe = upipe_udpsrc_to_upipe(upipe_udpsrc);
    upipe_init(upipe, mgr, uprobe, ulog);
    urefcount_init(&upipe_udpsrc->refcount);
    upipe_udpsrc_init_uref_mgr(upipe);
    upipe_udpsrc_init_ubuf_mgr(upipe);
    upipe_udpsrc_init_output(upipe);
    upipe_udpsrc_init_upump_mgr(upipe);
    upipe_udpsrc_init_uclock(upipe);
    upipe_udpsrc_init_read_size(upipe, UBUF_DEFAULT_SIZE);
    upipe_udpsrc->fd = -1;
    upipe_udpsrc->uri = NULL;
    upipe_udpsrc->ready = false;
    return upipe;
}

/** @internal @This reads data from the source and outputs it.
 * It is called either when the idler triggers (permanent storage mode) or
 * when data is available on the udp socket descriptor (live stream mode).
 *
 * @param upump description structure of the read watcher
 */
static void upipe_udpsrc_worker(struct upump *upump)
{
    struct upipe *upipe = upump_get_opaque(upump, struct upipe *);
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    uint64_t systime = 0; /* to keep gcc quiet */
    if (unlikely(upipe_udpsrc->uclock != NULL))
        systime = uclock_now(upipe_udpsrc->uclock);

    struct uref *uref = uref_block_alloc(upipe_udpsrc->uref_mgr,
                                         upipe_udpsrc->ubuf_mgr,
                                         upipe_udpsrc->read_size);
    if (unlikely(uref == NULL)) {
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }

    uint8_t *buffer;
    int read_size = -1;
    if (unlikely(!uref_block_write(uref, 0, &read_size, &buffer))) {
        uref_free(uref);
        ulog_aerror(upipe->ulog);
        upipe_throw_aerror(upipe);
        return;
    }
    assert(read_size == upipe_udpsrc->read_size);

    ssize_t ret = read(upipe_udpsrc->fd, buffer, upipe_udpsrc->read_size);
    uref_block_unmap(uref, 0, read_size);

    if (unlikely(ret == -1)) {
        uref_free(uref);
        switch (errno) {
            case EINTR:
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
                /* not an issue, try again later */
                return;
            case EBADF:
            case EINVAL:
            case EIO:
            default:
                break;
        }
        ulog_error(upipe->ulog, "read error from %s (%s)", upipe_udpsrc->uri,
                   ulog_strerror(upipe->ulog, errno));
        upipe_udpsrc_set_upump(upipe, NULL);
        upipe_throw_read_end(upipe, upipe_udpsrc->uri);
        return;
    }
    if (unlikely(ret == 0)) {
        uref_free(uref);
        if (likely(upipe_udpsrc->uclock == NULL)) {
            ulog_notice(upipe->ulog, "end of udp socket %s", upipe_udpsrc->uri);
            upipe_udpsrc_set_upump(upipe, NULL);
            upipe_throw_read_end(upipe, upipe_udpsrc->uri);
        }
        return;
    }
    if (unlikely(upipe_udpsrc->uclock != NULL))
        uref_clock_set_systime(uref, systime);
    if (unlikely(ret != upipe_udpsrc->read_size))
        uref_block_resize(uref, 0, ret);
    upipe_udpsrc_output(upipe, uref, upump);
}

/** @internal @This returns the index of an interface
 *
 * @param upipe description structure of the pipe
 * @param name interface name
 * @return interface index
 */
static bool upipe_udp_get_ifindex(struct upipe *upipe, const char *name, int *ifrindex)
{
#ifndef __APPLE__
    int fd;
    struct ifreq ifr;

    if (! (name && ifrindex && upipe) ) {
        return false;
    }

    if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        ulog_error(upipe->ulog, "unable to open socket (%s)", strerror(errno));
        return false;
    }

    strncpy(ifr.ifr_name, name, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ-1] = '\0';

    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
        ulog_error(upipe->ulog, "unable to get interface index (%s)", strerror(errno));
        return false;
    }

    close(fd);

    *ifrindex = ifr.ifr_ifindex;
    return true;
#else
    ulog_error(upipe->ulog, "unable to get interface index (%s)", strerror(errno));
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
        ulog_debug(upipe->ulog, "%s bind:%s:%u", text,
                 inet_ntoa(bind->sin.sin_addr), ntohs(bind->sin.sin_port));
    } else if (bind->ss.ss_family == AF_INET6) {
        char buf[INET6_ADDRSTRLEN];
        ulog_debug(upipe->ulog, "%s bind:[%s]:%u", text,
                 inet_ntop(AF_INET6, &bind->sin6.sin6_addr, buf, sizeof(buf)),
                 ntohs(bind->sin6.sin6_port));
    }

    if (connect->ss.ss_family == AF_INET) {
        ulog_debug(upipe->ulog, "%s connect:%s:%u", text,
                 inet_ntoa(connect->sin.sin_addr),
                 ntohs(connect->sin.sin_port));
    } else if (connect->ss.ss_family == AF_INET6) {
        char buf[INET6_ADDRSTRLEN];
        ulog_debug(upipe->ulog, "%s connect:[%s]:%u", text,
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
static struct addrinfo *upipe_udp_parse_node_service(struct upipe *upipe, char *_string, char **stringend,
                                          uint16_t default_port,
                                          int *if_index)
{
    int family = AF_INET;
    char port_buffer[6];
    char *string = strdup(_string);
    char *node, *port = NULL, *end;
    struct addrinfo *res;
    struct addrinfo hint;
    int ret;

    if (string[0] == '[') {
        family = AF_INET6;
        node = string + 1;
        end = strchr(node, ']');
        if (end == NULL) {
            ulog_warning(upipe->ulog, "invalid IPv6 address %s", _string);
            free(string);
            return NULL;
        }
        *end++ = '\0';

        char *intf = strrchr(node, '%');
        if (intf != NULL) {
            *intf++ = '\0';
            if (if_index != NULL) {
                if (!upipe_udp_get_ifindex(upipe, intf, if_index)) {
                    return NULL;
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
            struct sockaddr_in *sin = malloc(sizeof(struct sockaddr_in));
            sin->sin_family = AF_INET;
            if (port != NULL) {
                sin->sin_port = ntohs(atoi(port));
            } else {
                sin->sin_port = 0;
            }
            sin->sin_addr = addr;

            res = malloc(sizeof(struct addrinfo));
            res->ai_family = AF_INET;
            res->ai_socktype = SOCK_DGRAM;
            res->ai_protocol = 0;
            res->ai_addrlen = sizeof(struct sockaddr_in);
            res->ai_addr = (struct sockaddr *)sin;
            res->ai_canonname = NULL;
            res->ai_next = NULL;

            free(string);
            return res;
        }
    }

    memset(&hint, 0, sizeof(hint));
    hint.ai_family = family;
    hint.ai_socktype = SOCK_DGRAM;
    hint.ai_protocol = 0;
    hint.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV | AI_ADDRCONFIG;
    if ((ret = getaddrinfo(node, port, &hint, &res)) != 0) {
        //ulog_warning(upipe->ulog, "getaddrinfo error: %s", gai_strerror(ret));
        free(string);
        return NULL;
    }

    free(string);
    return res;
}

/** @internal @This parses argv and open IPv4 & IPv6 sockets
 *
 * @param upipe description structure of the pipe
 * @param _uri socket URI
 * @param ttl packets time-to-live
 * @param bind_port bind port
 * @param connect_port connect port
 * @param weight weight (UNUSED)
 * @param use_tcp Set this to open a tcp socket (instead of udp)
 */
int upipe_udp_open_socket(struct upipe *upipe, const char *_uri, int ttl, uint16_t bind_port,
                uint16_t connect_port, unsigned int *weight, bool *use_tcp)
{
    union sockaddru bind_addr, connect_addr;
    int fd, i;
    char *uri = strdup(_uri);
    char *token = uri;
    char *token2 = NULL;
    int bind_if_index = 0, connect_if_index = 0;
    in_addr_t if_addr = INADDR_ANY;
    int tos = 0;
    bool b_tcp;
    struct addrinfo *ai;
    int family;
    socklen_t sockaddr_len;

    bind_addr.ss.ss_family = AF_UNSPEC;
    connect_addr.ss.ss_family = AF_UNSPEC;

    if (use_tcp == NULL) {
        use_tcp = &b_tcp;
    }
    *use_tcp = false;

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
        return -1;
    }

    /* Hosts */
    if (token[0] != '@') {
        ai = upipe_udp_parse_node_service(upipe, token, &token, connect_port, &connect_if_index);
        if (ai == NULL) {
            return -1;
        }
        memcpy(&connect_addr.ss, ai->ai_addr, ai->ai_addrlen);
        freeaddrinfo(ai);
    }

    if (token[0] == '@') {
        token++;
        ai = upipe_udp_parse_node_service(upipe, token, &token, bind_port, &bind_if_index);
        if (ai == NULL) {
            return -1;
        }
        memcpy(&bind_addr.ss, ai->ai_addr, ai->ai_addrlen);
        freeaddrinfo(ai);
    }

    if (bind_addr.ss.ss_family == AF_UNSPEC &&
         connect_addr.ss.ss_family == AF_UNSPEC) {
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
                if_addr = inet_addr(ARG_OPTION("ifaddr="));
            } else if (IS_OPTION("ttl=")) {
                ttl = strtol(ARG_OPTION("ttl="), NULL, 0);
            } else if (IS_OPTION("tos=")) {
                tos = strtol(ARG_OPTION("tos="), NULL, 0);
            } else if (IS_OPTION("tcp")) {
                *use_tcp = true;
            } else {
                ulog_warning(upipe->ulog, "unrecognized option %s", token2);
            }
#undef IS_OPTION
#undef ARG_OPTION
        } while ((token2 = strchr(token2, '/')) != NULL);
    }

    free(uri);

    /* Sanity checks */
    if (bind_addr.ss.ss_family != AF_UNSPEC
          && connect_addr.ss.ss_family != AF_UNSPEC
          && bind_addr.ss.ss_family != connect_addr.ss.ss_family) {
        ulog_error(upipe->ulog, "incompatible address types");
        return -1;
    }
    if (bind_addr.ss.ss_family != AF_UNSPEC) {
        family = bind_addr.ss.ss_family;
    } else if (connect_addr.ss.ss_family != AF_UNSPEC) {
        family = connect_addr.ss.ss_family;
    } else {
        ulog_error(upipe->ulog, "ambiguous address declaration");
        return -1;
    }
    sockaddr_len = (family == AF_INET) ? sizeof(struct sockaddr_in) :
                     sizeof(struct sockaddr_in6);

    if (bind_if_index && connect_if_index
          && bind_if_index != connect_if_index) {
        ulog_error(upipe->ulog, "incompatible bind and connect interfaces");
        return -1;
    }
    if (connect_if_index) bind_if_index = connect_if_index;
    else connect_if_index = bind_if_index;

    /* Socket configuration */
    if ((fd = socket(family, *use_tcp ? SOCK_STREAM : SOCK_DGRAM,
                         0)) < 0) {
        ulog_error(upipe->ulog, "unable to open socket (%s)", strerror(errno));
        return -1;
    }

    i = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)&i,
                     sizeof(i)) == -1) {
        ulog_error(upipe->ulog, "unable to set socket (%s)", strerror(errno));
        close(fd);
        return -1;
    }

    if (family == AF_INET6) {
        if (bind_if_index
              && setsockopt(fd, IPPROTO_IPV6, IPV6_MULTICAST_IF,
                     (void *)&bind_if_index, sizeof(bind_if_index)) < 0) {
            ulog_error(upipe->ulog, "couldn't set interface index");
            upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
            close(fd);
            return -1;
        }

        if (bind_addr.ss.ss_family != AF_UNSPEC) {
            #ifndef __APPLE__
            if (IN6_IS_ADDR_MULTICAST(&bind_addr.sin6.sin6_addr)) {
                struct ipv6_mreq imr;
                union sockaddru bind_addr_any = bind_addr;
                bind_addr_any.sin6.sin6_addr = in6addr_any;

                if (bind(fd, &bind_addr_any.so,
                           sizeof(bind_addr_any)) < 0) {
                    ulog_error(upipe->ulog, "couldn't bind");
                    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
                    close(fd);
                    return -1;
                }

                imr.ipv6mr_multiaddr = bind_addr.sin6.sin6_addr;
                imr.ipv6mr_interface = bind_if_index;

                /* Join Multicast group without source filter */
                if (setsockopt(fd, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP,
                                 (char *)&imr, sizeof(struct ipv6_mreq)) < 0) {
                    ulog_error(upipe->ulog, "couldn't join multicast group");
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
            ulog_error(upipe->ulog, "couldn't bind");
            upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
            close(fd);
            return -1;
        }
    }

    if (!*use_tcp) {
        /* Increase the receive buffer size to 1/2MB (8Mb/s during 1/2s) to
         * avoid packet loss caused by scheduling problems */
        i = 0x80000;
        setsockopt(fd, SOL_SOCKET, SO_RCVBUF, (void *) &i, sizeof(i));

        /* Join the multicast group if the socket is a multicast address */
        if (bind_addr.ss.ss_family == AF_INET
              && IN_MULTICAST(ntohl(bind_addr.sin.sin_addr.s_addr))) {
            if (connect_addr.ss.ss_family != AF_UNSPEC) {
                /* Source-specific multicast */
                struct ip_mreq_source imr;
                imr.imr_multiaddr = bind_addr.sin.sin_addr;
                imr.imr_interface.s_addr = if_addr;
                imr.imr_sourceaddr = connect_addr.sin.sin_addr;
                if (bind_if_index) {
                    ulog_warning(upipe->ulog, "ignoring ifindex option in SSM");
                }

                if (setsockopt(fd, IPPROTO_IP, IP_ADD_SOURCE_MEMBERSHIP,
                            (char *)&imr, sizeof(struct ip_mreq_source)) < 0) {
                    ulog_error(upipe->ulog, "couldn't join multicast group (%s)",
                             strerror(errno));
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
                    ulog_error(upipe->ulog, "couldn't join multicast group (%s)",
                             strerror(errno));
                    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr,
                                 &connect_addr);
                    close(fd);
                    return -1;
                }
            } else {
                /* Regular multicast */
                struct ip_mreq imr;
                imr.imr_multiaddr = bind_addr.sin.sin_addr;
                imr.imr_interface.s_addr = if_addr;

                if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                                 (char *)&imr, sizeof(struct ip_mreq)) < 0) {
                    ulog_error(upipe->ulog, "couldn't join multicast group (%s)",
                             strerror(errno));
                    upipe_udp_print_socket(upipe, "socket definition:", &bind_addr,
                                 &connect_addr);
                    close(fd);
                    return -1;
                }
            }
        }
    }

    if (connect_addr.ss.ss_family != AF_UNSPEC) {
        if (connect(fd, &connect_addr.so, sockaddr_len) < 0) {
            ulog_error(upipe->ulog, "cannot connect socket (%s)",
                     strerror(errno));
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
                        ulog_error(upipe->ulog, "couldn't set TTL (%s)",
                                 strerror(errno));
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
                        ulog_error(upipe->ulog, "couldn't set TTL (%s)",
                                 strerror(errno));
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
                    ulog_error(upipe->ulog, "couldn't set TOS (%s)", strerror(errno));
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
            ulog_error(upipe->ulog, "couldn't listen (%s)", strerror(errno));
            upipe_udp_print_socket(upipe, "socket definition:", &bind_addr, &connect_addr);
            close(fd);
            return -1;
        }

        while ((new_fd = accept(fd, NULL, NULL)) < 0) {
            if (errno != EINTR) {
                ulog_error(upipe->ulog, "couldn't accept (%s)", strerror(errno));
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

/** @internal @This returns the uri of the currently opened udp socket.
 *
 * @param upipe description structure of the pipe
 * @param uri_p filled in with the uri of the udp socket
 * @return false in case of error
 */
static bool _upipe_udpsrc_get_uri(struct upipe *upipe, const char **uri_p)
{
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    assert(uri_p != NULL);
    *uri_p = upipe_udpsrc->uri;
    return true;
}

/** @internal @This asks to open the given udp socket.
 *
 * @param upipe description structure of the pipe
 * @param uri relative or absolute uri of the udp socket
 * @return false in case of error
 */
static bool _upipe_udpsrc_set_uri(struct upipe *upipe, const char *uri)
{
    bool use_tcp = 0;
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);

    if (unlikely(upipe_udpsrc->fd != -1)) {
        if (likely(upipe_udpsrc->uri != NULL))
            ulog_notice(upipe->ulog, "closing udp socket %s", upipe_udpsrc->uri);
        close(upipe_udpsrc->fd);
    }
    free(upipe_udpsrc->uri);
    upipe_udpsrc->uri = NULL;
    upipe_udpsrc_set_upump(upipe, NULL);

    if (likely(uri != NULL)) {
        upipe_udpsrc->fd = upipe_udp_open_socket(upipe, uri, UDP_DEFAULT_TTL, UDP_DEFAULT_PORT, 0, NULL, &use_tcp);
//        upipe_udpsrc->fd = open(uri, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (unlikely(upipe_udpsrc->fd == -1)) {
            ulog_error(upipe->ulog, "can't open udp socket %s (%s)", uri,
                       ulog_strerror(upipe->ulog, errno));
            return false;
        }

        upipe_udpsrc->uri = strdup(uri);
        if (unlikely(upipe_udpsrc->uri == NULL)) {
            close(upipe_udpsrc->fd);
            upipe_udpsrc->fd = -1;
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return false;
        }
        ulog_notice(upipe->ulog, "opening udp socket %s", upipe_udpsrc->uri);
    }
    return true;
}

/** @internal @This processes control commands on a udp socket source pipe.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool _upipe_udpsrc_control(struct upipe *upipe, enum upipe_command command,
                                va_list args)
{
    switch (command) {
        case UPIPE_GET_UREF_MGR: {
            struct uref_mgr **p = va_arg(args, struct uref_mgr **);
            return upipe_udpsrc_get_uref_mgr(upipe, p);
        }
        case UPIPE_SET_UREF_MGR: {
            struct uref_mgr *uref_mgr = va_arg(args, struct uref_mgr *);
            return upipe_udpsrc_set_uref_mgr(upipe, uref_mgr);
        }

        case UPIPE_GET_UBUF_MGR: {
            struct ubuf_mgr **p = va_arg(args, struct ubuf_mgr **);
            return upipe_udpsrc_get_ubuf_mgr(upipe, p);
        }
        case UPIPE_SET_UBUF_MGR: {
            struct ubuf_mgr *ubuf_mgr = va_arg(args, struct ubuf_mgr *);
            return upipe_udpsrc_set_ubuf_mgr(upipe, ubuf_mgr);
        }
        case UPIPE_GET_OUTPUT: {
            struct upipe **p = va_arg(args, struct upipe **);
            return upipe_udpsrc_get_output(upipe, p);
        }
        case UPIPE_SET_OUTPUT: {
            struct upipe *output = va_arg(args, struct upipe *);
            return upipe_udpsrc_set_output(upipe, output);
        }

        case UPIPE_GET_UPUMP_MGR: {
            struct upump_mgr **p = va_arg(args, struct upump_mgr **);
            return upipe_udpsrc_get_upump_mgr(upipe, p);
        }
        case UPIPE_SET_UPUMP_MGR: {
            struct upump_mgr *upump_mgr = va_arg(args, struct upump_mgr *);
            return upipe_udpsrc_set_upump_mgr(upipe, upump_mgr);
        }
        case UPIPE_GET_UCLOCK: {
            struct uclock **p = va_arg(args, struct uclock **);
            return upipe_udpsrc_get_uclock(upipe, p);
        }
        case UPIPE_SET_UCLOCK: {
            struct uclock *uclock = va_arg(args, struct uclock *);
            return upipe_udpsrc_set_uclock(upipe, uclock);
        }
        case UPIPE_SOURCE_GET_READ_SIZE: {
            unsigned int *p = va_arg(args, unsigned int *);
            return upipe_udpsrc_get_read_size(upipe, p);
        }
        case UPIPE_SOURCE_SET_READ_SIZE: {
            unsigned int read_size = va_arg(args, unsigned int);
            return upipe_udpsrc_set_read_size(upipe, read_size);
        }

        case UPIPE_UDPSRC_GET_URI: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_UDPSRC_SIGNATURE);
            const char **uri_p = va_arg(args, const char **);
            return _upipe_udpsrc_get_uri(upipe, uri_p);
        }
        case UPIPE_UDPSRC_SET_URI: {
            unsigned int signature = va_arg(args, unsigned int);
            assert(signature == UPIPE_UDPSRC_SIGNATURE);
            const char *uri = va_arg(args, const char *);
            return _upipe_udpsrc_set_uri(upipe, uri);
        }
        default:
            return false;
    }
}

/** @internal @This processes control commands on a udp socket source pipe, and
 * checks the status of the pipe afterwards.
 *
 * @param upipe description structure of the pipe
 * @param command type of command to process
 * @param args arguments of the command
 * @return false in case of error
 */
static bool upipe_udpsrc_control(struct upipe *upipe, enum upipe_command command,
                               va_list args)
{
    if (unlikely(!_upipe_udpsrc_control(upipe, command, args)))
        return false;

    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    if (unlikely(upipe_udpsrc->uref_mgr != NULL &&
                 upipe_udpsrc->flow_def == NULL)) {
        struct uref *flow_def = uref_block_flow_alloc_def(upipe_udpsrc->uref_mgr,
                                                          NULL);
        if (unlikely(flow_def == NULL)) {
            ulog_aerror(upipe->ulog);
            upipe_throw_aerror(upipe);
            return false;
        }
        upipe_udpsrc_store_flow_def(upipe, flow_def);
    }

    if (unlikely(upipe_udpsrc->uref_mgr != NULL &&
                 upipe_udpsrc->output != NULL &&
                 upipe_udpsrc->ubuf_mgr != NULL &&
                 upipe_udpsrc->upump_mgr != NULL &&
                 upipe_udpsrc->fd != -1)) {
        if (likely(upipe_udpsrc->upump == NULL)) {
            struct upump *upump;
            if (likely(upipe_udpsrc->uclock == NULL))
                upump = upump_alloc_idler(upipe_udpsrc->upump_mgr,
                                          upipe_udpsrc_worker, upipe, true);
            else
                upump = upump_alloc_fd_read(upipe_udpsrc->upump_mgr,
                                            upipe_udpsrc_worker, upipe, true,
                                            upipe_udpsrc->fd);
            if (unlikely(upump == NULL)) {
                ulog_error(upipe->ulog, "can't create worker");
                upipe_throw_upump_error(upipe);
                return false;
            }
            upipe_udpsrc_set_upump(upipe, upump);
            upump_start(upump);
        }
        if (likely(!upipe_udpsrc->ready)) {
            upipe_udpsrc->ready = true;
            upipe_throw_ready(upipe);
        }

    } else {
        upipe_udpsrc_set_upump(upipe, NULL);
        upipe_udpsrc->ready = false;

        if (unlikely(upipe_udpsrc->fd != -1)) {
            if (unlikely(upipe_udpsrc->uref_mgr == NULL))
                upipe_throw_need_uref_mgr(upipe);
            else if (unlikely(upipe_udpsrc->upump_mgr == NULL))
                upipe_throw_need_upump_mgr(upipe);
            else if (unlikely(upipe_udpsrc->ubuf_mgr == NULL))
                upipe_throw_need_ubuf_mgr(upipe, upipe_udpsrc->flow_def);
        }
    }

    return true;
}

/** @This increments the reference count of a upipe.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_udpsrc_use(struct upipe *upipe)
{
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    urefcount_use(&upipe_udpsrc->refcount);
}

/** @This decrements the reference count of a upipe or frees it.
 *
 * @param upipe description structure of the pipe
 */
static void upipe_udpsrc_release(struct upipe *upipe)
{
    struct upipe_udpsrc *upipe_udpsrc = upipe_udpsrc_from_upipe(upipe);
    if (unlikely(urefcount_release(&upipe_udpsrc->refcount))) {
        if (likely(upipe_udpsrc->fd != -1)) {
            if (likely(upipe_udpsrc->uri != NULL))
                ulog_notice(upipe->ulog, "closing udp socket %s", upipe_udpsrc->uri);
            close(upipe_udpsrc->fd);
        }
        free(upipe_udpsrc->uri);
        upipe_udpsrc_clean_read_size(upipe);
        upipe_udpsrc_clean_uclock(upipe);
        upipe_udpsrc_clean_upump_mgr(upipe);
        upipe_udpsrc_clean_output(upipe);
        upipe_udpsrc_clean_ubuf_mgr(upipe);
        upipe_udpsrc_clean_uref_mgr(upipe);

        upipe_clean(upipe);
        urefcount_clean(&upipe_udpsrc->refcount);
        free(upipe_udpsrc);
    }
}

/** module manager static descriptor */
static struct upipe_mgr upipe_udpsrc_mgr = {
    .signature = UPIPE_UDPSRC_SIGNATURE,

    .upipe_alloc = upipe_udpsrc_alloc,
    .upipe_input = NULL,
    .upipe_control = upipe_udpsrc_control,
    .upipe_use = upipe_udpsrc_use,
    .upipe_release = upipe_udpsrc_release,

    .upipe_mgr_use = NULL,
    .upipe_mgr_release = NULL
};

/** @This returns the management structure for all udp socket sources
 *
 * @return pointer to manager
 */
struct upipe_mgr *upipe_udpsrc_mgr_alloc(void)
{
    return &upipe_udpsrc_mgr;
}

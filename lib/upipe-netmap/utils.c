#include <upipe/ubase.h>
#include <upipe/upipe.h>

#include "utils.h"
#include "../upipe-modules/upipe_udp.h"

static int parse_one(struct upipe *upipe, struct destination *dest, const char *path)
{
    char *str = strdup(path);
    if (!str)
        return UBASE_ERR_ALLOC;

    bool success = upipe_udp_parse_node_service(upipe, str, NULL, 0, NULL,
                (struct sockaddr_storage *)&dest->sin);
    free(str);
    if (!success)
        return UBASE_ERR_INVALID;

    /* A zero-length IP address (path string starting with a colon) will
     * parse as 0.0.0.0 so re-use the source addresses but keep the parsed
     * port number. */
    if (dest->sin.sin_addr.s_addr == 0) {
        /* TODO */
        return UBASE_ERR_INVALID;
    }

    /* Set ethernet details and the inferface index. */
    dest->sll = (struct sockaddr_ll) {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(ETHERNET_TYPE_IP),
        /* TODO: get ifindex and socket if we want to do ARP. */
        .sll_halen = ETHERNET_ADDR_LEN,
    };

    /* Set MAC address. */
    uint32_t dst_ip = ntohl(dest->sin.sin_addr.s_addr);
    /* If a multicast IP address, fill a multicast MAC address. */
    if (IN_MULTICAST(dst_ip)) {
        dest->sll.sll_addr[0] = 0x01;
        dest->sll.sll_addr[1] = 0x00;
        dest->sll.sll_addr[2] = 0x5e;
        dest->sll.sll_addr[3] = (dst_ip >> 16) & 0x7f;
        dest->sll.sll_addr[4] = (dst_ip >>  8) & 0xff;
        dest->sll.sll_addr[5] = (dst_ip      ) & 0xff;
    }
    /* Otherwise query ARP for the destination address. */
    else {
        /* TODO */
        return UBASE_ERR_INVALID;
    }

    return UBASE_ERR_NONE;
}

int parse_destinations(struct upipe *upipe,
        struct destination *destination1, struct destination *destination2,
        const char *path_1, const char *path_2)
{
    struct destination d[2];
    memset(d, 0, sizeof d);
    UBASE_RETURN(parse_one(upipe, &d[0], path_1));
    UBASE_RETURN(parse_one(upipe, &d[1], path_2));
    *destination1 = d[0];
    *destination2 = d[1];
    return UBASE_ERR_NONE;
}

void make_header(uint8_t buf[HEADER_ETH_IP_UDP_LEN],
        const struct destination *src, const struct destination *dst,
        int vlan_id, uint16_t payload_size)
{
    /* Write ethernet header. */
    ethernet_set_dstaddr(buf, dst->sll.sll_addr);
    ethernet_set_srcaddr(buf, src->sll.sll_addr);
    if (vlan_id < 0) {
        ethernet_set_lentype(buf, ETHERNET_TYPE_IP);
    }
    /* VLANs */
    else {
        ethernet_set_lentype(buf, ETHERNET_TYPE_VLAN);
        ethernet_vlan_set_priority(buf, 0);
        ethernet_vlan_set_cfi(buf, 0);
        ethernet_vlan_set_id(buf, vlan_id);
        ethernet_vlan_set_lentype(buf, ETHERNET_TYPE_IP);
    }
    buf = ethernet_payload(buf);
    /* Write IP and UDP headers. */
    upipe_udp_raw_fill_headers(buf, src->sin.sin_addr.s_addr, dst->sin.sin_addr.s_addr,
            ntohs(src->sin.sin_port), ntohs(dst->sin.sin_port),
            10 /* TTL */, 0 /* TOS */, payload_size);
}

#include <bitstream/ieee/ethernet.h>
#include <bitstream/ietf/ip.h>
#include <bitstream/ietf/udp.h>

#include <netinet/in.h>
#include <linux/if_packet.h>

#define HEADER_ETH_IP_UDP_LEN (ETHERNET_HEADER_LEN + ETHERNET_VLAN_LEN + IP_HEADER_MINSIZE + UDP_HEADER_SIZE)

/* Destination details for all flows, eventually. */
struct destination {
    /* IP details for the destination. */
    struct sockaddr_in sin;
    /* Ethernet details for the destination. */
    struct sockaddr_ll sll;
    /* Raw Ethernet, optional vlan, IP, and UDP headers. */
    uint8_t header[HEADER_ETH_IP_UDP_LEN];
    /* length (vlan or not) */
    uint8_t header_len;
};

/* Parse a pair of strings represnting IP:port into the given structures.
 * On error the structures remain unchanged. */
int parse_destinations(struct upipe *upipe,
        struct destination *destination1, struct destination *destination2,
        const char *path_1, const char *path_2);

/* Fill all fields of ethernet, optional vlan, IP, UDP headers. */
void make_header(uint8_t buf[HEADER_ETH_IP_UDP_LEN],
        const struct destination *source, const struct destination *destination,
        int vlan_id, uint16_t payload_size);

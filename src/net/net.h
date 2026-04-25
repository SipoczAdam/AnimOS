#ifndef NET_H
#define NET_H

#include "../kernel/types.h"

struct ethernet_header {
    uint8_t dest[6];
    uint8_t src[6];
    uint16_t type;
} __attribute__((packed));

struct ipv4_header {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t len;
    uint16_t id;
    uint16_t flags_offset;
    uint8_t ttl;
    uint8_t proto;
    uint16_t chksum;
    uint32_t src_ip;
    uint32_t dest_ip;
} __attribute__((packed));

struct udp_header {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t len;
    uint16_t chksum;
} __attribute__((packed));

struct ntp_packet {
    uint8_t li_vn_mode;
    uint8_t stratum;
    uint8_t poll;
    uint8_t precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint32_t ref_ts_sec;
    uint32_t ref_ts_frac;
    uint32_t orig_ts_sec;
    uint32_t orig_ts_frac;
    uint32_t recv_ts_sec;
    uint32_t recv_ts_frac;
    uint32_t trans_ts_sec;
    uint32_t trans_ts_frac;
} __attribute__((packed));

void net_init(uint32_t ip);
void net_poll();
void arp_request(uint32_t target_ip);
void ntp_sync(uint32_t ntp_server_ip);
uint64_t ntp_get_time();

#endif

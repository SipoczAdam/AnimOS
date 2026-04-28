#include "net.h"
#include "../drivers/e1000.h"

static uint32_t my_ip = 0;
static uint8_t my_mac[6];
static uint32_t gateway_ip = 0;
static uint8_t gateway_mac[6] = {0, 0, 0, 0, 0, 0};
static uint32_t dhcp_server_ip = 0;
static uint64_t last_ntp_timestamp = 0;
static uint32_t global_xid_counter = 0x12345678;
static uint32_t last_xid = 0;
int received_any = 0; 
int packet_counter = 0;
int dhcp_ok = 0;

static inline uint16_t swap16(uint16_t v) { return (v << 8) | (v >> 8); }
static inline uint32_t swap32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}

static uint16_t ip_checksum(void* vdata, size_t length) {
    uint16_t* ptr = (uint16_t*)vdata;
    uint32_t sum = 0;
    for (size_t i = 0; i < length / 2; i++) {
        sum += ptr[i];
    }
    if (length & 1) sum += ((uint8_t*)vdata)[length - 1];
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~((uint16_t)sum);
}

void net_init(uint32_t ip) {
    my_ip = 0;
    e1000_get_mac(my_mac);
    global_xid_counter += *(uint32_t*)(my_mac + 2);
}

uint32_t net_get_ip() { return my_ip; }
int net_dhcp_ok() { return dhcp_ok; }

void arp_request(uint32_t target_ip) {
    uint8_t buffer[64];
    for(int i=0; i<64; i++) buffer[i] = 0;
    struct ethernet_header* eth = (struct ethernet_header*)buffer;
    for(int i=0; i<6; i++) { eth->dest[i] = 0xFF; eth->src[i] = my_mac[i]; }
    eth->type = swap16(0x0806);

    uint8_t* arp = (uint8_t*)(eth + 1);
    arp[0] = 0; arp[1] = 1; arp[2] = 8; arp[3] = 0; 
    arp[4] = 6; arp[5] = 4; arp[6] = 0; arp[7] = 1; 
    for(int i=0; i<6; i++) arp[8+i] = my_mac[i];
    *(uint32_t*)(arp + 14) = my_ip;
    for(int i=0; i<6; i++) arp[18+i] = 0;
    *(uint32_t*)(arp + 24) = target_ip;

    e1000_send_packet(buffer, 42);
}

void dhcp_discover() {
    uint8_t buffer[1024];
    for(int i=0; i<1024; i++) buffer[i] = 0;
    struct ethernet_header* eth = (struct ethernet_header*)buffer;
    for(int i=0; i<6; i++) { eth->dest[i] = 0xFF; eth->src[i] = my_mac[i]; }
    eth->type = swap16(0x0800);

    struct ipv4_header* ip = (struct ipv4_header*)(eth + 1);
    ip->version_ihl = 0x45;
    ip->len = swap16(580); // 20 + 8 + 552
    ip->ttl = 64; ip->proto = 17;
    ip->src_ip = 0; ip->dest_ip = 0xFFFFFFFF;
    ip->chksum = 0; ip->chksum = ip_checksum(ip, 20);

    struct udp_header* udp = (struct udp_header*)(ip + 1);
    udp->src_port = swap16(68); udp->dest_port = swap16(67);
    udp->len = swap16(560); // 8 + 552
    udp->chksum = 0; 

    struct dhcp_packet* dhcp = (struct dhcp_packet*)(udp + 1);
    dhcp->op = 1; dhcp->htype = 1; dhcp->hlen = 6;
    last_xid = global_xid_counter++;
    dhcp->xid = swap32(last_xid); 
    dhcp->flags = 0; // Unicast flag (some routers prefer this)
    dhcp->cookie = swap32(0x63825363);
    for(int i=0; i<6; i++) dhcp->chaddr[i] = my_mac[i];

    int o = 0;
    dhcp->options[o++] = 53; dhcp->options[o++] = 1; dhcp->options[o++] = 1; // Discover
    dhcp->options[o++] = 57; dhcp->options[o++] = 2; dhcp->options[o++] = 0x05; dhcp->options[o++] = 0xDC; // Max size 1500
    dhcp->options[o++] = 61; dhcp->options[o++] = 7; dhcp->options[o++] = 1;
    for(int i=0; i<6; i++) dhcp->options[o++] = my_mac[i];
    dhcp->options[o++] = 12; dhcp->options[o++] = 6; dhcp->options[o++] = 'A'; dhcp->options[o++] = 'n'; dhcp->options[o++] = 'i'; dhcp->options[o++] = 'm'; dhcp->options[o++] = 'O'; dhcp->options[o++] = 'S';
    dhcp->options[o++] = 55; dhcp->options[o++] = 4; 
    dhcp->options[o++] = 1; dhcp->options[o++] = 3; dhcp->options[o++] = 6; dhcp->options[o++] = 15;
    dhcp->options[o++] = 255;

    e1000_send_packet(buffer, 594);
}

void dhcp_request(uint32_t offered_ip, uint32_t server_ip) {
    uint8_t buffer[1024];
    for(int i=0; i<1024; i++) buffer[i] = 0;
    struct ethernet_header* eth = (struct ethernet_header*)buffer;
    for(int i=0; i<6; i++) { eth->dest[i] = 0xFF; eth->src[i] = my_mac[i]; }
    eth->type = swap16(0x0800);

    struct ipv4_header* ip = (struct ipv4_header*)(eth + 1);
    ip->version_ihl = 0x45;
    ip->len = swap16(580);
    ip->ttl = 64; ip->proto = 17;
    ip->src_ip = 0; ip->dest_ip = 0xFFFFFFFF;
    ip->chksum = 0; ip->chksum = ip_checksum(ip, 20);

    struct udp_header* udp = (struct udp_header*)(ip + 1);
    udp->src_port = swap16(68); udp->dest_port = swap16(67);
    udp->len = swap16(560);
    udp->chksum = 0;

    struct dhcp_packet* dhcp = (struct dhcp_packet*)(udp + 1);
    dhcp->op = 1; dhcp->htype = 1; dhcp->hlen = 6;
    last_xid = global_xid_counter++;
    dhcp->xid = swap32(last_xid);
    dhcp->flags = 0;
    dhcp->cookie = swap32(0x63825363);
    for(int i=0; i<6; i++) dhcp->chaddr[i] = my_mac[i];

    int o = 0;
    dhcp->options[o++] = 53; dhcp->options[o++] = 1; dhcp->options[o++] = 3; 
    dhcp->options[o++] = 50; dhcp->options[o++] = 4; *(uint32_t*)(dhcp->options + o) = offered_ip; o += 4;
    dhcp->options[o++] = 54; dhcp->options[o++] = 4; *(uint32_t*)(dhcp->options + o) = server_ip; o += 4;
    dhcp->options[o++] = 255;

    e1000_send_packet(buffer, 594);
}


void ntp_sync(uint32_t ntp_server_ip) {
    if (!dhcp_ok) {
        dhcp_discover();
        return;
    }
    
    if (gateway_mac[0] == 0 && gateway_mac[1] == 0) {
        arp_request(gateway_ip);
    }

    uint8_t buffer[256];
    for(int i=0; i<256; i++) buffer[i] = 0;
    struct ethernet_header* eth = (struct ethernet_header*)buffer;
    struct ipv4_header* ip = (struct ipv4_header*)(eth + 1);
    struct udp_header* udp = (struct udp_header*)(ip + 1);
    struct ntp_packet* ntp = (struct ntp_packet*)(udp + 1);

    if (gateway_mac[0] != 0 || gateway_mac[1] != 0) {
        for(int i=0; i<6; i++) eth->dest[i] = gateway_mac[i];
    } else {
        for(int i=0; i<6; i++) eth->dest[i] = 0xFF;
    }
    for(int i=0; i<6; i++) eth->src[i] = my_mac[i];
    eth->type = swap16(0x0800);

    ip->version_ihl = 0x45;
    ip->len = swap16(sizeof(struct ipv4_header) + sizeof(struct udp_header) + sizeof(struct ntp_packet));
    ip->ttl = 64; ip->proto = 17;
    ip->src_ip = my_ip; ip->dest_ip = ntp_server_ip;
    ip->chksum = 0; ip->chksum = ip_checksum(ip, 20);

    udp->src_port = swap16(123); udp->dest_port = swap16(123);
    udp->len = swap16(sizeof(struct udp_header) + sizeof(struct ntp_packet));
    udp->chksum = 0;
    ntp->li_vn_mode = 0x23;

    e1000_send_packet(buffer, sizeof(struct ethernet_header) + sizeof(struct ipv4_header) + sizeof(struct udp_header) + sizeof(struct ntp_packet));
}

void net_poll() {
    uint8_t buffer[2048];
    uint16_t len;
    while(e1000_receive_packet(buffer, &len)) {
        packet_counter++;
        received_any = 1;
        struct ethernet_header* eth = (struct ethernet_header*)buffer;
        
        if (eth->type == swap16(0x0806)) { 
            uint8_t* arp = (uint8_t*)(eth + 1);
            if (arp[7] == 1) { 
                uint32_t target_ip = *(uint32_t*)(arp + 24);
                if (target_ip == my_ip && my_ip != 0) {
                    arp[7] = 2; 
                    for(int i=0; i<6; i++) { eth->dest[i] = eth->src[i]; eth->src[i] = my_mac[i]; }
                    uint32_t sender_ip = *(uint32_t*)(arp + 14);
                    uint8_t* sender_mac = arp + 8;
                    for(int i=0; i<6; i++) { arp[18+i] = sender_mac[i]; arp[8+i] = my_mac[i]; }
                    *(uint32_t*)(arp + 24) = sender_ip;
                    *(uint32_t*)(arp + 14) = my_ip;
                    e1000_send_packet(buffer, 42);
                }
            } else if (arp[7] == 2) { 
                if (*(uint32_t*)(arp + 14) == gateway_ip) {
                    for(int i=0; i<6; i++) gateway_mac[i] = arp[8+i];
                }
            }
        }
        else if(eth->type == swap16(0x0800)) { 
            struct ipv4_header* ip = (struct ipv4_header*)(eth + 1);
            if(ip->proto == 17) { 
                struct udp_header* udp = (struct udp_header*)((uint8_t*)ip + ((ip->version_ihl & 0xF) * 4));
                if (swap16(udp->dest_port) == 68) {
                    struct dhcp_packet* dhcp = (struct dhcp_packet*)(udp + 1);
                    uint32_t rx_xid = swap32(dhcp->xid);
                    if (rx_xid == last_xid || rx_xid == (last_xid - 1)) {
                        uint8_t type = 0;
                        uint8_t* opt = dhcp->options;
                        uint32_t server = 0;
                        while(*opt != 255 && opt < (dhcp->options + 312)) {
                            if (*opt == 0) { opt++; continue; }
                            if(*opt == 53) type = opt[2];
                            if(*opt == 54) server = *(uint32_t*)(opt + 2);
                            if(*opt == 3) gateway_ip = *(uint32_t*)(opt + 2);
                            opt += (opt[1] + 2);
                        }
                        if (type == 2) { 
                            dhcp_server_ip = server;
                            dhcp_request(dhcp->yiaddr, dhcp_server_ip);
                            extern int net_status; net_status = 8; 
                        } else if (type == 5) { 
                            my_ip = dhcp->yiaddr;
                            dhcp_ok = 1;
                        }
                    }
                }
                else if(swap16(udp->src_port) == 123) {
                    struct ntp_packet* ntp = (struct ntp_packet*)(udp + 1);
                    if (swap32(ntp->trans_ts_sec) > 0) {
                        last_ntp_timestamp = (uint64_t)swap32(ntp->trans_ts_sec) - 2208988800ULL;
                        extern int net_status; net_status = 3;
                    }
                }
            }
        }
    }
}

uint64_t ntp_get_time() { return last_ntp_timestamp; }

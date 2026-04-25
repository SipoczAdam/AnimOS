#include "net.h"
#include "../drivers/e1000.h"

static uint32_t my_ip = 0;
static uint8_t my_mac[6];
static uint32_t gateway_ip = 0;
static uint8_t gateway_mac[6] = {0, 0, 0, 0, 0, 0};
static uint64_t last_ntp_timestamp = 0;
int received_any = 0; 

static inline uint16_t swap16(uint16_t v) { return (v << 8) | (v >> 8); }
static inline uint32_t swap32(uint32_t v) {
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
}

static uint16_t ip_checksum(void* vdata, size_t length) {
    uint16_t* ptr = (uint16_t*)vdata;
    uint32_t acc = 0;
    for (size_t i = 0; i < length / 2; i++) acc += ptr[i];
    if (length & 1) acc += ((uint8_t*)vdata)[length - 1];
    while (acc >> 16) acc = (acc & 0xFFFF) + (acc >> 16);
    return ~((uint16_t)acc);
}

void net_init(uint32_t ip) {
    my_ip = ip;
    gateway_ip = (ip & 0x00FFFFFF) | (2 << 24); // Alapértelmezett QEMU gateway: .2
    e1000_get_mac(my_mac);
}

void arp_request(uint32_t target_ip) {
    uint8_t buffer[64];
    for(int i=0; i<64; i++) buffer[i] = 0;
    struct ethernet_header* eth = (struct ethernet_header*)buffer;
    for(int i=0; i<6; i++) { eth->dest[i] = 0xFF; eth->src[i] = my_mac[i]; }
    eth->type = swap16(0x0806);

    uint8_t* arp = (uint8_t*)(eth + 1);
    arp[0] = 0; arp[1] = 1; // HW: Ethernet
    arp[2] = 8; arp[3] = 0; // Proto: IP
    arp[4] = 6; // HW Len
    arp[5] = 4; // Proto Len
    arp[6] = 0; arp[7] = 1; // Op: Request
    for(int i=0; i<6; i++) arp[8+i] = my_mac[i];
    *(uint32_t*)(arp + 14) = my_ip;
    for(int i=0; i<6; i++) arp[18+i] = 0;
    *(uint32_t*)(arp + 24) = target_ip;

    e1000_send_packet(buffer, 42);
}

void ntp_sync(uint32_t ntp_server_ip) {
    // Mindig küldünk egy ARP-t, hogy lássuk van-e élet
    arp_request(gateway_ip);

    int has_gw = 0;
    for(int i=0; i<6; i++) if(gateway_mac[i] != 0) has_gw = 1;
    
    uint8_t buffer[256];
    for(int i=0; i<256; i++) buffer[i] = 0;

    struct ethernet_header* eth = (struct ethernet_header*)buffer;
    struct ipv4_header* ip = (struct ipv4_header*)(eth + 1);
    struct udp_header* udp = (struct udp_header*)(ip + 1);
    struct ntp_packet* ntp = (struct ntp_packet*)(udp + 1);

    if (has_gw) {
        for(int i=0; i<6; i++) eth->dest[i] = gateway_mac[i];
    } else {
        // Ha nincs meg a GW, broadcast-tal próbálkozunk (néhány router átengedi)
        for(int i=0; i<6; i++) eth->dest[i] = 0xFF;
    }
    
    for(int i=0; i<6; i++) eth->src[i] = my_mac[i];
    eth->type = swap16(0x0800);

    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->len = swap16(sizeof(struct ipv4_header) + sizeof(struct udp_header) + sizeof(struct ntp_packet));
    ip->id = swap16(1);
    ip->flags_offset = 0;
    ip->ttl = 64;
    ip->proto = 17; // UDP
    ip->src_ip = my_ip;
    ip->dest_ip = ntp_server_ip;
    ip->chksum = 0;
    ip->chksum = ip_checksum(ip, sizeof(struct ipv4_header));

    udp->src_port = swap16(123); // NTP port
    udp->dest_port = swap16(123);
    udp->len = swap16(sizeof(struct udp_header) + sizeof(struct ntp_packet));
    udp->chksum = 0;

    ntp->li_vn_mode = 0x23; // V4, Client

    e1000_send_packet(buffer, sizeof(struct ethernet_header) + sizeof(struct ipv4_header) + sizeof(struct udp_header) + sizeof(struct ntp_packet));
}

void net_poll() {
    uint8_t buffer[2048];
    uint16_t len;
    while(e1000_receive_packet(buffer, &len)) {
        received_any = 1;
        struct ethernet_header* eth = (struct ethernet_header*)buffer;
        
        if (eth->type == swap16(0x0806)) { // ARP
            uint8_t* arp = (uint8_t*)(eth + 1);
            uint32_t sender_ip = *(uint32_t*)(arp + 14);
            uint8_t* sender_mac = arp + 8;

            if (arp[7] == 1) { // Request
                uint32_t target_ip = *(uint32_t*)(arp + 24);
                if (target_ip == my_ip) {
                    arp[7] = 2; // Reply
                    for(int i=0; i<6; i++) { eth->dest[i] = eth->src[i]; eth->src[i] = my_mac[i]; }
                    for(int i=0; i<6; i++) { arp[18+i] = sender_mac[i]; arp[8+i] = my_mac[i]; }
                    *(uint32_t*)(arp + 24) = sender_ip;
                    *(uint32_t*)(arp + 14) = my_ip;
                    e1000_send_packet(buffer, 42); // ARP is always 42 bytes
                }
            } else if (arp[7] == 2) { // Reply
                if (sender_ip == gateway_ip) {
                    for(int i=0; i<6; i++) gateway_mac[i] = sender_mac[i];
                }
            }
        }
        else if(eth->type == swap16(0x0800)) { // IP
            struct ipv4_header* ip = (struct ipv4_header*)(eth + 1);
            if(ip->proto == 17) { // UDP
                struct udp_header* udp = (struct udp_header*)((uint8_t*)ip + ((ip->version_ihl & 0xF) * 4));
                if(swap16(udp->src_port) == 123) {
                    struct ntp_packet* ntp = (struct ntp_packet*)(udp + 1);
                    uint32_t seconds = swap32(ntp->trans_ts_sec);
                    if (seconds > 0) {
                        last_ntp_timestamp = (uint64_t)seconds - 2208988800ULL;
                        extern int net_status;
                        net_status = 3;
                    }
                }
            }
        }
    }
}

uint64_t ntp_get_time() { return last_ntp_timestamp; }

#include <stdio.h>
#include <dhcpserver.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"

struct udp_pcb* global_pcb = NULL;

typedef struct 
{
    uint8_t wlan_report_id;
    uint8_t report_format;
    uint8_t data[64];
    uint16_t len;
} hoja_wlan_report_s;

#define UDP_PORT 4444
#define BEACON_MSG_LEN_MAX sizeof(hoja_wlan_report_s)
#define BEACON_TARGET "255.255.255.255"

#define WIFI_SSID_BASE "HOJA_WLAN_123456"
#define WIFI_PASS "HOJA_1234"

typedef enum 
{
    HWLAN_REPORT_PASSTHROUGH = 0x01,
    HWLAN_REPORT_TRANSPORT = 0x02,
    HWLAN_REPORT_HELLO = 0x03,
} hoja_wlan_report_t;

typedef struct 
{
    bool running;
    uint8_t report_format;
    uint8_t mac_address[6];
    char device_name[32];
} dongle_sm_s;

dongle_sm_s _sm = {0};

// When the dongle receives a packet
// it will pass through this function
void _udp_receive_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    if (p) {
        if(p->len != sizeof(hoja_wlan_report_s)) return;

        hoja_wlan_report_s* r = (uint8_t*) p->payload;

        switch(r->wlan_report_id)
        {
            case HWLAN_REPORT_HELLO:
            break;

            case HWLAN_REPORT_PASSTHROUGH:
            break;
        }

        if(!_sm.running)
        {

        }
        
        pbuf_free(p);
    }
}

void _udp_send_tunnel(const void *report, uint16_t len)
{
    if(!global_pcb) global_pcb = udp_new();
    ip_addr_t addr;
    ipaddr_aton(BEACON_TARGET, &addr);
    cyw43_arch_lwip_begin();
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, BEACON_MSG_LEN_MAX, PBUF_RAM);
    uint8_t *req = (uint8_t *)p->payload;
    //memset(req, 0, BEACON_MSG_LEN_MAX);
    memcpy(&req[0], report, len);
    err_t er = udp_sendto(global_pcb, p, &addr, UDP_PORT);
    pbuf_free(p);
    cyw43_arch_lwip_end();
}

// This function should be called by any input cores
// when data is RECEIVED so that it can be forwarded to
// the gamepad
void wlan_report_tunnel_out(const uint8_t *data, uint16_t len)
{

}

void wlan_report_tunnel_in(const uint8_t *data, uint16_t len)
{
    
}

int main()
{
    stdio_init_all();

    // Initialise the Wi-Fi chip
    if (cyw43_arch_init()) {
        printf("Wi-Fi init failed\n");
        return -1;
    }

    // Enable wifi station
    cyw43_arch_enable_sta_mode();

    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms("Your Wi-Fi SSID", "Your Wi-Fi Password", CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to connect.\n");
        return 1;
    } else {
        printf("Connected.\n");
        // Read the ip address in a human readable way
        uint8_t *ip_address = (uint8_t*)&(cyw43_state.netif[0].ip_addr.addr);
        printf("IP address %d.%d.%d.%d\n", ip_address[0], ip_address[1], ip_address[2], ip_address[3]);
    }

    while (true) {
        printf("Hello, world!\n");
        sleep_ms(1000);
    }
}

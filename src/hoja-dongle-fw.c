#include <stdio.h>
#include <hoja_types.h>
#include <dhcpserver.h>
#include <dongle.h>

#include "cores/cores.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/bootrom.h"
#include "hardware/watchdog.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "utilities/interval.h"

struct udp_pcb *global_tx_pcb = NULL;
struct udp_pcb *global_rx_pcb = NULL;
ip_addr_t global_gw;

#define UDP_PORT 4444
#define BEACON_MSG_LEN_MAX sizeof(hoja_wlan_report_s)
#define BEACON_TARGET "255.255.255.255"

#define WIFI_SSID_BASE "HOJA_WLAN_1234"
#define WIFI_PASS "HOJA_1234"

typedef struct
{
    bool running;
    bool connected;
    uint8_t report_format;
    uint8_t mac_address[6];
    char device_name[32];
} dongle_sm_s;

dongle_sm_s _sm = {0};

typedef struct
{
    bool unread;
    uint8_t report_format;
} dongle_init_msg_s;

volatile dongle_init_msg_s _msg;

static hoja_wlan_report_s _rx_report_buffer;

// When the dongle receives a packet
// it will pass through this function
void _udp_receive_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    if (!p)
        return;

    if (p->tot_len == sizeof(hoja_wlan_report_s))
    {
        // Copy the packet into our static buffer - NO MALLOC
        // This handles fragmentation and ensures data persists after pbuf_free
        pbuf_copy_partial(p, &_rx_report_buffer, sizeof(hoja_wlan_report_s), 0);

        hoja_wlan_report_s *r = &_rx_report_buffer;

        switch (r->wlan_report_id)
        {
        case HWLAN_REPORT_HELLO:
            _msg.report_format = r->report_format;
            _msg.unread = true;
            break;

        case HWLAN_REPORT_PASSTHROUGH:
            core_input_report_tunnel(r);
            break;
        }
    }

    pbuf_free(p);
}

static hoja_wlan_report_s _tx_report_buffer;

void _udp_send_tunnel(const void *report, uint16_t len)
{
    if (!global_tx_pcb)
        return;

    ip_addr_t addr;
    ipaddr_aton(BEACON_TARGET, &addr);

    if (len > sizeof(hoja_wlan_report_s))
        len = sizeof(hoja_wlan_report_s);
    memcpy(&_tx_report_buffer, report, len);

    // Use PBUF_REF to wrap the static buffer.
    // This allocates only the pbuf structure from lwIP's static pool, NO heap.
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_REF);
    if (!p)
        return;

    p->payload = &_tx_report_buffer;

    udp_sendto(global_tx_pcb, p, &addr, UDP_PORT);
    pbuf_free(p);
}

// This function should be called by any input cores
// when data is RECEIVED so that it can be forwarded to
// the gamepad
void wlan_report_tunnel_out(hoja_wlan_report_s report)
{
    hoja_wlan_report_s r = report;
    _udp_send_tunnel(&r, sizeof(hoja_wlan_report_s));
}

bool wlan_is_connected()
{
    return _sm.connected;
}

uint32_t pm_mode = 0;

void _wlan_network_task()
{
    // Initialise the Wi-Fi chip
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_USA))
    {
        printf("Wi-Fi init failed\n");
        return;
    }
    
    cyw43_wifi_ap_set_channel(&cyw43_state, 4);
    cyw43_arch_enable_ap_mode(WIFI_SSID_BASE, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK);

    pm_mode = cyw43_pm_value(CYW43_NO_POWERSAVE_MODE, 1, 1, 1, 1);
    cyw43_wifi_pm(&cyw43_state, pm_mode);

#define IP(x) (x)
    ip4_addr_t mask;
    IP(global_gw).addr = PP_HTONL(CYW43_DEFAULT_IP_AP_ADDRESS);
    IP(mask).addr = PP_HTONL(CYW43_DEFAULT_IP_MASK);
#undef IP

    dhcp_server_t dhcp_server;
    dhcp_server_init(&dhcp_server, &global_gw, &mask);

    global_tx_pcb = udp_new();
    global_rx_pcb = udp_new();

    // Start UDP listening
    if (global_rx_pcb)
    {
        udp_bind(global_rx_pcb, IP_ANY_TYPE, UDP_PORT);
        udp_recv(global_rx_pcb, _udp_receive_cb, NULL);
    }

    for (;;)
    {
        // Send ping every 250ms
        static interval_s interval = {0};
        if (interval_run(time_us_64(), 250 * 1000, &interval))
        {
            hoja_wlan_report_s ping = {
                .wlan_report_id = HWLAN_REPORT_PING,
            };
            wlan_report_tunnel_out(ping);
        }
    }
}

int main()
{
    stdio_init_all();


    multicore_launch_core1(_wlan_network_task);

    // DEBUG
    //_msg.report_format = CORE_REPORTFORMAT_GAMECUBE;
    //_msg.unread = true;

    for (;;)
    {
        // Init message handle on core 0
        if (_msg.unread)
        {
            if (_sm.running)
            {
                if (_msg.report_format != _sm.report_format)
                {
                    // Reboot to clear dongle info
                    watchdog_reboot(0, 0, 0);
                }
                else
                {
                    // We are already in the correct report mode
                    // we can continue...
                    _sm.connected = true;
                }
            }
            else
            {
                switch (_msg.report_format)
                {
                case CORE_REPORTFORMAT_SLIPPI:
                case CORE_REPORTFORMAT_SINPUT:
                case CORE_REPORTFORMAT_XINPUT:
                case CORE_REPORTFORMAT_N64:
                case CORE_REPORTFORMAT_GAMECUBE:
                    // DEBUG break;

                    if (core_init(_msg.report_format))
                    {
                        _sm.report_format = _msg.report_format;
                        _sm.running = true;
                        _sm.connected = true;
                    }
                    break;

                // Do nothing
                default:
                    break;
                }
            }
            _msg.unread = false;
        }

        core_task(time_us_64());
    }
}

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
#include "utilities/crosscore_snapshot.h"

static struct udp_pcb *pcb = NULL;
static ip_addr_t client_addr;
static u16_t client_port = 0;
static bool client_known = false;

#define UDP_PORT 4444
#define BEACON_MSG_LEN_MAX sizeof(hoja_wlan_report_s)

#define WIFI_SSID_BASE "HOJA_WLAN_1234"
#define WIFI_PASS "HOJA_1234"

typedef struct 
{
    uint16_t session_sig;
    uint16_t gamepad_sig;
    uint16_t dongle_sig;
} wlan_sigs_s;

wlan_sigs_s _wlan_sigs = {0};

// Whether or not we block new reliable packets
volatile uint32_t reliable_tx_block = 0;

SNAPSHOT_TYPE(wlan_pkt, hoja_wlan_report_s);
// Our OUTGOING reliable packet snapshot
snapshot_wlan_pkt_t snap_reliable_pkt;
// Our OUTGOING unreliable packet snapshot
snapshot_wlan_pkt_t snap_unreliable_pkt;
// Our INPUT packet snapshot (UDP from gamepad)
snapshot_wlan_pkt_t snap_input_pkt;


SNAPSHOT_TYPE(wlan_status_pkt, hoja_wlan_status_s);
// Our snapshot for our status data
snapshot_wlan_status_pkt_t snap_status_data;

hoja_wlan_status_s _this_status = {.connection_status = 0};

hoja_wlan_status_s* dongle_current_status()
{
    return &_this_status;
}

void dongle_update_rumble(uint8_t rumble_left, uint8_t rumble_right, uint8_t brake_left, uint8_t brake_right)
{
    _this_status.rumble_left = rumble_left;
    _this_status.rumble_right = rumble_right;
    _this_status.brake_left = brake_left;
    _this_status.brake_right = brake_right;
    snapshot_wlan_status_pkt_write(&snap_status_data, &_this_status);
}

void dongle_update_transport_status(uint8_t transport_status)
{
    _this_status.transport_status = transport_status;
    snapshot_wlan_status_pkt_write(&snap_status_data, &_this_status);
}

void dongle_update_connection_status(uint8_t connection_status)
{
    _this_status.connection_status = connection_status;
    snapshot_wlan_status_pkt_write(&snap_status_data, &_this_status);
}

// When the dongle receives a packet
// it will pass through this function
void _udp_receive_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    if (!p) return;

    bool send_hello = false;

    if (p->tot_len == sizeof(hoja_wlan_report_s))
    {
        hoja_wlan_report_s in_pkt;

        static hoja_wlan_report_s last_unreliable_pkt = {0};

        hoja_wlan_report_s this_pkt = {0};

        // Copy pbuf data to our in_pkt struct
        pbuf_copy_partial(p, &in_pkt, sizeof(hoja_wlan_report_s), 0);

        // Threadsafe copy input UDP for later processing
        snapshot_wlan_pkt_write(&snap_input_pkt, &in_pkt);

        // Allocate pbuf memory from PBUF_RAM for TX packet
        struct pbuf *txp = pbuf_alloc(PBUF_TRANSPORT, sizeof(hoja_wlan_report_s), PBUF_RAM);
        // Alloc success check
        if(!txp)  goto udp_end_clear;

        // If we don't know our client yet,
        // copy this data if the address doesn't match
        if(_wlan_sigs.session_sig != in_pkt.session_sig)
        {
            _wlan_sigs.session_sig = in_pkt.session_sig;

            ip_addr_copy(client_addr, *addr);
            client_port = port;
            client_known = true;

            dongle_update_connection_status(WLAN_CONNSTAT_CONNECTED);

            // If we have a new client
            // we need to say 'hello' which
            // sends a packet to identify 
            // that the connection is complete
            goto hello_send;
        }

        // Check if we are currently waiting for an ACK on an in-flight reliable packet
        if(reliable_tx_block)
        {
            snapshot_wlan_pkt_read(&snap_reliable_pkt, &this_pkt);
            // The gamepad echoes back timestamp_dongle in its reply to signal an ACK
            // If the in packet dongle timestamp matches, we can free up the block
            if(in_pkt.dongle_sig == this_pkt.dongle_sig)
            {
                // ACK confirmed - unblock and load the next pending TX packet
                __dmb();
                reliable_tx_block = 0;
                goto unreliable_check;
            }
            else
            {
                // ACK not confirmed - resend our last reliable pkt
                // Assigned to this_pkt
                goto outbox_send;
            }
        }
        else goto unreliable_check;

hello_send:
        this_pkt.wlan_report_id = HWLAN_REPORT_HELLO;
        this_pkt.dongle_sig = get_rand_64() & 0xFFFF;
        goto outbox_send;

unreliable_check:
        snapshot_wlan_pkt_read(&snap_unreliable_pkt, &this_pkt);
        if(this_pkt.dongle_sig != last_unreliable_pkt.dongle_sig)
        {
            // We have a new unreliable packet to send
            // Save to our last unreliable packet storage space
            last_unreliable_pkt = this_pkt;
            goto outbox_send;
        }

        // Fall through to status_send as a last resort
status_send:
        // Generate a status report
        hoja_wlan_status_s this_stat;
        snapshot_wlan_status_pkt_read(&snap_status_data, &this_stat);
        this_pkt.wlan_report_id = HWLAN_REPORT_STATUS_UNRELIABLE;
        this_pkt.dongle_sig = get_rand_64() & 0xFFFF;
        memcpy(this_pkt.data, &this_stat, sizeof(hoja_wlan_status_s));
        this_pkt.len = sizeof(hoja_wlan_status_s);

outbox_send:
        memcpy(txp->payload, &this_pkt, sizeof(hoja_wlan_report_s));
        err_t err = udp_sendto(pcb, txp, &client_addr, client_port);
        pbuf_free(txp);

        if(err != ERR_OK) 
        {
            // Failed to send
        }
        else 
        {
            // We sent OK
        }
    }

udp_end_clear:
    // Clean up the pbuf for the received packet
    pbuf_free(p);
}

void wlan_send_reliable_report(hoja_wlan_report_s *report)
{
    if(reliable_tx_block) return;
    __dmb();
    reliable_tx_block = true;
    // Ensure timestamp is updated
    report->dongle_sig = get_rand_64() & 0xFFFF;
    snapshot_wlan_pkt_write(&snap_reliable_pkt, report);
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
    
    cyw43_wifi_ap_set_channel(&cyw43_state, 6);
    cyw43_arch_enable_ap_mode(WIFI_SSID_BASE, WIFI_PASS, CYW43_AUTH_WPA2_AES_PSK);
    cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);

    cyw43_wifi_set_roam_enabled(&cyw43_state, false);
    cyw43_wifi_set_interference_mode(&cyw43_state, CYW43_IFMODE_NONE);

    // Start DHCP server
    static dhcp_server_t dhcp_server;
    ip4_addr_t ap_ip, ap_netmask;
    IP4_ADDR(&ap_ip, 192, 168, 4, 1);
    IP4_ADDR(&ap_netmask, 255, 255, 255, 0);
    dhcp_server_init(&dhcp_server, &ap_ip, &ap_netmask);

    pcb = udp_new();

    udp_bind(pcb, IP_ANY_TYPE, UDP_PORT);
    udp_recv(pcb, _udp_receive_cb, NULL);

    for (;;)
    {
        tight_loop_contents();
    }
}

bool _dongle_running = false;
uint8_t _dongle_format = 0xFF;

int main()
{
    stdio_init_all();

    // Write blank packets
    hoja_wlan_report_s blank = {0};
    snapshot_wlan_pkt_write(&snap_reliable_pkt, &blank);
    snapshot_wlan_pkt_write(&snap_unreliable_pkt, &blank);
    snapshot_wlan_pkt_write(&snap_input_pkt, &blank);

    hoja_wlan_status_s sblank = {0};
    snapshot_wlan_status_pkt_write(&snap_status_data, &sblank);

    multicore_launch_core1(_wlan_network_task);

    for (;;)
    {
        // Read our RX mailbox
        hoja_wlan_report_s rx;
        snapshot_wlan_pkt_read(&snap_input_pkt, &rx);

        static uint64_t last_rx_timestamp = 0;

        // Check if the message is new
        if(rx.gamepad_sig != _wlan_sigs.gamepad_sig)
        {
            _wlan_sigs.gamepad_sig = rx.gamepad_sig;

            // Take action on it here
            switch(rx.wlan_report_id)
            {
                case HWLAN_REPORT_HELLO:
                if(_dongle_running)
                {
                    if(_dongle_format != rx.report_format)
                    {
                        //cyw43_arch_deinit();
                        //sleep_ms(500);
                        watchdog_reboot(0, 0, 0);
                    }
                }
                else
                {
                    switch(rx.report_format)
                    {
                    case CORE_REPORTFORMAT_SLIPPI:
                    case CORE_REPORTFORMAT_SINPUT:
                    case CORE_REPORTFORMAT_XINPUT:
                    case CORE_REPORTFORMAT_N64:
                    case CORE_REPORTFORMAT_GAMECUBE:
                        if (core_init(rx.report_format))
                        {
                            _dongle_format = rx.report_format;
                            _dongle_running = true;
                        }
                        break;

                    // Do nothing
                    default:
                        break;
                    }
                }
                break;

                case HWLAN_REPORT_CORE_UNRELIABLE:
                if(rx.report_format==_dongle_format)
                core_input_report_tunnel(&rx);
                break;

                default:
                // Unhandled
                break;
            }
        }

        core_task(time_us_64());
    }
}
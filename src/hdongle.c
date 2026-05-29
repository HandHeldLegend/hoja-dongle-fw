#include <stdio.h>
#include <dhcpserver.h>
#include <dongle.h>

#include "cores/cores.h"
#include "dongle_wlan.h"
#include "dongle_wlan_core0.h"
#include "dongle_wlan_core1.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "utilities/crosscore_snapshot.h"
#include "utilities/crosscore_fifo.h"

#define WIFI_SSID_BASE "HOJA_WLAN_1234"
#define WIFI_PASS "HOJA_1234"

static struct udp_pcb *pcb = NULL;

dongle_status_u _status = {0};
SNAPSHOT_TYPE(status, dongle_status_u);
snapshot_status_t _ss_status;

/* --- Ingress FIFO: UDP callback (ISR) --- */
typedef struct
{
    dongle_pkt_s pkt;
    ip_addr_t addr;
    u16_t port;
} dongle_wlan_rx_frame_t;

CROSSCORE_FIFO_TYPE(udp_rx, dongle_wlan_rx_frame_t, DONGLE_WLAN_QUEUE_LEN);
static fifo_udp_rx_t _cf_udp_rx;

// Push frames here from ISR context
static bool _udp_rx_queue_push_core1(const dongle_wlan_rx_frame_t *frame)
{
    return fifo_udp_rx_push(&_cf_udp_rx, frame);
}

// In Core 1 loop, pop Core 1 frames
static bool _udp_rx_queue_pop_core1(dongle_wlan_rx_frame_t *frame)
{
    return fifo_udp_rx_pop(&_cf_udp_rx, frame);
}

CROSSCORE_FIFO_TYPE(rx_reliable, dongle_pkt_s, DONGLE_WLAN_QUEUE_LEN);
static fifo_rx_reliable_t _cf_rx_reliable;

// In Core 1 loop, when the gamepad sends us reliable packets
// they are loaded into a FIFO.
static bool _rx_reliable_queue_push_core1(const dongle_pkt_s *pkt)
{
    return fifo_rx_reliable_push(&_cf_rx_reliable, pkt);
}

// In our core 0 loop, we can pop reliable packets
// from the queue that originates from the gamepad.
static bool _rx_reliable_queue_pop_core0(dongle_pkt_s *pkt)
{
    return fifo_rx_reliable_pop(&_cf_rx_reliable, pkt);
}

/* --- RX: received unreliable core packet --- */
SNAPSHOT_TYPE(rx_unreliable, dongle_pkt_s);
snapshot_rx_unreliable_t _ss_rx_unreliable;

static void _rx_unreliable_write_core1(dongle_pkt_s *pkt)
{
    snapshot_rx_unreliable_write(&_ss_rx_unreliable, pkt);
}

static void _rx_wake_handle(dongle_pkt_s *pkt)
{

}

static void _rx_handle_ack(uint16_t ack)
{

}

bool hdongle_rx_unreliable_read_core0(dongle_pkt_s *pkt)
{
    snapshot_rx_unreliable_read(&_ss_rx_unreliable, pkt);
    if(pkt->len) return true;
    return false;
}

static void _udp_rx_queue_consume_core1(void)
{
    dongle_wlan_rx_frame_t frame;
    const dongle_pkt_s *pkt = NULL;
    while(_udp_rx_queue_pop(&frame))
    {
        pkt = &frame.pkt;

        _rx_handle_ack(pkt->ack);

        switch(pkt->id)
        {
            // Unhandled
            default: 
            case DONGLE_PID_STATUS:
            break;

            // Set up WLAN session
            // Core initialization, etc. 
            case DONGLE_PID_WAKE:
            _rx_wake_handle(pkt);
            break;

            // Add to our reliable queue
            case DONGLE_PID_CORE_RELIABLE:
            _rx_reliable_queue_push_core1(pkt);
            break;

            case DONGLE_PID_CORE_UNRELIABLE:
            _rx_unreliable_write_core1(pkt);
            break;
        }
    }
}

// Callback when we receive a packet from the gamepad
static void _udp_rx_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;

    /* lwIP UDP callback — keep this minimal; no protocol logic in ISR context. */
    if (!p || p->tot_len != sizeof(dongle_pkt_s) || !addr)
    {
        return;
    }
    else
    {
        dongle_wlan_rx_frame_t frame;
        pbuf_copy_partial(p, &frame.pkt, sizeof(dongle_pkt_s), 0);
        ip_addr_copy(frame.addr, *addr);
        frame.port = port;
        _ingress_push(&frame);
        pbuf_free(p);
    }
}

CROSSCORE_FIFO_TYPE(tx_reliable, dongle_pkt_s, DONGLE_WLAN_QUEUE_LEN);
static fifo_tx_reliable_t _cf_tx_reliable;

// In our core 0 tasks, we can call this
// to push reliable packets to the gamepad
static bool _tx_reliable_push_queue_core0(dongle_pkt_s *pkt)
{
    return fifo_tx_reliable_push(&_cf_tx_reliable, pkt);
}

// In our core 1 tasks, we can use this to see
// if we have any reliable packets that must be
// sent
static bool _tx_reliable_pop_queue_core1(dongle_pkt_s *pkt)
{
    return fifo_tx_reliable_pop(&_cf_tx_reliable, pkt);
}

void hdongle_update_rumble(uint8_t rumble_left, uint8_t rumble_right, bool brake_left, bool brake_right)
{
    _status.rumble.left = rumble_left;
    _status.rumble.right = rumble_right;
    _status.brake.left = brake_left;
    _status.brake.right = brake_right;
    snapshot_wlan_status_write(&_ss_status, &_status);
}

void hdongle_update_connection_status(dongle_connection_t connection)
{
    _status.connection = (uint8_t)connection;
    snapshot_wlan_status_write(&_ss_status, &_status);
}

void hdongle_update_player_number(uint8_t player)
{
    _status.player_number = player;
    snapshot_wlan_status_write(&_ss_status, &_status);
}

// Core 1 loop (WLAN CORE)
void hdongle_core1(uint64_t time)
{
    // Process all UDP packets 
    // received from gamepad
    _udp_rx_queue_consume_core1();
}

void hdongle_core0_send_reliable_outputreport(uint8_t *data, uint16_t len)
{

}

bool hdongle_core0_consume_reliable_inputreport(uint8_t *data, uint16_t *len)
{
    dongle_pkt_s pkt;

    // See if we have any reliable messages needing
    // processing by our input task
    if(_rx_reliable_queue_pop_core0(&pkt))
    {
        memcpy(data, pkt.data, pkt.len);
        return true;
    }

    return false;
}

// Core 0 loop (TRANSPORT CORE)
void hdongle_core0(uint64_t time)
{


    core_task(time);
}

// Core 1 entrypoint
void main_core1(void)
{
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

    static dhcp_server_t dhcp_server;
    ip4_addr_t ap_ip, ap_netmask;
    IP4_ADDR(&ap_ip, 192, 168, 4, 1);
    IP4_ADDR(&ap_netmask, 255, 255, 255, 0);
    dhcp_server_init(&dhcp_server, &ap_ip, &ap_netmask);

    pcb = udp_new();
    udp_bind(pcb, IP_ANY_TYPE, DONGLE_WLAN_UDP_PORT);
    udp_recv(pcb, _udp_rx_cb, NULL);

    dongle_wlan_core1_init(pcb);

    for (;;)
    {
        hdongle_core1(time_us_64());
    }
}

int main(void)
{
    stdio_init_all();

    // N64 mode needs to be init first
    // to ensure we meet the timing requirements
    // for the first N64 mode poll/identity check
    core_init(CORE_REPORTFORMAT_N64, NULL);

    // Launch main for core1
    multicore_launch_core1(main_core1);

    for (;;)
    {
        hdongle_core0(time_us_64());
    }
}

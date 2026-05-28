#include "dongle_wlan_core1.h"

#include <string.h>

#include "dongle_wlan.h"
#include "dongle_wlan_core0.h"
#include "utilities/crosscore_snapshot.h"
#include "utilities/interval.h"

#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"

#include "pico/stdlib.h"

typedef enum
{
    WLAN_STATE_AWAITING = 0,
    WLAN_STATE_CONNECTED,
} wlan_state_t;

typedef struct
{
    uint8_t data[64];
    uint16_t len;
} out_item_t;

/* --- Inbox: raw UDP frames (callback → poll) --- */

static dongle_wlan_rx_frame_t _ingress[DONGLE_WLAN_QUEUE_LEN];
static uint8_t _ingress_head;
static uint8_t _ingress_tail;
static uint8_t _ingress_count;

/* --- Outbox: host → gamepad reliable payloads --- */

static out_item_t _outbox[DONGLE_WLAN_QUEUE_LEN];
static uint8_t _out_head;
static uint8_t _out_tail;
static uint8_t _out_count;

static struct udp_pcb *_pcb;
static ip_addr_t _client_addr;
static u16_t _client_port;
static bool _client_known;
static wlan_state_t _wlan_state = WLAN_STATE_AWAITING;
static dongle_session_s _active_session;
static uint64_t _last_rx_us;

static uint16_t _tx_counter;
static uint16_t _outstanding_counter;
static out_item_t _inflight;

static interval_s _wake_interval;
static interval_s _pump_interval;

static uint16_t session_pack(const dongle_session_s *s)
{
    uint16_t v = 0;
    memcpy(&v, s, sizeof(uint16_t));
    return v;
}

static bool _ingress_push(const dongle_wlan_rx_frame_t *frame)
{
    if (_ingress_count >= DONGLE_WLAN_QUEUE_LEN)
    {
        return false;
    }
    _ingress[_ingress_tail] = *frame;
    _ingress_tail = (uint8_t)((_ingress_tail + 1) % DONGLE_WLAN_QUEUE_LEN);
    _ingress_count++;
    return true;
}

static bool _ingress_pop(dongle_wlan_rx_frame_t *frame)
{
    if (_ingress_count == 0)
    {
        return false;
    }
    *frame = _ingress[_ingress_head];
    _ingress_head = (uint8_t)((_ingress_head + 1) % DONGLE_WLAN_QUEUE_LEN);
    _ingress_count--;
    return true;
}

static bool udp_send_to(const dongle_pkt_s *pkt, const ip_addr_t *addr, u16_t port)
{
    if (!_pcb || !addr)
    {
        return false;
    }

    struct pbuf *txp = pbuf_alloc(PBUF_TRANSPORT, sizeof(dongle_pkt_s), PBUF_RAM);
    if (!txp)
    {
        return false;
    }

    memcpy(txp->payload, pkt, sizeof(dongle_pkt_s));
    err_t err = udp_sendto(_pcb, txp, addr, port);
    pbuf_free(txp);
    return err == ERR_OK;
}

static bool wlan_udp_send(const dongle_pkt_s *pkt)
{
    if (!_client_known)
    {
        return false;
    }
    return udp_send_to(pkt, &_client_addr, _client_port);
}

static void bind_client(const ip_addr_t *addr, u16_t port)
{
    ip_addr_copy(_client_addr, *addr);
    _client_port = port;
    _client_known = true;
    _wlan_state = WLAN_STATE_CONNECTED;
    dongle_update_connection_status(DONGLE_CONN_CONNECTED);
}

static void reset_link(void)
{
    _client_known = false;
    _wlan_state = WLAN_STATE_AWAITING;
    _out_head = 0;
    _out_tail = 0;
    _out_count = 0;
    _outstanding_counter = 0;
    memset(&_inflight, 0, sizeof(_inflight));
    memset(&_active_session, 0, sizeof(_active_session));
    _last_rx_us = 0;
    dongle_update_connection_status(DONGLE_CONN_IDLE);
    dongle_wlan_core0_signal_link_timeout();
}

static void build_pkt(dongle_pkt_s *pkt, dongle_pid_t pid, uint16_t counter, const uint8_t *data, uint16_t len)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->session = session_pack(&_active_session);
    pkt->counter = counter;
    pkt->id = (uint8_t)pid;
    pkt->len = len;
    if (data && len > 0)
    {
        uint16_t n = len > sizeof(pkt->data) ? sizeof(pkt->data) : len;
        memcpy(pkt->data, data, n);
        pkt->len = n;
    }
}

static void handle_ack(uint16_t counter)
{
    if (_outstanding_counter == 0 || counter != _outstanding_counter)
    {
        return;
    }
    _outstanding_counter = 0;
    memset(&_inflight, 0, sizeof(_inflight));
    if (_out_count > 0)
    {
        _out_head = (uint8_t)((_out_head + 1) % DONGLE_WLAN_QUEUE_LEN);
        _out_count--;
    }
}

static void _process_ingress_frame(const dongle_wlan_rx_frame_t *frame)
{
    _last_rx_us = time_us_64();

    if (!_client_known)
    {
        bind_client(&frame->addr, frame->port);
    }

    // Handle types of packets
    dongle_pkt_s *pkt = &frame->pkt;

    switch((dongle_pid_t) pkt->id)
    {
        case DONGLE_PID_WAKE:
        break;

        case DONGLE_PID_CORE_RELIABLE:
        break;

        case DONGLE_PID_CORE_UNRELIABLE:
        break;

        // UNHANDLED
        case DONGLE_PID_STATUS:
        break;
    }
}

static void _drain_ingress(void)
{
    dongle_wlan_rx_frame_t frame;
    while (_ingress_pop(&frame))
    {
        _process_ingress_frame(&frame);
    }
}

static bool send_wake(void)
{
    dongle_pkt_s pkt;
    build_pkt(&pkt, DONGLE_PID_WAKE, 0, NULL, 0);
    if (_client_known)
    {
        return wlan_udp_send(&pkt);
    }
    ip_addr_t bcast;
    IP4_ADDR(&bcast, 192, 168, 4, 255);
    return udp_send_to(&pkt, &bcast, DONGLE_WLAN_UDP_PORT);
}

static bool send_status(void)
{
    dongle_pkt_s pkt;
    dongle_status_u stat;
    dongle_wlan_core0_status_snapshot_read(&stat);
    build_pkt(&pkt, DONGLE_PID_STATUS, 0, (const uint8_t *)&stat, sizeof(dongle_status_u));
    return wlan_udp_send(&pkt);
}

static bool send_core(const out_item_t *item, uint16_t counter)
{
    dongle_pkt_s pkt;
    build_pkt(&pkt, DONGLE_PID_CORE_RELIABLE, counter, item->data, item->len);
    return wlan_udp_send(&pkt);
}

static void pump_tx(uint64_t now_us)
{
    if (_wlan_state == WLAN_STATE_CONNECTED && _client_known && _last_rx_us > 0 &&
        (now_us - _last_rx_us) > DONGLE_WLAN_TIMEOUT_US)
    {
        reset_link();
        return;
    }

    if (!_client_known || _wlan_state == WLAN_STATE_AWAITING)
    {
        if (interval_run(now_us, DONGLE_WLAN_WAKE_INTERVAL_US, &_wake_interval))
        {
            send_wake();
        }
        return;
    }

    if (!interval_run(now_us, DONGLE_WLAN_PUMP_INTERVAL_US, &_pump_interval))
    {
        return;
    }

    if (_outstanding_counter != 0)
    {
        send_core(&_inflight, _outstanding_counter);
        return;
    }

    if (_out_count > 0)
    {
        _inflight = _outbox[_out_head];
        _tx_counter++;
        _outstanding_counter = _tx_counter;
        send_core(&_inflight, _outstanding_counter);
        return;
    }

    send_status();
}

void dongle_wlan_core1_init(struct udp_pcb *pcb)
{
    _pcb = pcb;
    _ingress_head = 0;
    _ingress_tail = 0;
    _ingress_count = 0;
    _client_known = false;
    _wlan_state = WLAN_STATE_AWAITING;
    _last_rx_us = 0;
}

// This is called in ISR context
// We quickly take the packet and queue it into
// our ingress FIFO
void dongle_wlan_core1_on_rx(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)pcb;

    if (!p || p->tot_len != sizeof(dongle_pkt_s) || !addr)
    {
        return;
    }

    dongle_wlan_rx_frame_t frame;
    pbuf_copy_partial(p, &frame.pkt, sizeof(dongle_pkt_s), 0);
    ip_addr_copy(frame.addr, *addr);
    frame.port = port;
    _ingress_push(&frame);
}

void dongle_wlan_core1_poll(uint64_t now_us)
{
    _drain_ingress();
    pump_tx(now_us);
}

bool dongle_wlan_core1_queue_output(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || len > 64 || _out_count >= DONGLE_WLAN_QUEUE_LEN)
    {
        return false;
    }

    out_item_t *slot = &_outbox[_out_tail];
    memcpy(slot->data, data, len);
    slot->len = len;
    _out_tail = (uint8_t)((_out_tail + 1) % DONGLE_WLAN_QUEUE_LEN);
    _out_count++;
    return true;
}

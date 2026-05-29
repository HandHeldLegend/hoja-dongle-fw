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
#include "pico/rand.h"

/*
 * Core 1 WLAN (WiFi / UDP)
 * ------------------------
 * - UDP RX callback queues frames; dongle_wlan_core1_poll() drains ingress.
 * - WAKE with dongle_wake_s → dongle_wlan_wake_post() (deduped) for core 0.
 * - 100 ms wake beacons to DONGLE_GAMEPAD_IP*, 500 Hz STATUS / reliable pump.
 */

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



/* --- Outbox: core 0 host OUT → gamepad, sent reliably with ack handshake --- */

static out_item_t _outbox[DONGLE_WLAN_QUEUE_LEN];
static uint8_t _out_head;
static uint8_t _out_tail;
static uint8_t _out_count;

static struct udp_pcb *_pcb;
static ip_addr_t _gamepad_addr;
static bool _link_up;
static wlan_state_t _wlan_state = WLAN_STATE_AWAITING;
static dongle_session_s _active_session;
static uint64_t _last_rx_us;

/* --- Reliable host→gamepad TX ---
 *
 * Host OUT payloads sit in _outbox until the gamepad confirms delivery.
 *
 * Lifecycle for each queued payload:
 *   1. reliable_start_inflight()  — copy outbox head → _inflight, assign random ack
 *   2. pump_reliable_tx()         — transmit (and retransmit on every pump tick)
 *   3. reliable_on_gamepad_ack()  — gamepad echoes ack in pkt.ack; pop outbox head,
 *                                   then immediately start step 1 for the next item
 *
 * Only one payload is in flight at a time (_outstanding_ack != 0 means waiting).
 */

static uint16_t _last_ack;
static uint16_t _outstanding_ack;
static out_item_t _inflight;

static interval_s _wake_interval;
static interval_s _pump_interval;

static bool reliable_lane_busy(void)
{
    /* Non-zero ack means we have sent a reliable CORE packet and are waiting
     * for the gamepad to echo that same value back in its next packet. */
    return _outstanding_ack != 0;
}

static uint16_t assign_reliable_ack(void)
{
    uint16_t ack;

    /* Each new reliable send gets its own ack token. Retransmissions reuse
     * _outstanding_ack; only a fresh start_inflight() calls here. */
    do
    {
        ack = (uint16_t)(get_rand_32() & 0xFFFFu);
    } while (ack == _last_ack);

    _last_ack = ack;
    return ack;
}

static bool reliable_start_inflight(void)
{
    /* Cannot start while already waiting on a prior ack, or with nothing queued. */
    if (reliable_lane_busy() || _out_count == 0)
    {
        return false;
    }

    /* Copy outbox head into _inflight but do NOT advance _out_head yet — the
     * entry stays queued until the gamepad echoes our ack (retire_inflight). */
    _inflight = _outbox[_out_head];
    _outstanding_ack = assign_reliable_ack();
    return true;
}

static void reliable_retire_inflight(void)
{
    /* Lane is free again — clear the copy we were sending. */
    _outstanding_ack = 0;
    memset(&_inflight, 0, sizeof(_inflight));

    /* Now safe to drop the outbox entry we finished delivering. */
    if (_out_count > 0)
    {
        _out_head = (uint8_t)((_out_head + 1) % DONGLE_WLAN_QUEUE_LEN);
        _out_count--;
    }
}

static void reliable_on_gamepad_ack(uint16_t echoed_ack)
{
    /* Every inbound packet carries pkt.ack. Ignore unless it matches what we
     * are currently waiting on — unrelated traffic uses ack=0 or stale values. */
    if (!reliable_lane_busy() || echoed_ack != _outstanding_ack)
    {
        return;
    }

    /* Valid ack: pop the delivered item, then tee up the next one if queued.
     * poll() drains ingress before pump_tx(), so pump_reliable_tx() sends the
     * newly started item on the same 500 Hz tick when possible. */
    reliable_retire_inflight();
    reliable_start_inflight();
}

static void reliable_reset(void)
{
    _last_ack = 0;
    _outstanding_ack = 0;
    memset(&_inflight, 0, sizeof(_inflight));
}

static uint16_t session_pack(const dongle_session_s *s)
{
    uint16_t v = 0;
    memcpy(&v, s, sizeof(uint16_t));
    return v;
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

static void init_gamepad_addr(void)
{
    IP4_ADDR(&_gamepad_addr, DONGLE_GAMEPAD_IP0, DONGLE_GAMEPAD_IP1, DONGLE_GAMEPAD_IP2, DONGLE_GAMEPAD_IP3);
}

static bool gamepad_addr_matches(const ip_addr_t *addr, u16_t port)
{
    return port == DONGLE_GAMEPAD_PORT && ip_addr_cmp(addr, &_gamepad_addr);
}

static bool gamepad_send(const dongle_pkt_s *pkt)
{
    if (!_pcb)
    {
        return false;
    }
    return udp_send_to(pkt, &_gamepad_addr, DONGLE_GAMEPAD_PORT);
}

static void session_unpack(uint16_t packed, dongle_session_s *s)
{
    memcpy(s, &packed, sizeof(uint16_t));
}

static void handle_wake_from_gamepad(const dongle_pkt_s *pkt, bool link_just_established)
{
    /*
     * WAKE with payload is how the gamepad tells us which core mode to run.
     * Post to the cross-core mailbox; core 0 will consume and core_init().
     *
     * dongle_wlan_wake_post() drops back-to-back duplicates unless this is the
     * first RX after a link drop (link_just_established).
     */
    if (pkt->len < sizeof(dongle_wake_s))
    {
        return;
    }

    session_unpack(pkt->session, &_active_session);
    dongle_wlan_wake_post(pkt, link_just_established);
}

static void link_mark_connected(void)
{
    _link_up = true;
    _wlan_state = WLAN_STATE_CONNECTED;
    dongle_update_connection_status(DONGLE_CONN_CONNECTED);
}

static void link_mark_disconnected(void)
{
    _link_up = false;
    _wlan_state = WLAN_STATE_AWAITING;
    memset(&_active_session, 0, sizeof(_active_session));
    _last_rx_us = 0;
    dongle_update_connection_status(DONGLE_CONN_IDLE);
}

static void reset_link(void)
{
    _out_head = 0;
    _out_tail = 0;
    _out_count = 0;
    reliable_reset();
    link_mark_disconnected();
    dongle_wlan_reset();
    dongle_wlan_core0_signal_link_timeout();
}

static void build_pkt(dongle_pkt_s *pkt, dongle_pid_t pid, uint16_t ack, const uint8_t *data, uint16_t len)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->session = session_pack(&_active_session);
    pkt->ack = ack;
    pkt->id = (uint8_t)pid;
    pkt->len = len;
    if (data && len > 0)
    {
        uint16_t n = len > sizeof(pkt->data) ? sizeof(pkt->data) : len;
        memcpy(pkt->data, data, n);
        pkt->len = n;
    }
}

static void _process_ingress_frame(const dongle_wlan_rx_frame_t *frame)
{
    if (!gamepad_addr_matches(&frame->addr, frame->port))
    {
        return;
    }

    _last_rx_us = time_us_64();

    bool was_up = _link_up;
    if (!_link_up)
    {
        link_mark_connected();
    }

    reliable_on_gamepad_ack(frame->pkt.ack);

    const dongle_pkt_s *pkt = &frame->pkt;

    if (pkt->len == 0 && pkt->id != DONGLE_PID_WAKE)
    {
        return;
    }

    switch ((dongle_pid_t)pkt->id)
    {
    case DONGLE_PID_WAKE:
        handle_wake_from_gamepad(pkt, !was_up);
        break;

    case DONGLE_PID_CORE_RELIABLE:
        dongle_wlan_queue_reliable(pkt->data, pkt->len);
        break;

    case DONGLE_PID_CORE_UNRELIABLE:
        dongle_wlan_write_unreliable(pkt->data, pkt->len);
        break;

    default:
        break;
    }
}

static void _drain_ingress(void)
{
    /* Process every frame queued by dongle_wlan_core1_on_rx() since last poll. */
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

    /* Always unicast to the fixed gamepad address defined in dongle.h. */
    return gamepad_send(&pkt);
}

static bool send_status(void)
{
    dongle_pkt_s pkt;
    dongle_status_u stat;

    dongle_wlan_core0_status_snapshot_read(&stat);
    build_pkt(&pkt, DONGLE_PID_STATUS, 0, (const uint8_t *)&stat, sizeof(dongle_status_u));
    return gamepad_send(&pkt);
}

static bool send_reliable_core(const out_item_t *item, uint16_t ack)
{
    dongle_pkt_s pkt;
    build_pkt(&pkt, DONGLE_PID_CORE_RELIABLE, ack, item->data, item->len);
    return gamepad_send(&pkt);
}

static bool pump_reliable_tx(void)
{
    /* Lane idle — try to load the next host OUT payload from the outbox. */
    if (!reliable_lane_busy())
    {
        reliable_start_inflight();
    }

    /* Still idle after start attempt: outbox empty, nothing to send. */
    if (!reliable_lane_busy())
    {
        return false;
    }

    /* Send or resend _inflight with the same ack until gamepad confirms. */
    send_reliable_core(&_inflight, _outstanding_ack);
    return true;
}

static void pump_tx(uint64_t now_us)
{
    if (_link_up && _last_rx_us > 0 && (now_us - _last_rx_us) > DONGLE_WLAN_TIMEOUT_US)
    {
        reset_link();
        return;
    }

    if (!_link_up)
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

    if (!pump_reliable_tx())
    {
        send_status();
    }
}

void dongle_wlan_core1_init(struct udp_pcb *pcb)
{
    _pcb = pcb;
    init_gamepad_addr();
    _ingress_head = 0;
    _ingress_tail = 0;
    _ingress_count = 0;
    _link_up = false;
    _wlan_state = WLAN_STATE_AWAITING;
    _last_rx_us = 0;
}

void dongle_wlan_core1_on_rx(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)pcb;

    

    
}

void dongle_wlan_core1_poll(uint64_t now_us)
{
    /* Order matters: process inbound acks and gamepad data before outbound pump,
     * so a valid ack can start the next reliable send before we transmit. */
    _drain_ingress();
    pump_tx(now_us);
}

bool dongle_wlan_core1_queue_output(const uint8_t *data, uint16_t len)
{
    /* Called from core 0 (via dongle_wlan_queue_output) when a core tunnels
     * host OUT — e.g. SInput haptics or feature request. */
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

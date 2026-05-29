/*
 * hdongle.c — WLAN dongle protocol (Pico core 0 + core 1)
 *
 * Naming:
 *   _core0 / _core1  — which Pico CPU runs the function
 *   _wlan_sm_*       — WLAN protocol state machine (hdongle_wlan_sm_t); not a CPU index
 *   _crosscore       — safe to call from either core (resets shared FIFOs/snapshots)
 *
 * Responsibility split:
 *   Core 1 — WiFi AP, DHCP, UDP bind, RX callback, ingress, WAKE beacons.
 *            While link is up, STATUS / reliable OUT only when core0 schedules hdongle_link_pump(at_us).
 *   Core 0 — USB/console transport, core_init from WAKE, gamepad → host data paths.
 *
 * Wire model (dongle_pkt_s, see dongle.h):
 *   Every UDP payload is a fixed-size dongle_pkt_s. The gamepad echoes pkt->ack on its
 *   replies so the dongle can confirm a host→gamepad reliable OUT was received.
 *
 * Cross-core primitives (lock-free FIFOs / snapshots):
 *   fifo_udp_rx         ISR/callback push → core1 pop (_udp_rx_consume_core1)
 *   fifo_rx_reliable    core1 push (gamepad IN)  → core0 pop (USB cores)
 *   fifo_tx_reliable    core0 push (host OUT)   → core1 pop (reliable TX pump)
 *   snapshot rx_unreliable   latest CORE_UNRELIABLE input (overwrite)
 *   snapshot wake_mailbox    one pending WAKE for core0 to call core_init
 *   snapshot status          rumble/brakes/connection (core0 writes, STATUS TX reads)
 *   snapshot link_timeout    core1 sets flag → core0 restores boot core (e.g. N64)
 *
 * Typical bring-up sequence:
 *   1. Core1 LINK_DOWN: broadcast WAKE beacons to DONGLE_GAMEPAD_IP*.
 *   2. Gamepad sends WAKE with session + dongle_wake_s → core1 posts wake_mailbox.
 *   3. Core0 consumes mailbox → core_init(wake).
 *   4. Core1 LINK_UP: hdongle_link_pump_* from host poll timing (SOF / joybus) → STATUS or reliable OUT.
 *   5. On 5 s without RX: core1 resets link, signals core0 timeout snapshot.
 */

#include <stdio.h>
#include <string.h>
#include <stdatomic.h>

#include <dhcpserver.h>
#include <dongle.h>

#include "hdongle.h"
#include "cores/cores.h"
#include "hal/dongle_rgb.h"

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "hardware/watchdog.h"

#include "lwip/pbuf.h"
#include "lwip/err.h"
#include "lwip/ip4_addr.h"
#include "lwip/udp.h"

#include "utilities/crosscore_snapshot.h"
#include "utilities/crosscore_fifo.h"
#include "utilities/interval.h"

#define WIFI_SSID_BASE "HOJA_WLAN_1234"
#define WIFI_PASS "HOJA_1234"

/* ========================================================================== */
/* Shared status (core 0 owns fields; core 1 reads snapshot for STATUS TX)   */
/* ========================================================================== */

static dongle_status_u _status;
SNAPSHOT_TYPE(status, dongle_status_u);
static snapshot_status_t _ss_status;

dongle_status_u *hdongle_current_status(void)
{
    return &_status;
}

void hdongle_update_rumble(uint8_t rumble_left, uint8_t rumble_right, uint8_t brake_left, uint8_t brake_right)
{
    _status.rumble.left = rumble_left;
    _status.rumble.right = rumble_right;
    _status.brake.left = brake_left;
    _status.brake.right = brake_right;
    /* Publish so core1 STATUS packets see the latest rumble/brake without locking. */
    snapshot_status_write(&_ss_status, &_status);
}

void hdongle_update_connection_status(dongle_connection_t connection)
{
    _status.connection = (uint8_t)connection;
    snapshot_status_write(&_ss_status, &_status);
}

void hdongle_update_player_number(uint8_t player)
{
    _status.player_number = player;
    snapshot_status_write(&_ss_status, &_status);
}

/* ========================================================================== */
/* Cross-core FIFOs / snapshots                                              */
/* ========================================================================== */

/* Full RX metadata queued from UDP callback; core1 validates addr/port before use. */
typedef struct
{
    dongle_pkt_s pkt;
    ip_addr_t addr;
    u16_t port;
} udp_rx_frame_t;

CROSSCORE_FIFO_TYPE(udp_rx, udp_rx_frame_t, HDONGLE_QUEUE_LEN);
static fifo_udp_rx_t _cf_udp_rx;

CROSSCORE_FIFO_TYPE(rx_reliable, dongle_pkt_s, HDONGLE_QUEUE_LEN);
static fifo_rx_reliable_t _cf_rx_reliable;

CROSSCORE_FIFO_TYPE(tx_reliable, dongle_pkt_s, HDONGLE_QUEUE_LEN);
static fifo_tx_reliable_t _cf_tx_reliable;

SNAPSHOT_TYPE(rx_unreliable, dongle_pkt_s);
static snapshot_rx_unreliable_t _ss_rx_unreliable;

SNAPSHOT_TYPE(link_timeout, uint8_t);
static snapshot_link_timeout_t _ss_link_timeout;

/* Single-slot mailbox: core1 producer, core0 consumer (consume clears pending). */
typedef struct
{
    bool pending;
    dongle_session_s session;
    dongle_wake_s wake;
} wake_mailbox_t;

SNAPSHOT_TYPE(wake_mailbox, wake_mailbox_t);
static snapshot_wake_mailbox_t _ss_wake_mailbox;

/*
 * WAKE dedupe state (updated on core1, cleared on link reset from either core).
 * Suppresses posting identical session+wake back-to-back while link stays up;
 * always allow the first WAKE after LINK_DOWN→UP (reconnect / same session).
 */
static dongle_session_s _wake_last_posted_session;
static dongle_wake_s _wake_last_posted_body;
static bool _wake_last_posted_valid;

/** Drop all pending gamepad→host reliable reports (used on link reset). */
static void _drain_rx_reliable_core0(void)
{
    dongle_pkt_s pkt;
    while (fifo_rx_reliable_pop(&_cf_rx_reliable, &pkt))
    {
    }
}

/** Drop all pending host→gamepad reliable OUT (used on link reset). */
static void _drain_tx_reliable_core1(void)
{
    dongle_pkt_s pkt;
    while (fifo_tx_reliable_pop(&_cf_tx_reliable, &pkt))
    {
    }
}

/** Clear the WAKE mailbox without touching dedupe history. */
static void _wake_mailbox_reset_crosscore(void)
{
    wake_mailbox_t empty = {0};
    snapshot_wake_mailbox_write(&_ss_wake_mailbox, &empty);
}

/**
 * Full protocol reset of cross-core data paths after link loss.
 * Called from core0 (after timeout flag) and core1 (_wlan_sm_reset_link).
 */
static void _reset_crosscore_paths(void)
{
    dongle_pkt_s empty_pkt = {0};
    snapshot_rx_unreliable_write(&_ss_rx_unreliable, &empty_pkt);
    _wake_mailbox_reset_crosscore();
    _drain_rx_reliable_core0();
    _drain_tx_reliable_core1();
}

/* --- Public core0 RX/TX hooks (called from USB transport cores) --- */

bool hdongle_rx_unreliable_read_core0(dongle_pkt_s *pkt)
{
    if (!pkt)
    {
        return false;
    }

    snapshot_rx_unreliable_read(&_ss_rx_unreliable, pkt);
    /* len==0 means snapshot was cleared or never written. */
    return pkt->len > 0;
}

bool hdongle_core0_consume_reliable_inputreport(uint8_t *data, uint16_t *len)
{
    dongle_pkt_s pkt;

    if (!data || !len)
    {
        return false;
    }

    if (!fifo_rx_reliable_pop(&_cf_rx_reliable, &pkt))
    {
        return false;
    }

    memcpy(data, pkt.data, pkt.len);
    *len = pkt.len;
    return true;
}

void hdongle_core0_send_reliable_outputreport(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || len > 64)
    {
        return;
    }

    dongle_pkt_s pkt = {0};
    pkt.id = DONGLE_PID_CORE_RELIABLE;
    pkt.len = len;
    memcpy(pkt.data, data, len);
    /* Core1 pump will pop, assign ack, and resend until gamepad echoes ack. */
    fifo_tx_reliable_push(&_cf_tx_reliable, &pkt);
}

/* ========================================================================== */
/* Core 0 — session / boot / link timeout                                    */
/* ========================================================================== */

#define HDONGLE_MODE_NONE 0xFFu /* no core selected yet (before first core_init) */

static uint8_t _boot_mode = DONGLE_MODE_N64; /* Restored on WLAN link timeout */
static uint8_t _active_mode = HDONGLE_MODE_NONE;
static bool _wlan_owned_core; /* true if WLAN WAKE switched us away from boot core */
static dongle_session_s _active_session;

uint8_t hdongle_active_mode(void)
{
    return _active_mode;
}

/**
 * Take one pending WAKE from the mailbox (consume-once).
 * Returns false if core1 has not posted a new WAKE since last consume.
 */
static bool _wake_mailbox_consume_core0(dongle_wake_s *wake, dongle_session_s *session)
{
    wake_mailbox_t box = {0};
    snapshot_wake_mailbox_read(&_ss_wake_mailbox, &box);
    if (!box.pending)
    {
        return false;
    }

    *wake = box.wake;
    *session = box.session;

    /* Acknowledge so we do not re-init the same WAKE on every core0 loop. */
    box.pending = false;
    snapshot_wake_mailbox_write(&_ss_wake_mailbox, &box);
    return true;
}

/**
 * Apply a consumed WAKE: deinit/reinit the USB core to match gamepad mode.
 * Reboots via watchdog if switching between two non-N64 formats (hardware limit).
 */
static void _init_core_from_wake_core0(const dongle_wake_s *wake, const dongle_session_s *session)
{
    dongle_mode_t mode = (dongle_mode_t)wake->mode;
    bool mode_changed = _active_mode != HDONGLE_MODE_NONE && _active_mode != (uint8_t)mode;

    if (mode_changed && _active_mode != DONGLE_MODE_N64)
    {
        /* Cannot hot-swap e.g. XInput → GC without a clean reboot. */
        watchdog_reboot(0, 0, 0);
        return;
    }

    core_deinit();
    if (!core_init(wake))
    {
        return;
    }

    _active_session = *session;
    _active_mode = (uint8_t)mode;
    dongle_rgb_set_mode(_active_mode);
    _wlan_owned_core = (_active_mode != _boot_mode);
    /* Boot N64 stays resident in RAM even when gamepad also wants N64. */
    if (mode == DONGLE_MODE_N64 && _boot_mode == DONGLE_MODE_N64)
    {
        _wlan_owned_core = false;
    }
}

/** If mailbox has a WAKE, run core_init path once. */
static void _try_consume_wake_core0(void)
{
    dongle_wake_s wake;
    dongle_session_s session;

    if (!_wake_mailbox_consume_core0(&wake, &session))
    {
        return;
    }

    _init_core_from_wake_core0(&wake, &session);
}

/**
 * Core0 reaction to WLAN link timeout (flag set by core1).
 * Restores boot core when WLAN had switched the active format.
 */
static void _handle_link_timeout_core0(void)
{
    if (_wlan_owned_core)
    {
        core_deinit();
        sleep_ms(100);
        core_init(core_boot_wake());
        _active_mode = _boot_mode;
        dongle_rgb_set_mode(_active_mode);
        _wlan_owned_core = false;
    }

    memset(&_active_session, 0, sizeof(_active_session));
    _wake_last_posted_valid = false;
    hdongle_update_connection_status(DONGLE_CONN_IDLE);
}

void hdongle_core0(uint64_t time_us)
{
    (void)time_us;

    /* Core1 sets this snapshot when gamepad goes silent for HDONGLE_TIMEOUT_US. */
    uint8_t timeout = 0;
    snapshot_link_timeout_read(&_ss_link_timeout, &timeout);
    if (timeout)
    {
        timeout = 0;
        snapshot_link_timeout_write(&_ss_link_timeout, &timeout);
        _handle_link_timeout_core0();
        _reset_crosscore_paths();
        return;
    }

    _try_consume_wake_core0();
    core_task(time_us);
    dongle_rgb_task(time_us);
}

/* ========================================================================== */
/* WLAN state machine (runs on core 1; names describe protocol, not CPU)     */
/* ========================================================================== */

typedef enum
{
    HDONGLE_WLAN_LINK_DOWN, /* No recent gamepad RX — send WAKE beacons only */
    HDONGLE_WLAN_LINK_UP,   /* Gamepad heard — pump via hdongle_link_pump() only */
} hdongle_wlan_link_t;

typedef enum
{
    HDONGLE_WLAN_RELIABLE_IDLE,      /* No host OUT waiting for gamepad ack */
    HDONGLE_WLAN_RELIABLE_AWAIT_ACK, /* CORE_RELIABLE on wire; pkt.ack must match expected_ack */
} hdongle_wlan_reliable_t;

typedef struct
{
    hdongle_wlan_link_t link;
    uint64_t last_rx_us;       /* Last valid UDP RX from gamepad (for 5 s timeout) */
    dongle_session_s session;  /* Packed into outbound dongle_pkt_s.session */

    hdongle_wlan_reliable_t reliable;
    uint16_t prev_ack;     /* Last ack token we sent (never issue the same twice in a row) */
    uint16_t expected_ack; /* While AWAIT_ACK: gamepad must echo this in pkt->ack */
    dongle_pkt_s inflight; /* Host OUT payload being acknowledged */

    interval_s wake_iv; /* 100 ms WAKE beacon timer while LINK_DOWN */
} hdongle_wlan_sm_t;

static struct udp_pcb *_pcb;
static ip_addr_t _gamepad_addr; /* Fixed DONGLE_GAMEPAD_IP* from dongle.h */
static hdongle_wlan_sm_t _wlan_sm;

/** Next WLAN pump deadline from core0 (atomic; core1 executes when time_us >= pump_at_us). */
static atomic_ullong _link_pump_at_us;
/** Last completed WLAN pump on core1 (rate limit). */
static atomic_ullong _link_pump_last_done_us;
/** Last host poll response time on core0 (half-period scheduling). */
static atomic_ullong _link_poll_last_sent_us;

static void _signal_link_timeout_core1(void);

/** Mirror wlan_sm link phase into dongle_status_u.connection for STATUS packets. */
static void _wlan_sm_publish_connection(const hdongle_wlan_sm_t *sm)
{
    dongle_connection_t conn =
        (sm->link == HDONGLE_WLAN_LINK_UP) ? DONGLE_CONN_CONNECTED : DONGLE_CONN_IDLE;
    hdongle_update_connection_status(conn);
}

static void _wlan_sm_init(hdongle_wlan_sm_t *sm)
{
    memset(sm, 0, sizeof(*sm));
    sm->link = HDONGLE_WLAN_LINK_DOWN;
    sm->reliable = HDONGLE_WLAN_RELIABLE_IDLE;
    _wlan_sm_publish_connection(sm);
}

/** Call on every accepted gamepad RX; promotes LINK_DOWN → LINK_UP once. */
static void _wlan_sm_link_on_rx(hdongle_wlan_sm_t *sm, uint64_t now_us)
{
    sm->last_rx_us = now_us;
    if (sm->link == HDONGLE_WLAN_LINK_DOWN)
    {
        sm->link = HDONGLE_WLAN_LINK_UP;
        _wlan_sm_publish_connection(sm);
    }
}

static bool _wlan_sm_link_timed_out(const hdongle_wlan_sm_t *sm, uint64_t now_us)
{
    return sm->link == HDONGLE_WLAN_LINK_UP && sm->last_rx_us > 0 &&
           (now_us - sm->last_rx_us) > HDONGLE_TIMEOUT_US;
}

static void _wlan_sm_link_disconnect(hdongle_wlan_sm_t *sm)
{
    sm->link = HDONGLE_WLAN_LINK_DOWN;
    sm->last_rx_us = 0;
    memset(&sm->session, 0, sizeof(sm->session));
    _wlan_sm_publish_connection(sm);
}

static bool _wlan_sm_reliable_awaiting_ack(const hdongle_wlan_sm_t *sm)
{
    return sm->reliable == HDONGLE_WLAN_RELIABLE_AWAIT_ACK;
}

static void _wlan_sm_reliable_clear_inflight(hdongle_wlan_sm_t *sm)
{
    sm->reliable = HDONGLE_WLAN_RELIABLE_IDLE;
    sm->expected_ack = 0;
    memset(&sm->inflight, 0, sizeof(sm->inflight));
}

/** Pick a random 16-bit ack token distinct from prev_ack; store as expected_ack. */
static uint16_t _wlan_sm_reliable_assign_ack(hdongle_wlan_sm_t *sm)
{
    uint16_t ack;
    do
    {
        ack = (uint16_t)(get_rand_32() & 0xFFFFu);
    } while (ack == sm->prev_ack);
    sm->prev_ack = ack;
    sm->expected_ack = ack;
    return ack;
}

/**
 * If reliable lane is free, pop next host OUT from fifo_tx_reliable and go AWAIT_ACK.
 * The pump will transmit inflight with pkt->ack == expected_ack until gamepad echoes it.
 */
static bool _wlan_sm_reliable_try_dequeue(hdongle_wlan_sm_t *sm)
{
    dongle_pkt_s pkt;

    if (_wlan_sm_reliable_awaiting_ack(sm))
    {
        return false;
    }

    if (!fifo_tx_reliable_pop(&_cf_tx_reliable, &pkt))
    {
        return false;
    }

    sm->inflight = pkt;
    _wlan_sm_reliable_assign_ack(sm);
    sm->reliable = HDONGLE_WLAN_RELIABLE_AWAIT_ACK;
    return true;
}

/**
 * Gamepad echoes our ack in every packet while session is active.
 * Match → retire inflight and optionally start next queued OUT.
 */
static void _wlan_sm_reliable_on_ack(hdongle_wlan_sm_t *sm, uint16_t echoed_ack)
{
    if (!_wlan_sm_reliable_awaiting_ack(sm) || echoed_ack != sm->expected_ack)
    {
        return;
    }

    _wlan_sm_reliable_clear_inflight(sm);
    _wlan_sm_reliable_try_dequeue(sm);
}

static void _wlan_sm_reliable_reset(hdongle_wlan_sm_t *sm)
{
    sm->prev_ack = 0;
    _wlan_sm_reliable_clear_inflight(sm);
    _drain_tx_reliable_core1();
}

/**
 * Full link teardown: reliable lane, session, cross-core FIFOs, notify core0.
 */
static void _wlan_sm_reset_link(hdongle_wlan_sm_t *sm)
{
    _wlan_sm_reliable_reset(sm);
    _wlan_sm_link_disconnect(sm);
    _reset_crosscore_paths();
    _wake_last_posted_valid = false;
    hdongle_link_pump_reset_timing();
    atomic_store_explicit(&_link_pump_at_us, 0, memory_order_relaxed);
    _signal_link_timeout_core1();
}

/* ========================================================================== */
/* Core 1 — UDP, ingress, TX pump                                              */
/* ========================================================================== */

/** session field on wire is a packed dongle_session_s (16 bits). */
static void _session_unpack_core1(uint16_t packed, dongle_session_s *out)
{
    memcpy(out, &packed, sizeof(uint16_t));
}

static bool _session_equal_core1(const dongle_session_s *a, const dongle_session_s *b)
{
    return a->mode == b->mode && a->id == b->id;
}

static bool _wake_body_equal_core1(const dongle_wake_s *a, const dongle_wake_s *b)
{
    return memcmp(a, b, sizeof(dongle_wake_s)) == 0;
}

/**
 * Ignore repeated WAKE with identical session+body while link remains up.
 * link_just_established bypasses dedupe so reconnect always reaches core0.
 */
static bool _wake_is_duplicate_core1(const dongle_session_s *session, const dongle_wake_s *wake)
{
    if (!_wake_last_posted_valid)
    {
        return false;
    }
    return _session_equal_core1(session, &_wake_last_posted_session) &&
           _wake_body_equal_core1(wake, &_wake_last_posted_body);
}

/** Queue WAKE for core0 and remember what we posted for dedupe. */
static void _wake_mailbox_post_core1(const dongle_session_s *session, const dongle_wake_s *wake)
{
    wake_mailbox_t box = {0};
    box.pending = true;
    box.session = *session;
    box.wake = *wake;
    snapshot_wake_mailbox_write(&_ss_wake_mailbox, &box);

    _wake_last_posted_session = *session;
    _wake_last_posted_body = *wake;
    _wake_last_posted_valid = true;
}

/** Set link_timeout snapshot; core0 clears it after handling. */
static void _signal_link_timeout_core1(void)
{
    uint8_t flag = 1;
    snapshot_link_timeout_write(&_ss_link_timeout, &flag);
}

static uint16_t _session_pack_core1(const dongle_session_s *s)
{
    uint16_t v = 0;
    memcpy(&v, s, sizeof(uint16_t));
    return v;
}

/** Only accept packets from configured gamepad IP/port (see dongle.h). */
static bool _gamepad_addr_matches_core1(const ip_addr_t *addr, u16_t port)
{
    return port == DONGLE_GAMEPAD_PORT && ip_addr_cmp(addr, &_gamepad_addr);
}

static bool _udp_send_to_core1(const dongle_pkt_s *pkt, const ip_addr_t *addr, u16_t port)
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

static bool _gamepad_send_core1(const dongle_pkt_s *pkt)
{
    return _udp_send_to_core1(pkt, &_gamepad_addr, DONGLE_GAMEPAD_PORT);
}

/** Build outbound dongle_pkt_s using current WLAN SM session and supplied ack/payload. */
static void _build_pkt_core1(const hdongle_wlan_sm_t *sm, dongle_pkt_s *pkt, dongle_pid_t pid, uint16_t ack,
                             const uint8_t *data, uint16_t len)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->session = _session_pack_core1(&sm->session);
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

/**
 * Handle inbound WAKE: update TX session, post mailbox for core0.
 * link_just_established is true on the first RX after LINK_DOWN (always accept WAKE).
 */
static void _rx_wake_handle_core1(hdongle_wlan_sm_t *sm, const dongle_pkt_s *pkt, bool link_just_established)
{
    dongle_session_s session;
    const dongle_wake_s *wake;

    if (pkt->len < sizeof(dongle_wake_s))
    {
        return;
    }

    _session_unpack_core1(pkt->session, &session);
    wake = (const dongle_wake_s *)pkt->data;

    if (!link_just_established && _wake_is_duplicate_core1(&session, wake))
    {
        return;
    }

    memcpy(&sm->session, &session, sizeof(uint16_t));
    _wake_mailbox_post_core1(&session, wake);
}

/** Latest-only input path: cores on core0 read via hdongle_rx_unreliable_read_core0. */
static void _rx_unreliable_write_core1(const dongle_pkt_s *pkt)
{
    snapshot_rx_unreliable_write(&_ss_rx_unreliable, pkt);
}

/**
 * Drain UDP RX FIFO (filled in _udp_rx_cb_core1), validate source, dispatch by PID.
 * Runs in core1 main loop (not ISR).
 */
static void _udp_rx_consume_core1(hdongle_wlan_sm_t *sm)
{
    udp_rx_frame_t frame;

    while (fifo_udp_rx_pop(&_cf_udp_rx, &frame))
    {
        if (!_gamepad_addr_matches_core1(&frame.addr, frame.port))
        {
            continue;
        }

        uint64_t now_us = time_us_64();
        bool link_just_established = (sm->link == HDONGLE_WLAN_LINK_DOWN);
        _wlan_sm_link_on_rx(sm, now_us);

        const dongle_pkt_s *pkt = &frame.pkt;

        /* Every gamepad packet can acknowledge our outstanding reliable OUT. */
        _wlan_sm_reliable_on_ack(sm, pkt->ack);

        /* Empty payload except WAKE is ignored (keepalive / ack-only frames). */
        if (pkt->len == 0 && pkt->id != DONGLE_PID_WAKE)
        {
            continue;
        }

        switch ((dongle_pid_t)pkt->id)
        {
        case DONGLE_PID_WAKE:
            _rx_wake_handle_core1(sm, pkt, link_just_established);
            break;

        case DONGLE_PID_CORE_RELIABLE:
            fifo_rx_reliable_push(&_cf_rx_reliable, pkt);
            break;

        case DONGLE_PID_CORE_UNRELIABLE:
            _rx_unreliable_write_core1(pkt);
            break;

        default:
            break;
        }
    }
}

/** Empty WAKE to fixed gamepad IP — invites gamepad to associate and send session WAKE. */
static bool _send_wake_beacon_core1(const hdongle_wlan_sm_t *sm)
{
    dongle_pkt_s pkt;
    _build_pkt_core1(sm, &pkt, DONGLE_PID_WAKE, 0, NULL, 0);
    return _gamepad_send_core1(&pkt);
}

/** Rumble/connection/player snapshot to gamepad when reliable lane is idle. */
static bool _send_status_core1(const hdongle_wlan_sm_t *sm)
{
    dongle_pkt_s pkt;
    dongle_status_u stat;

    snapshot_status_read(&_ss_status, &stat);
    _build_pkt_core1(sm, &pkt, DONGLE_PID_STATUS, 0, (const uint8_t *)&stat, sizeof(dongle_status_u));
    return _gamepad_send_core1(&pkt);
}

/** Resend current inflight CORE_RELIABLE with expected_ack until gamepad echoes it. */
static bool _send_inflight_reliable_core1(const hdongle_wlan_sm_t *sm)
{
    dongle_pkt_s pkt;

    if (!_wlan_sm_reliable_awaiting_ack(sm))
    {
        return false;
    }

    _build_pkt_core1(sm, &pkt, DONGLE_PID_CORE_RELIABLE, sm->expected_ack, sm->inflight.data, sm->inflight.len);
    return _gamepad_send_core1(&pkt);
}

/**
 * One WLAN pump tick for reliable OUT.
 * Returns true if a reliable packet was sent (caller skips STATUS this tick).
 */
static bool _pump_reliable_tx_core1(hdongle_wlan_sm_t *sm)
{
    if (!_wlan_sm_reliable_awaiting_ack(sm))
    {
        _wlan_sm_reliable_try_dequeue(sm);
    }

    if (!_wlan_sm_reliable_awaiting_ack(sm))
    {
        return false;
    }

    return _send_inflight_reliable_core1(sm);
}

/**
 * One WLAN link pump: send reliable resend if inflight, else STATUS.
 * No-op while LINK_DOWN.
 */
static void _wlan_pump_link_core1(hdongle_wlan_sm_t *sm)
{
    if (sm->link != HDONGLE_WLAN_LINK_UP)
    {
        return;
    }

    if (!_pump_reliable_tx_core1(sm))
    {
        _send_status_core1(sm);
    }
}

void hdongle_link_pump(uint64_t pump_at_us)
{
    uint64_t now_us = time_us_64();
    uint64_t last_us = atomic_load_explicit(&_link_pump_last_done_us, memory_order_relaxed);

    if (last_us > 0 && (now_us - last_us) < HDONGLE_LINK_PUMP_MIN_INTERVAL_US)
    {
        return;
    }

    if (pump_at_us < now_us)
    {
        pump_at_us = now_us;
    }

    atomic_store_explicit(&_link_pump_at_us, pump_at_us, memory_order_release);
}

void hdongle_link_pump_reset_timing(void)
{
    atomic_store_explicit(&_link_poll_last_sent_us, 0, memory_order_relaxed);
}

void hdongle_link_pump_schedule_from_poll(uint64_t now_us)
{
    uint64_t last_sent_us = atomic_load_explicit(&_link_poll_last_sent_us, memory_order_relaxed);

    if (last_sent_us > 0)
    {
        hdongle_link_pump(now_us + ((now_us - last_sent_us) >> 1));
    }
}

void hdongle_link_pump_mark_sent(uint64_t now_us)
{
    atomic_store_explicit(&_link_poll_last_sent_us, now_us, memory_order_relaxed);
}

static void _try_link_pump_core1(hdongle_wlan_sm_t *sm, uint64_t now_us)
{
    uint64_t at_us = atomic_load_explicit(&_link_pump_at_us, memory_order_acquire);
    if (at_us == 0 || now_us < at_us)
    {
        return;
    }

    uint64_t last_us = atomic_load_explicit(&_link_pump_last_done_us, memory_order_relaxed);
    if (last_us > 0 && (now_us - last_us) < HDONGLE_LINK_PUMP_MIN_INTERVAL_US)
    {
        return;
    }

    atomic_store_explicit(&_link_pump_at_us, 0, memory_order_release);
    atomic_store_explicit(&_link_pump_last_done_us, now_us, memory_order_relaxed);
    _wlan_pump_link_core1(sm);
}

/**
 * Core1 periodic TX scheduler:
 *   - Check RX timeout → full reset + notify core0
 *   - LINK_DOWN → 100 ms WAKE beacons
 *   (LINK_UP TX is driven only by hdongle_link_pump on core0)
 */
static void _pump_tx_core1(hdongle_wlan_sm_t *sm, uint64_t now_us)
{
    if (_wlan_sm_link_timed_out(sm, now_us))
    {
        _wlan_sm_reset_link(sm);
        return;
    }

    if (sm->link == HDONGLE_WLAN_LINK_DOWN)
    {
        if (interval_run(now_us, HDONGLE_WAKE_INTERVAL_US, &sm->wake_iv))
        {
            _send_wake_beacon_core1(sm);
        }
    }
}

/**
 * lwIP UDP recv callback (may run in TCP/IP context).
 * Copy frame into fifo_udp_rx; core1 loop does parsing and state updates.
 */
static void _udp_rx_cb_core1(void *arg, struct udp_pcb *udp, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)udp;

    if (!p || p->tot_len != sizeof(dongle_pkt_s) || !addr)
    {
        if (p)
        {
            pbuf_free(p);
        }
        return;
    }

    udp_rx_frame_t frame;
    pbuf_copy_partial(p, &frame.pkt, sizeof(dongle_pkt_s), 0);
    ip_addr_copy(frame.addr, *addr);
    frame.port = port;
    fifo_udp_rx_push(&_cf_udp_rx, &frame);
    pbuf_free(p);
}

void hdongle_core1(uint64_t time_us)
{
    _udp_rx_consume_core1(&_wlan_sm);
    _try_link_pump_core1(&_wlan_sm, time_us);
    _pump_tx_core1(&_wlan_sm, time_us);
}

/* ========================================================================== */
/* Entry points                                                                */
/* ========================================================================== */

/** Core1: WiFi AP + DHCP + UDP server; never returns. */
static void main_core1(void)
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
    ip_addr_t ap_ip, ap_netmask;
    IP4_ADDR(&ap_ip, 192, 168, 4, 1);
    IP4_ADDR(&ap_netmask, 255, 255, 255, 0);
    dhcp_server_init(&dhcp_server, &ap_ip, &ap_netmask);

    struct udp_pcb *udp = udp_new();
    udp_bind(udp, IP_ANY_TYPE, HDONGLE_WLAN_UDP_PORT);
    udp_recv(udp, _udp_rx_cb_core1, NULL);

    _pcb = udp;
    IP4_ADDR(&_gamepad_addr, DONGLE_GAMEPAD_IP0, DONGLE_GAMEPAD_IP1, DONGLE_GAMEPAD_IP2, DONGLE_GAMEPAD_IP3);
    _wlan_sm_init(&_wlan_sm);
    _wake_last_posted_valid = false;

    _status.connection = DONGLE_CONN_IDLE;
    snapshot_status_write(&_ss_status, &_status);

    for (;;)
    {
        hdongle_core1(time_us_64());
    }
}

/** Core0: boot N64, launch WLAN core, run transport + WAKE consumer loop. */
int main(void)
{
    dongle_rgb_enter_bootloader_if_buttons_held();

    stdio_init_all();

    _boot_mode = core_boot_wake()->mode;
    _active_mode = _boot_mode;

    core_init(core_boot_wake());

    multicore_launch_core1(main_core1);

    /* CYW43 may reconfigure GPIOs at init; reclaim button pins on core0. */
    dongle_rgb_gpio_init();

    dongle_rgb_init();
    dongle_rgb_set_mode(_active_mode);

    for (;;)
    {
        hdongle_core0(time_us_64());
    }
}

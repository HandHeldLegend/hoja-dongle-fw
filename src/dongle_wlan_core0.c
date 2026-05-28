#include "dongle_wlan_core0.h"

#include <string.h>

#include "dongle_wlan.h"
#include "cores/cores.h"
#include "utilities/crosscore_snapshot.h"

#include "pico/bootrom.h"
#include "hardware/watchdog.h"

/* --- Inbox: packets forwarded from core 1 --- */

static struct
{
    volatile uint16_t head;
    volatile uint16_t tail;
    dongle_pkt_s slots[DONGLE_WLAN_QUEUE_LEN];
} _inbox;

/* --- Inbox: reliable gamepad → host payloads --- */

typedef struct
{
    uint8_t data[64];
    uint16_t len;
} core0_payload_t;

static core0_payload_t _rel_in[DONGLE_WLAN_QUEUE_LEN];
static uint8_t _rel_head;
static uint8_t _rel_count;
static bool _block_unreliable;

/* --- Session / boot state --- */

static dongle_status_u _status;
static uint8_t _boot_format = 0xFF;
static uint8_t _active_format = 0xFF;
static bool _wlan_owned_core;
static dongle_session_s _active_session;

SNAPSHOT_TYPE(wlan_status, dongle_status_u);
static snapshot_wlan_status_t _snap_status;

SNAPSHOT_TYPE(wlan_link_timeout, uint8_t);
static snapshot_wlan_link_timeout_t _snap_link_timeout;

static void status_refresh(void)
{
    snapshot_wlan_status_write(&_snap_status, &_status);
}

static bool inbox_pop(dongle_pkt_s *pkt)
{
    if (_inbox.head == _inbox.tail)
    {
        return false;
    }
    *pkt = _inbox.slots[_inbox.head];
    __dmb();
    _inbox.head = (uint16_t)((_inbox.head + 1) % DONGLE_WLAN_QUEUE_LEN);
    return true;
}

static bool rel_enqueue(const uint8_t *data, uint16_t len)
{
    if (_rel_count >= DONGLE_WLAN_QUEUE_LEN || !data || len == 0 || len > 64)
    {
        return false;
    }
    core0_payload_t *slot = &_rel_in[(_rel_head + _rel_count) % DONGLE_WLAN_QUEUE_LEN];
    memcpy(slot->data, data, len);
    slot->len = len;
    _rel_count++;
    _block_unreliable = true;
    return true;
}

static void rel_dequeue(void)
{
    if (_rel_count == 0)
    {
        return;
    }
    _rel_head = (uint8_t)((_rel_head + 1) % DONGLE_WLAN_QUEUE_LEN);
    _rel_count--;
    if (_rel_count == 0)
    {
        _block_unreliable = false;
    }
}

static void session_unpack(uint16_t packed, dongle_session_s *s)
{
    memcpy(s, &packed, sizeof(uint16_t));
}

static bool sessions_equal(const dongle_session_s *a, const dongle_session_s *b)
{
    return a->mode == b->mode && a->id == b->id;
}

static core_reportformat_t mode_to_format(dongle_mode_t mode)
{
    switch (mode)
    {
    case DONGLE_MODE_SINPUT:
        return CORE_REPORTFORMAT_SINPUT;
    case DONGLE_MODE_XINPUT:
        return CORE_REPORTFORMAT_XINPUT;
    case DONGLE_MODE_SLIPPI:
        return CORE_REPORTFORMAT_SLIPPI;
    case DONGLE_MODE_SNES:
        return CORE_REPORTFORMAT_SNES;
    case DONGLE_MODE_N64:
        return CORE_REPORTFORMAT_N64;
    case DONGLE_MODE_GAMECUBE:
        return CORE_REPORTFORMAT_GAMECUBE;
    case DONGLE_MODE_SWITCH:
        return CORE_REPORTFORMAT_SWPRO;
    default:
        return CORE_REPORTFORMAT_UNDEFINED;
    }
}

static void apply_session(const dongle_wake_s *wake, const dongle_session_s *sess)
{
    core_reportformat_t fmt = mode_to_format((dongle_mode_t)wake->mode);
    if (fmt == CORE_REPORTFORMAT_UNDEFINED)
    {
        return;
    }

    static dongle_session_s last_applied;
    static bool last_valid;
    bool session_changed = !last_valid || !sessions_equal(sess, &last_applied);
    bool format_changed = _active_format != (uint8_t)fmt;

    if (!session_changed && !format_changed)
    {
        return;
    }

    if (format_changed && _active_format != (uint8_t)CORE_REPORTFORMAT_UNDEFINED &&
        _active_format != (uint8_t)CORE_REPORTFORMAT_N64)
    {
        watchdog_reboot(0, 0, 0);
        return;
    }

    core_deinit();
    if (!core_init(fmt, wake))
    {
        return;
    }

    last_applied = *sess;
    last_valid = true;
    _active_session = *sess;
    _active_format = (uint8_t)fmt;
    _wlan_owned_core = (fmt != (core_reportformat_t)_boot_format);
    if (fmt == CORE_REPORTFORMAT_N64 && fmt == (core_reportformat_t)_boot_format)
    {
        _wlan_owned_core = false;
    }
}

static void handle_core_payload(const dongle_pkt_s *pkt)
{
    if (pkt->len == 0 || pkt->len > 64)
    {
        return;
    }

    switch ((dongle_pid_t)pkt->id)
    {
    case DONGLE_PID_CORE_UNRELIABLE:
        if (!_block_unreliable && _rel_count == 0)
        {
            core_input_report_tunnel(pkt->data, pkt->len);
        }
        break;

    case DONGLE_PID_CORE_RELIABLE:
        rel_enqueue(pkt->data, pkt->len);
        break;

    default:
        break;
    }
}

static void handle_packet(const dongle_pkt_s *pkt)
{
    if (pkt->id == DONGLE_PID_WAKE && pkt->len >= sizeof(dongle_wake_s))
    {
        const dongle_wake_s *wake = (const dongle_wake_s *)pkt->data;
        dongle_session_s sess;
        session_unpack(pkt->session, &sess);
        apply_session(wake, &sess);
        return;
    }

    handle_core_payload(pkt);
}

void dongle_wlan_core0_init(void)
{
    memset(&_status, 0, sizeof(_status));
    _status.connection = DONGLE_CONN_IDLE;
    status_refresh();
    dongle_wlan_core0_reset();
}

void dongle_wlan_core0_reset(void)
{
    _inbox.head = 0;
    _inbox.tail = 0;
    _rel_head = 0;
    _rel_count = 0;
    _block_unreliable = false;
    memset(&_active_session, 0, sizeof(_active_session));
}

void dongle_wlan_core0_set_boot_format(uint8_t format)
{
    _boot_format = format;
    _active_format = format;
}

bool dongle_wlan_core0_inbox_push(const dongle_pkt_s *pkt)
{
    if (!pkt)
    {
        return false;
    }
    uint16_t next = (uint16_t)((_inbox.tail + 1) % DONGLE_WLAN_QUEUE_LEN);
    if (next == _inbox.head)
    {
        return false;
    }
    _inbox.slots[_inbox.tail] = *pkt;
    __dmb();
    _inbox.tail = next;
    return true;
}

void dongle_wlan_core0_signal_link_timeout(void)
{
    uint8_t flag = 1;
    snapshot_wlan_link_timeout_write(&_snap_link_timeout, &flag);
}

void dongle_wlan_core0_status_snapshot_read(dongle_status_u *out)
{
    if (out)
    {
        snapshot_wlan_status_read(&_snap_status, out);
    }
}

bool dongle_wlan_core0_peek_reliable(uint8_t *data, uint16_t *len)
{
    if (_rel_count == 0 || !data || !len)
    {
        return false;
    }
    const core0_payload_t *head = &_rel_in[_rel_head];
    memcpy(data, head->data, head->len);
    *len = head->len;
    return true;
}

void dongle_wlan_core0_consume_reliable(void)
{
    rel_dequeue();
}

void dongle_wlan_core0_poll(uint64_t now_us)
{
    (void)now_us;

    uint8_t timeout = 0;
    snapshot_wlan_link_timeout_read(&_snap_link_timeout, &timeout);
    if (timeout)
    {
        timeout = 0;
        snapshot_wlan_link_timeout_write(&_snap_link_timeout, &timeout);

        if (_wlan_owned_core)
        {
            core_deinit();
            core_init((core_reportformat_t)_boot_format, NULL);
            _active_format = _boot_format;
            _wlan_owned_core = false;
        }
        dongle_wlan_core0_reset();
        dongle_update_connection_status(DONGLE_CONN_IDLE);
        return;
    }

    dongle_pkt_s pkt;
    while (inbox_pop(&pkt))
    {
        handle_packet(&pkt);
    }
}

dongle_status_u *dongle_current_status(void)
{
    return &_status;
}

void dongle_update_rumble(uint8_t rumble_left, uint8_t rumble_right, uint8_t brake_left, uint8_t brake_right)
{
    _status.rumble.left = rumble_left;
    _status.rumble.right = rumble_right;
    _status.brake.left = brake_left;
    _status.brake.right = brake_right;
    status_refresh();
}

void dongle_update_connection_status(dongle_connection_t connection)
{
    _status.connection = (uint8_t)connection;
    status_refresh();
}

void dongle_update_player_number(uint8_t player)
{
    _status.player_number = player;
    status_refresh();
}

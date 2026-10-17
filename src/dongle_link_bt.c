/*
 * Bluetooth platform adapter: the dongle's radio side on FLOSS (core 1).
 *
 * Copyright (c) 2026 Hand Held Legend, LLC
 * Author: Mitchell Cairns
 *
 * SPDX-License-Identifier: MIT-0
 */

/**
 * @file dongle_link_bt.c
 * @brief The radio core of a Bluetooth dongle: FLOSS link profile + the dongle host link API.
 *
 * The dongle waits to be found (the gamepad connects and runs the link) and stores
 * nothing: every connection pairs again, and any free gamepad may connect. The traffic
 * on top of the link is defined in dongle_link.h:
 *
 *   - link INPUT (live reports, the app's live report) -> dongle_api_host_link_rx_*
 *   - a reliable L2CAP channel (DONGLE_LINK_PSM_RELIABLE), opened by the gamepad:
 *     host info out on connect / change, console output reports and config traffic
 *     out, the gamepad's identity / reliable reports / config replies in
 *   - link OUTPUT: the latest status (rumble, player), sent on change
 *
 * Everything here runs on core 1, the only core that touches FLOSS or the radio (FLOSS's
 * bare CYW43 glue: no async context, no interrupts; the driver runs inside floss_poll()).
 */

#include <stdint.h>
#include <string.h>

#include <dongle.h>
#include <dongle_host.h>
#include <dongle_link.h>

#include "dongle_radio.h"
#include "utilities/dongle_info.h"

#include "pico/stdlib.h"
#include "pico/rand.h"
#include "hardware/sync.h"

#include "floss/floss.h"
#include "floss_cyw43.h"

/* -------------------------------------------------------------------------- */
/* FLOSS platform (HAL)                                                       */
/* -------------------------------------------------------------------------- */

uint32_t floss_hal_time_us(void)
{
    return time_us_32();
}

/* Only core 1 uses FLOSS; interrupts off is enough */
uint32_t floss_hal_critical_enter(void)
{
    return save_and_disable_interrupts();
}

void floss_hal_critical_exit(uint32_t state)
{
    restore_interrupts(state);
}

void floss_hal_random(uint8_t *buf, size_t len)
{
    while (len)
    {
        uint32_t r = get_rand_32();
        size_t n = len < 4 ? len : 4;
        memcpy(buf, &r, n);
        buf += n;
        len -= n;
    }
}

void floss_hal_log_write(const char *text, size_t len)
{
    (void)text;
    (void)len;
}

void floss_hal_debug_pin(uint8_t pin, bool level)
{
    (void)pin;
    (void)level;
}

/* The dongle remembers no pairings: a store in RAM, empty at every boot */
#define DONGLE_STORE_ENTRIES 8u
#define DONGLE_STORE_VALUE_MAX 64u

typedef struct
{
    uint16_t key;
    uint8_t used;
    uint8_t len;
    uint8_t value[DONGLE_STORE_VALUE_MAX];
} dongle_store_entry_s;

static dongle_store_entry_s _store[DONGLE_STORE_ENTRIES];

static dongle_store_entry_s *_store_find(uint16_t key)
{
    for (unsigned i = 0; i < DONGLE_STORE_ENTRIES; i++)
    {
        if (_store[i].used && _store[i].key == key)
        {
            return &_store[i];
        }
    }
    return NULL;
}

int floss_hal_store_read(uint16_t key, void *buf, uint16_t len)
{
    const dongle_store_entry_s *e = _store_find(key);
    if (!e)
    {
        return FLOSS_ERR_NOT_FOUND;
    }
    uint16_t n = len < e->len ? len : e->len;
    memcpy(buf, e->value, n);
    return n;
}

floss_err_t floss_hal_store_write(uint16_t key, const void *buf, uint16_t len)
{
    if (len > DONGLE_STORE_VALUE_MAX || (len && !buf))
    {
        return FLOSS_ERR_INVALID;
    }
    dongle_store_entry_s *e = _store_find(key);
    for (unsigned i = 0; !e && i < DONGLE_STORE_ENTRIES; i++)
    {
        if (!_store[i].used)
        {
            e = &_store[i];
        }
    }
    if (!e)
    {
        return FLOSS_ERR_NOMEM;
    }
    e->key = key;
    e->used = 1;
    e->len = (uint8_t)len;
    if (len)
    {
        memcpy(e->value, buf, len);
    }
    return FLOSS_OK;
}

floss_err_t floss_hal_store_delete(uint16_t key)
{
    dongle_store_entry_s *e = _store_find(key);
    if (e)
    {
        memset(e, 0, sizeof(*e));
    }
    return FLOSS_OK;
}

/* -------------------------------------------------------------------------- */
/* Link                                                                       */
/* -------------------------------------------------------------------------- */

static floss_link_config_t _link_cfg;
static volatile bool _radio_ready = false;

/* The reliable channel the gamepad opens */
static floss_l2cap_chan_t *_rel = NULL;

/* What the gamepad last got from us */
static bool _host_sent = false;
static dongle_link_host_s _host_last;
static bool _status_sent = false;
static dongle_status_s _status_last;

static void _link_session_end(void)
{
    if (_rel)
    {
        _rel = NULL;
    }
    _host_sent = false;
    _status_sent = false;
    dongle_api_host_link_set_up(false);
}

static void _on_connected(const floss_addr_t *peer)
{
    (void)peer; /* the session starts once the gamepad opens the reliable channel */
}

static void _on_disconnected(uint8_t reason)
{
    (void)reason;
    _link_session_end();
}

#if DONGLE_LINK_DIAG
/* Test builds: the report pacing for computers steps through these, DIAG_INTERVAL_STEP_US each,
 * to compare them under the same radio conditions; bytes 61-62 carry the one in force */
static const uint16_t _diag_intervals[] = {1250, 1290, 1330, 1400};
#define DIAG_INTERVAL_STEP_US 10000000u
static uint16_t _diag_interval = 1250;

static void _diag_interval_task(void)
{
    uint32_t i = (time_us_32() / DIAG_INTERVAL_STEP_US) % (sizeof(_diag_intervals) / sizeof(_diag_intervals[0]));
    if (_diag_intervals[i] != _diag_interval)
    {
        _diag_interval = _diag_intervals[i];
        dongle_api_host_link_set_pc_interval(_diag_interval);
    }
}

/* Test builds: timing in a 64-byte report's last bytes, which SInput leaves unused (bytes 47-62):
 * the gamepad's sequence number and send time, our receive time; the USB send time follows
 * (core_sinput.c), then the pacing asked for. Little endian. */
static void _diag_rx_report(const uint8_t *data, uint8_t len, const floss_link_rx_info_t *info)
{
    uint8_t p[DONGLE_LINK_REPORT_MAX];
    uint16_t n;
    if (!dongle_link_untrim_report(data, len, &p[1], &n))
        return;
    if (n == 64)
    {
        memcpy(&p[1 + 47], &info->seq, 2);
        memcpy(&p[1 + 49], &info->t_pad_us, 4);
        memcpy(&p[1 + 53], &info->t_rx_us, 4);
        memcpy(&p[1 + 61], &_diag_interval, 2);
    }
    p[0] = (uint8_t)n;
    dongle_api_host_link_rx_report(p, (uint16_t)(1u + n), info->seq, info->t_pad_us);
}
#endif

static void _on_input(uint8_t report_id, const uint8_t *data, uint8_t len, const floss_link_rx_info_t *info)
{
    switch (report_id)
    {
    case DONGLE_LINK_IN_REPORT:
#if DONGLE_LINK_DIAG
        _diag_rx_report(data, len, info);
#else
        dongle_api_host_link_rx_report(data, len, info->seq, info->t_pad_us);
#endif
        break;
    case DONGLE_LINK_IN_BULK:
        dongle_api_host_link_rx_bulk(data, len);
        break;
    default:
        break;
    }
}

static const floss_link_callbacks_t _link_cb = {
    .on_connected = _on_connected,
    .on_disconnected = _on_disconnected,
    .on_input = _on_input,
};

static void _rel_on_open(floss_l2cap_chan_t *ch, uint16_t status)
{
    if (status != 0)
    {
        return;
    }
    /* Only from the gamepad on our link, and only one */
    if (!floss_link_connected() || (_rel && _rel != ch))
    {
        floss_l2cap_disconnect(ch);
        return;
    }
    _rel = ch;
    _host_sent = false;
    _status_sent = false;
    dongle_api_host_link_set_up(true);
}

static void _rel_on_close(floss_l2cap_chan_t *ch)
{
    if (ch == _rel)
    {
        _link_session_end();
    }
}

static void _rel_on_data(floss_l2cap_chan_t *ch, const uint8_t *data, uint16_t len)
{
    if (ch == _rel)
    {
        dongle_api_host_link_rx_msg(data, len);
    }
}

static const floss_l2cap_ops_t _rel_ops = {
    .on_open = _rel_on_open,
    .on_close = _rel_on_close,
    .on_data = _rel_on_data,
};

static void _on_gap_event(const floss_gap_evt_t *evt, void *user)
{
    (void)user;
    if (evt->type == FLOSS_GAP_EVT_READY)
    {
        _radio_ready = true;
    }
}

/* Host info on connect and on change, then console / app traffic, then status on change */
static void _link_service(void)
{
    if (!_rel || _rel->state != FLOSS_L2CAP_OPEN)
    {
        return;
    }

    dongle_link_host_s host;
    if (dongle_api_host_link_get_host(&host) && (!_host_sent || memcmp(&host, &_host_last, sizeof(host))))
    {
        uint8_t msg[1 + sizeof(dongle_link_host_s)];
        msg[0] = DONGLE_LINK_MSG_HOST;
        memcpy(&msg[1], &host, sizeof(host));
        if (floss_l2cap_send(_rel, msg, sizeof(msg)) == FLOSS_OK)
        {
            _host_sent = true;
            _host_last = host;
        }
    }

    uint8_t msg[DONGLE_LINK_MSG_MAX];
    uint16_t len;
    if (dongle_api_host_link_peek_msg(msg, &len) && floss_l2cap_send(_rel, msg, len) == FLOSS_OK)
    {
        dongle_api_host_link_msg_sent();
    }

    dongle_status_s status;
    dongle_api_host_link_get_status(&status);
    if (!_status_sent || memcmp(&status, &_status_last, sizeof(status)))
    {
        if (floss_link_send_output(DONGLE_LINK_OUT_STATUS, (const uint8_t *)&status, sizeof(status)) == FLOSS_OK)
        {
            _status_sent = true;
            _status_last = status;
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Core 1 entry                                                               */
/* -------------------------------------------------------------------------- */

bool dongle_radio_ready(void)
{
    return _radio_ready;
}

void dongle_radio_core1_entry(void)
{
    dongle_cfg_host_s cfg = {
        .board = DONGLE_BOARD_ID,
        .fw_version = DONGLE_FW_VERSION,
    };
    dongle_api_host_link_init(&cfg);

    floss_cyw43_driver_init();
    floss_init_t init = {
        .transport = floss_cyw43_transport(),
        .chipset = &floss_chipset_cyw43,
    };
    floss_init(&init);
    floss_gap_add_listener(_on_gap_event, NULL);

    floss_link_config_defaults(&_link_cfg, FLOSS_LINK_ROLE_DONGLE, FLOSS_LINK_CLASSIC);
    _link_cfg.name = DONGLE_LINK_NAME;
    _link_cfg.classic_pad_connects = true; /* the gamepad finds us and runs the link */
    _link_cfg.classic_store_keys = false;  /* pair every time, remember nobody */
    floss_link_init(&_link_cfg, &_link_cb);
    floss_l2cap_register_psm(DONGLE_LINK_PSM_RELIABLE, &_rel_ops, NULL, FLOSS_SEC_ENCRYPTED);

    floss_addr_t addr;
    floss_cyw43_default_addr(&addr);
    floss_hci_set_addr_override(&addr);
    floss_start();

    for (;;)
    {
        floss_poll();
        _link_service();
#if DONGLE_LINK_DIAG
        _diag_interval_task();
#endif
    }
}

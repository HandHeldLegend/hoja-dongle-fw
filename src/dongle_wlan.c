#include "dongle_wlan.h"

#include <string.h>

#include "dongle_wlan_core1.h"
#include "utilities/crosscore_snapshot.h"

/* -------------------------------------------------------------------------- */
/* Gamepad → host: latest unreliable input (snapshot)                          */
/* -------------------------------------------------------------------------- */

typedef struct
{
    uint8_t data[64];
    uint16_t len;
} dongle_wlan_payload_t;

SNAPSHOT_TYPE(unreliable, dongle_wlan_payload_t);
static snapshot_unreliable_t _snap_unreliable;

static dongle_wlan_payload_t _rel_queue[DONGLE_WLAN_QUEUE_LEN];
static volatile uint8_t _rel_head;
static volatile uint8_t _rel_count;

static bool reliable_blocked(void)
{
    return _rel_count > 0;
}

bool dongle_wlan_write_unreliable(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || len > 64 || reliable_blocked())
    {
        return false;
    }

    dongle_wlan_payload_t tmp = {0};
    memcpy(tmp.data, data, len);
    tmp.len = len;
    snapshot_unreliable_write(&_snap_unreliable, &tmp);
    return true;
}

static uint16_t dongle_wlan_read_unreliable(uint8_t *out)
{
    if (!out)
    {
        return 0;
    }

    dongle_wlan_payload_t tmp = {0};
    snapshot_unreliable_read(&_snap_unreliable, &tmp);
    if (tmp.len == 0)
    {
        return 0;
    }

    memcpy(out, tmp.data, tmp.len);
    return tmp.len;
}

bool dongle_wlan_queue_reliable(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0 || len > 64 || _rel_count >= DONGLE_WLAN_QUEUE_LEN)
    {
        return false;
    }

    dongle_wlan_payload_t *slot = &_rel_queue[(_rel_head + _rel_count) % DONGLE_WLAN_QUEUE_LEN];
    memcpy(slot->data, data, len);
    slot->len = len;
    _rel_count++;
    return true;
}

static bool dongle_wlan_pop_reliable(uint8_t *data, uint16_t *len)
{
    if (_rel_count == 0 || !data || !len)
    {
        return false;
    }

    const dongle_wlan_payload_t *head = &_rel_queue[_rel_head];
    memcpy(data, head->data, head->len);
    *len = head->len;

    _rel_head = (uint8_t)((_rel_head + 1) % DONGLE_WLAN_QUEUE_LEN);
    _rel_count--;
    return true;
}

bool dongle_wlan_read_next(uint8_t *data, uint16_t *len)
{
    if (!data || !len)
    {
        return false;
    }

    if (dongle_wlan_pop_reliable(data, len))
    {
        return true;
    }

    *len = dongle_wlan_read_unreliable(data);
    return *len > 0;
}

/* -------------------------------------------------------------------------- */
/* WAKE mailbox: session + dongle_wake_s from gamepad → core_init on core 0  */
/* -------------------------------------------------------------------------- */

typedef struct
{
    bool pending;
    dongle_session_s session;
    dongle_wake_s wake;
} dongle_wlan_wake_mailbox_t;

SNAPSHOT_TYPE(wake_mailbox, dongle_wlan_wake_mailbox_t);
static snapshot_wake_mailbox_t _snap_wake_mailbox;

/* Last WAKE we accepted from ingress — used to ignore duplicate sequential WAKEs. */
static dongle_session_s _wake_last_posted_session;
static dongle_wake_s _wake_last_posted_body;
static bool _wake_last_posted_valid;

static void session_unpack(uint16_t packed, dongle_session_s *out)
{
    memcpy(out, &packed, sizeof(uint16_t));
}

static bool session_equal(const dongle_session_s *a, const dongle_session_s *b)
{
    return a->mode == b->mode && a->id == b->id;
}

static bool wake_body_equal(const dongle_wake_s *a, const dongle_wake_s *b)
{
    return memcmp(a, b, sizeof(dongle_wake_s)) == 0;
}

static bool wake_is_duplicate(const dongle_session_s *session, const dongle_wake_s *wake)
{
    if (!_wake_last_posted_valid)
    {
        return false;
    }

    return session_equal(session, &_wake_last_posted_session) &&
           wake_body_equal(wake, &_wake_last_posted_body);
}

static void wake_mailbox_write(const dongle_session_s *session, const dongle_wake_s *wake)
{
    dongle_wlan_wake_mailbox_t box = {0};
    box.pending = true;
    box.session = *session;
    box.wake = *wake;
    snapshot_wake_mailbox_write(&_snap_wake_mailbox, &box);

    _wake_last_posted_session = *session;
    _wake_last_posted_body = *wake;
    _wake_last_posted_valid = true;
}

bool dongle_wlan_wake_post(const dongle_pkt_s *pkt, bool link_just_established)
{
    if (!pkt || pkt->len < sizeof(dongle_wake_s))
    {
        return false;
    }

    dongle_session_s session;
    session_unpack(pkt->session, &session);

    const dongle_wake_s *wake = (const dongle_wake_s *)pkt->data;

    /*
     * Gamepads often send several identical WAKE packets in a row. Ignore repeats
     * while the link is already up. After an RX timeout, link_just_established is
     * true so the same session id still gets posted once (reconnect path).
     */
    if (!link_just_established && wake_is_duplicate(&session, wake))
    {
        return false;
    }

    wake_mailbox_write(&session, wake);
    return true;
}

bool dongle_wlan_wake_consume(dongle_wake_s *wake, dongle_session_s *session)
{
    if (!wake || !session)
    {
        return false;
    }

    dongle_wlan_wake_mailbox_t box = {0};
    snapshot_wake_mailbox_read(&_snap_wake_mailbox, &box);

    if (!box.pending)
    {
        return false;
    }

    *wake = box.wake;
    *session = box.session;

    /* Clear pending so the same WAKE is never applied twice on core 0. */
    box.pending = false;
    snapshot_wake_mailbox_write(&_snap_wake_mailbox, &box);
    return true;
}

static void wake_mailbox_reset(void)
{
    dongle_wlan_wake_mailbox_t empty = {0};
    snapshot_wake_mailbox_write(&_snap_wake_mailbox, &empty);
    _wake_last_posted_valid = false;
}

void dongle_wlan_reset(void)
{
    _rel_head = 0;
    _rel_count = 0;

    dongle_wlan_payload_t empty = {0};
    snapshot_unreliable_write(&_snap_unreliable, &empty);

    wake_mailbox_reset();
}

bool dongle_wlan_queue_output(const uint8_t *data, uint16_t len)
{
    return dongle_wlan_core1_queue_output(data, len);
}

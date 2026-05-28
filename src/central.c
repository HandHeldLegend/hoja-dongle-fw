#include <dongle.h>

#include "cores/cores.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "central.h"
#include "core1wlan.h"
#include "core0transport.h"
#include "utilities/rgb.h"

#include "utilities/crosscore_snapshot.h"

#define LINK_TIMEOUT_US 5000000u /* 5 s without a matching packet -> link down */

SNAPSHOT_TYPE(status, dongle_status_u);
snapshot_status_t _ss_status;

SNAPSHOT_TYPE(session, dongle_session_s);
snapshot_session_t _ss_session;

/* Wireless link state machine. Written only by core 1; read by core 1 + RGB. */
typedef struct
{
    uint8_t  status;     /* dongle_link_status_t */
    uint64_t last_rx_us; /* time of last accepted (matching-session) packet */
} dongle_link_state_s;

SNAPSHOT_TYPE(link, dongle_link_state_s);
static snapshot_link_t _ss_link; /* zero-init => DONGLE_LINK_DOWN, last_rx_us 0 */

void set_status_rumble(uint8_t left, uint8_t right, uint8_t brake_left, uint8_t brake_right)
{
    dongle_status_u tmp;
    snapshot_status_read(&_ss_status, &tmp);
    tmp.rumble.left = left;
    tmp.rumble.right = right;
    tmp.brake.left = brake_left;
    tmp.brake.right = brake_right;
    snapshot_status_write(&_ss_status, &tmp);
}

void set_status_player_number(uint8_t player)
{
    dongle_status_u tmp;
    snapshot_status_read(&_ss_status, &tmp);
    tmp.player_number = player;
    snapshot_status_write(&_ss_status, &tmp);
}

void set_status_transport(dongle_transport_status_t status)
{
    dongle_status_u tmp;
    snapshot_status_read(&_ss_status, &tmp);
    tmp.transport_status = status;
    snapshot_status_write(&_ss_status, &tmp);
}

void get_status(dongle_status_u *out)
{
    if (out)
    {
        snapshot_status_read(&_ss_status, out);
    }
}


void set_session(dongle_session_s *session)
{
    snapshot_session_write(&_ss_session, session);
}

void get_session(dongle_session_s *session)
{
    snapshot_session_read(&_ss_session, session);
}

void set_link_up(uint64_t now_us)
{
    dongle_link_state_s st;
    st.status = DONGLE_LINK_UP;
    st.last_rx_us = now_us;
    snapshot_link_write(&_ss_link, &st);
}

void set_link_down(void)
{
    dongle_link_state_s st;
    st.status = DONGLE_LINK_DOWN;
    st.last_rx_us = 0;
    snapshot_link_write(&_ss_link, &st);
}

bool link_check_timeout(uint64_t now_us)
{
    dongle_link_state_s st;
    snapshot_link_read(&_ss_link, &st);

    if (st.status == DONGLE_LINK_UP && (now_us - st.last_rx_us) >= LINK_TIMEOUT_US)
    {
        set_link_down();
        return true; /* UP->DOWN edge: caller should drain / prepare reinit */
    }
    return false;
}

dongle_link_status_t get_link_status(void)
{
    dongle_link_state_s st;
    snapshot_link_read(&_ss_link, &st);
    return (dongle_link_status_t)st.status;
}

int main(void)
{
    dongle_rgb_enter_bootloader_if_buttons_held();

    stdio_init_all();

    core_init(core_boot_wake());

    multicore_launch_core1(core1_entry);

    // CYW43 may reconfigure GPIOs at init; reclaim button pins on core0.
    dongle_rgb_gpio_init();
    dongle_rgb_init();

    for(;;)
    {
        core0_task();
    }
}

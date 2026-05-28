#include "dongle_wlan.h"

#include "dongle_wlan_core0.h"
#include "dongle_wlan_core1.h"

#include "utilities/crosscore_snapshot.h"

typedef struct
{
    uint8_t data[64];
    uint16_t len;
} dongle_wlan_unreliable_s;

SNAPSHOT_TYPE(unreliable, dongle_wlan_unreliable_s);
snapshot_unreliable_t _ss_unreliable = {0};

bool dongle_wlan_write_unreliable(const uint8_t *data, uint16_t len)
{
    if(!data || !len) return false;
    len = len > 64 ? 64 : len;
    dongle_wlan_unreliable_s tmp = {0};
    memcpy(tmp.data, data, len);
    snapshot_unreliable_write(&_ss_unreliable, &tmp);
}

uint16_t dongle_wlan_read_unreliable(uint8_t *out)
{
    if(!out) return 0;
    dongle_wlan_unreliable_s tmp = {0};
    snapshot_unreliable_read(&_ss_unreliable, &tmp);
    if(!tmp.len) return 0;
    memcpy(out, tmp.data, tmp.len);
    return tmp.len;
}

bool dongle_wlan_queue_reliable(const uint8_t *data, uint16_t len)
{

}

bool dongle_wlan_pop_reliable(uint8_t *data, uint16_t *len)
{

}

bool dongle_wlan_read_next(uint8_t *data, uint16_t *len)
{
    if(!dongle_wlan_pop_reliable(data, len))
    {
        // Load unreliable
        *len = dongle_wlan_read_unreliable(data);
        return *len>0;
    }
}

#ifndef HOJA_DONGLE_H
#define HOJA_DONGLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    DONGLE_STAT_DOWN,
    DONGLE_STAT_CONNECTED,
} hoja_dongle_status_t;

typedef enum 
{
    HWLAN_REPORT_PASSTHROUGH = 0x01, 
    HWLAN_REPORT_TRANSPORT = 0x02, 
    HWLAN_REPORT_HELLO = 0x03, 
    HWLAN_REPORT_PING = 0x04, 
} hoja_wlan_report_t;

typedef struct 
{
    uint8_t wlan_report_id;
    uint8_t report_format;
    uint8_t data[64];
    uint16_t len;
} hoja_wlan_report_s;

bool wlan_is_connected();
void wlan_report_tunnel_out(hoja_wlan_report_s);

#endif
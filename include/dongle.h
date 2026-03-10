#ifndef HOJA_DONGLE_H
#define HOJA_DONGLE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum 
{
    HWLAN_REPORT_CORE_UNRELIABLE = 0x01, 
    HWLAN_REPORT_CORE_RELIABLE = 0x02, 
    HWLAN_REPORT_STATUS_UNRELIABLE = 0x03, 
    HWLAN_REPORT_HELLO = 0x04,
} hoja_wlan_report_t;

typedef enum
{
    WLAN_CONNSTAT_IDLE = 0,
    WLAN_CONNSTAT_CONNECTED = 1,
    WLAN_CONNSTAT_DISCONNECTED = 2,
} hoja_wlan_connstat_t;

typedef enum 
{
    TRANSPORT_CONNSTAT_IDLE = 0,
    TRANSPORT_CONNSTAT_CONNECTED = 1,
    TRANSPORT_CONNSTAT_DISCONNECTED = 2,
} hoja_transport_connstat_t;

typedef struct
{
    uint8_t rumble_left;
    uint8_t rumble_right;
    uint8_t brake_left;
    uint8_t brake_right;
    uint8_t connection_status;
    uint8_t transport_status;
} hoja_wlan_status_s;

typedef struct __attribute__((packed))
{
    uint16_t session_sig; // RNG for the current gamepad/dongle session
    uint16_t gamepad_sig; // RNG for the last gamepad message
    uint16_t dongle_sig; // RNG for the last dongle message
    uint8_t wlan_report_id;
    uint8_t report_format;
    uint8_t data[64];
    uint16_t len;
} hoja_wlan_report_s;


void dongle_update_rumble(uint8_t rumble_left, uint8_t rumble_right, uint8_t brake_left, uint8_t brake_right);
void dongle_update_transport_status(uint8_t transport_status);
void dongle_update_connection_status(uint8_t connection_status);

hoja_wlan_status_s* dongle_current_status();

bool wlan_is_connected();
void wlan_send_reliable_report(hoja_wlan_report_s *report);

#endif
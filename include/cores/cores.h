#ifndef CORES_CENTRAL_H
#define CORES_CENTRAL_H
#include <stdint.h>

#include <hoja_usb.h>
#include <hoja_types.h>
#include <dongle.h>

typedef enum 
{
    CORE_REPORTFORMAT_UNDEFINED = -1,
    CORE_REPORTFORMAT_SWPRO,
    CORE_REPORTFORMAT_XINPUT,
    CORE_REPORTFORMAT_SINPUT,
    CORE_REPORTFORMAT_SLIPPI,
    CORE_REPORTFORMAT_SNES,
    CORE_REPORTFORMAT_N64,
    CORE_REPORTFORMAT_GAMECUBE
} core_reportformat_t;

typedef struct
{
    const uint8_t *hid_report_descriptor;
    uint16_t hid_report_descriptor_len;
    const uint8_t *config_descriptor;
    uint16_t config_descriptor_len;
    uint16_t vid;
    uint16_t pid;
    char name[64];
    const hoja_usb_device_descriptor_t *device_descriptor;
} core_hid_device_t;

#define CORE_REPORT_DATA_LEN 64
typedef struct
{
    core_reportformat_t reportformat;
    uint8_t size;
    uint8_t data[CORE_REPORT_DATA_LEN];
} core_report_s;

typedef bool (*core_generate_report_t)(core_report_s *out);
typedef void (*core_input_report_tunnel_t)(const uint8_t *data, uint16_t len);
typedef void (*core_output_report_tunnel_t)(const uint8_t *data, uint16_t len);
typedef void (*core_transport_task_t)(uint64_t timestamp);
typedef void (*core_task_t)(uint64_t timestamp);
typedef void (*core_deinit_t)(void);

typedef struct 
{
    core_task_t                   core_task;
    core_transport_task_t         core_transport_task;
    gamepad_transport_t           core_transport;
    core_reportformat_t           core_report_format;
    uint16_t                      core_pollrate_us;
    core_generate_report_t        core_report_generator;
    core_input_report_tunnel_t    core_input_report_tunnel;
    core_output_report_tunnel_t   core_output_report_tunnel;
    core_deinit_t                 core_deinit;
    const core_hid_device_t*      hid_device;
} core_params_s;

core_params_s* core_current_params();
bool core_get_generated_report(core_report_s *out);
void core_input_report_tunnel(const uint8_t *data, uint16_t len);
void core_task(uint64_t timestamp);
void core_deinit();
bool core_init(core_reportformat_t format, const dongle_wake_s *wake);
bool core_transport_is_usb(core_reportformat_t format);

#endif

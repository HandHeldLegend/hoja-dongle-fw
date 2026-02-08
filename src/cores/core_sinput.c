#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "utilities/interval.h"

#include "cores/cores.h"
#include "transport/transport.h"

#define SINPUT_VID 0x2E8A  // Raspberry Pi
#define SINPUT_PID  0x10C6 // Hoja Gamepad
#define SINPUT_NAME "SInput Gamepad"

#define REPORT_ID_SINPUT_INPUT  0x01 // Input Report ID, used for SINPUT input data
#define REPORT_ID_SINPUT_INPUT_CMDDAT  0x02 // Input report ID for command replies
#define REPORT_ID_SINPUT_OUTPUT_CMDDAT 0x03 // Output Haptic Report ID, used for haptics and commands

#define SINPUT_COMMAND_HAPTIC       0x01
#define SINPUT_COMMAND_FEATURES     0x02
#define SINPUT_COMMAND_PLAYERLED    0x03

#define SINPUT_REPORT_LEN_COMMAND 48 
#define SINPUT_REPORT_LEN_INPUT   64

// When we receive our features report data
// we can ready this flag
bool _sinput_features_report_got = false;
uint8_t _sinput_features_report_data[SINPUT_REPORT_LEN_INPUT] = {0};

const hoja_usb_device_descriptor_t _sinput_device_descriptor = {
    .bLength = sizeof(hoja_usb_device_descriptor_t),
    .bDescriptorType = HUSB_DESC_DEVICE,
    .bcdUSB = 0x0210, // Changed from 0x0200 to 2.1 for BOS & WebUSB
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,

    .bMaxPacketSize0 = 64,

    .idVendor = SINPUT_VID,
    .idProduct = SINPUT_PID, // board_config PID

    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
    };

const uint8_t _sinput_hid_report_descriptor[139] = {
    0x05, 0x01,                    // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,                    // Usage (Gamepad)
    0xA1, 0x01,                    // Collection (Application)
    
    // INPUT REPORT ID 0x01 - Main gamepad data
    0x85, 0x01,                    //   Report ID (1)
    
    // Padding bytes (bytes 2-3) - Plug status and Charge Percent (0-100)
    0x06, 0x00, 0xFF,              //   Usage Page (Vendor Defined)
    0x09, 0x01,                    //   Usage (Vendor Usage 1)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x25, 0xFF,                    //   Logical Maximum (255)
    0x75, 0x08,                    //   Report Size (8)
    0x95, 0x02,                    //   Report Count (2)
    0x81, 0x02,                    //   Input (Data,Var,Abs)

    // --- 32 buttons ---
    0x05, 0x09,        // Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x20,        //   Usage Maximum (Button 32)
    0x15, 0x00,        //   Logical Min (0)
    0x25, 0x01,        //   Logical Max (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x20,        //   Report Count (32)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    
    // Analog Sticks and Triggers
    0x05, 0x01,                    // Usage Page (Generic Desktop)
    // Left Stick X (bytes 8-9)
    0x09, 0x30,                    //   Usage (X)
    // Left Stick Y (bytes 10-11)
    0x09, 0x31,                    //   Usage (Y)
    // Right Stick X (bytes 12-13)
    0x09, 0x32,                    //   Usage (Z)
    // Right Stick Y (bytes 14-15)
    0x09, 0x35,                    //   Usage (Rz)
    // Right Trigger (bytes 18-19)
    0x09, 0x33,                    //   Usage (Rx)
    // Left Trigger  (bytes 16-17)
    0x09, 0x34,                     //  Usage (Ry)
    0x16, 0x00, 0x80,              //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,              //   Logical Maximum (32767)
    0x75, 0x10,                    //   Report Size (16)
    0x95, 0x06,                    //   Report Count (6)
    0x81, 0x02,                    //   Input (Data,Var,Abs)
    
    // Padding/Reserved data (bytes 20-63) - 44 bytes
    // This includes gyro/accel data and counter that apps can use if supported
    0x06, 0x00, 0xFF,              // Usage Page (Vendor Defined)
    
    // Motion Input Timestamp (Microseconds)
    0x09, 0x20,                    //   Usage (Vendor Usage 0x20)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x26, 0xFF, 0xFF,              //   Logical Maximum (655535)
    0x75, 0x20,                    //   Report Size (32)
    0x95, 0x01,                    //   Report Count (1)
    0x81, 0x02,                    //   Input (Data,Var,Abs)

    // Motion Input Accelerometer XYZ (Gs) and Gyroscope XYZ (Degrees Per Second)
    0x09, 0x21,                    //   Usage (Vendor Usage 0x21)
    0x16, 0x00, 0x80,              //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,              //   Logical Maximum (32767)
    0x75, 0x10,                    //   Report Size (16)
    0x95, 0x06,                    //   Report Count (6)
    0x81, 0x02,                    //   Input (Data,Var,Abs)

    // Reserved padding
    0x09, 0x22,                    //   Usage (Vendor Usage 0x22)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x26, 0xFF, 0x00,              //   Logical Maximum (255)
    0x75, 0x08,                    //   Report Size (8)
    0x95, 0x1D,                    //   Report Count (29)
    0x81, 0x02,                    //   Input (Data,Var,Abs)
    
    // INPUT REPORT ID 0x02 - Vendor COMMAND data
    0x85, 0x02,                    //   Report ID (2)
    0x09, 0x23,                    //   Usage (Vendor Usage 0x23)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x26, 0xFF, 0x00,              //   Logical Maximum (255)
    0x75, 0x08,                    //   Report Size (8 bits)
    0x95, 0x3F,                    //   Report Count (63) - 64 bytes minus report ID
    0x81, 0x02,                    //   Input (Data,Var,Abs)

    // OUTPUT REPORT ID 0x03 - Vendor COMMAND data
    0x85, 0x03,                    //   Report ID (3)
    0x09, 0x24,                    //   Usage (Vendor Usage 0x24)
    0x15, 0x00,                    //   Logical Minimum (0)
    0x26, 0xFF, 0x00,              //   Logical Maximum (255)
    0x75, 0x08,                    //   Report Size (8 bits)
    0x95, 0x2F,                    //   Report Count (47) - 48 bytes minus report ID
    0x91, 0x02,                    //   Output (Data,Var,Abs)

    0xC0                           // End Collection 
};

#define SINPUT_CONFIG_DESCRIPTOR_LEN 41
const uint8_t _sinput_configuration_descriptor[SINPUT_CONFIG_DESCRIPTOR_LEN] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    HUSB_CONFIG_DESCRIPTOR(1, 1, 0, SINPUT_CONFIG_DESCRIPTOR_LEN, HUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 350),

    // Interface
    9, HUSB_DESC_INTERFACE, 0x00, 0x00, 0x02, HUSB_CLASS_HID, 0x00, 0x00, 0x00,
    // HID Descriptor
    9, HHID_DESC_TYPE_HID, HUSB_U16_TO_U8S_LE(0x0111), 0, 1, HHID_DESC_TYPE_REPORT, HUSB_U16_TO_U8S_LE(sizeof(_sinput_hid_report_descriptor)),
    // Endpoint Descriptor
    7, HUSB_DESC_ENDPOINT, 0x81, HUSB_XFER_INTERRUPT, HUSB_U16_TO_U8S_LE(64), 1,
    // Endpoint Descriptor
    7, HUSB_DESC_ENDPOINT, 0x01, HUSB_XFER_INTERRUPT, HUSB_U16_TO_U8S_LE(64), 4
};

#define SINPUT_CLAMP(val, min, max) ((val) < (min) ? (min) : ((val) > (max) ? (max) : (val)))



volatile uint8_t _si_current_command = 0;

void _core_sinput_report_tunnel_cb(const uint8_t *data, uint16_t len)
{
    if(len<2) return;


}

bool _core_sinput_get_generated_report(core_report_s *out)
{
    out->reportformat=CORE_REPORTFORMAT_SINPUT;
    out->size=64; // 64 bytes including our report ID

    // Handle features request response
    if(_si_current_command==SINPUT_COMMAND_FEATURES)
    {
        if(_sinput_features_report_got)
        {
            memcpy(out->data, _sinput_features_report_data, SINPUT_REPORT_LEN_INPUT);
        }
        _si_current_command = 0;
    }
    else 
    {
        // Forward our most up to date input data
        // TO DO
    }

    return true;
}

const core_hid_device_t _sinput_hid_device = {
    .config_descriptor = _sinput_configuration_descriptor,
    .config_descriptor_len = SINPUT_CONFIG_DESCRIPTOR_LEN,
    .hid_report_descriptor = _sinput_hid_report_descriptor,
    .hid_report_descriptor_len = sizeof(_sinput_hid_report_descriptor),
    .device_descriptor = &_sinput_device_descriptor,
    .name = SINPUT_NAME,
    .pid = SINPUT_PID,
    .vid = SINPUT_VID,
};

/*------------------------------------------------*/

// Public Functions
void core_sinput_deinit()
{

}

volatile bool _sinput_transport_running = false;
core_params_s *_sinput_core_params = NULL;

bool core_sinput_init(core_params_s *params)
{
    _sinput_core_params = params;

    params->transport_dev_mac[5] += 2;
    
    switch(params->transport_type)
    {
        case GAMEPAD_TRANSPORT_USB:
        params->core_pollrate_us = 1000;
        break;

        // Unsupported transport methods
        default:
        return false;
    }

    params->hid_device = &_sinput_hid_device;

    params->core_report_format    = CORE_REPORTFORMAT_SINPUT;
    params->core_report_generator = _core_sinput_get_generated_report;
    params->core_report_tunnel    = _core_sinput_report_tunnel_cb;

    return transport_init(params);
}

void core_sinput_task(uint64_t timestamp)
{
    // The idea here is that we do not want to init
    // TinyUSB until we actually have our feature
    // data. Once we receive the features data
    // from the gamepad, we init the TinyUSB stack
    // so that Steam and other programs may have 
    // a chance to pick up all of the correct properties
    if(!_sinput_transport_running)
    {
        if(_sinput_features_report_got && _sinput_core_params)
        {
            _sinput_transport_running = transport_init(_sinput_core_params);
        }
    }
    else 
    {
        // Run our transport task
        if(_sinput_core_params->transport_task)
        _sinput_core_params->transport_task(timestamp);
    }
}

bool _sinput_compare_features(const uint8_t *in)
{
    for(uint16_t i = 0; i < SINPUT_REPORT_LEN_COMMAND; i++)
    {
        // For any byte that doesn't match, we will return false
        if(in[i] != _sinput_features_report_data[i]) return false;
    }

    return true;
}

// WLAN Packets INPUT from gamepad we receive are tunneled into here
void core_sinput_input_tunnel(const uint8_t *data, uint16_t len)
{
    switch(data[0])
    {
        // Standard input report data
        case REPORT_ID_SINPUT_INPUT:
        break;

        // Command reply data
        case REPORT_ID_SINPUT_INPUT_CMDDAT:
        uint8_t command = data[1];
        if(command == SINPUT_COMMAND_FEATURES && len==SINPUT_REPORT_LEN_INPUT)
        {
            if(_sinput_features_report_got)
            {
                // If we return true, proceed, otherwise reboot
                // because need to re-init
                if(!_sinput_compare_features(data))
                {
                    // REBOOT HERE
                }
            }

            memcpy(_sinput_features_report_data, data, SINPUT_REPORT_LEN_INPUT);
            _sinput_features_report_got = true;
        }
        break;
    }
}

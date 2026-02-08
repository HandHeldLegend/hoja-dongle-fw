#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cores/cores.h"
#include "transport/transport.h"

#if defined(HOJA_USB_VID)
#define SINPUT_VID  HOJA_USB_VID
#else
#define SINPUT_VID 0x2E8A // Raspberry Pi
#endif

#if defined(HOJA_USB_PID)
#define SINPUT_PID  HOJA_USB_PID
#else
#define SINPUT_PID  0x10C6 // Hoja Gamepad
#endif

#if defined(HOJA_DEVICE_NAME)
#define SINPUT_NAME HOJA_DEVICE_NAME
#else
#define SINPUT_NAME "SInput Gamepad"
#endif

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

#define REPORT_ID_SINPUT_INPUT  0x01 // Input Report ID, used for SINPUT input data
#define REPORT_ID_SINPUT_INPUT_CMDDAT  0x02 // Input report ID for command replies
#define REPORT_ID_SINPUT_OUTPUT_CMDDAT 0x03 // Output Haptic Report ID, used for haptics and commands

#define SINPUT_COMMAND_HAPTIC       0x01
#define SINPUT_COMMAND_FEATURES     0x02
#define SINPUT_COMMAND_PLAYERLED    0x03

#define SINPUT_REPORT_LEN_COMMAND 48 
#define SINPUT_REPORT_LEN_INPUT   64

#pragma pack(push, 1) // Ensure byte alignment
// Input report (Report ID: 1)
typedef struct
{
    uint8_t plug_status;    // Plug Status Format
    uint8_t charge_percent; // 0-100

    union {
        struct {
            uint8_t button_south : 1;
            uint8_t button_east  : 1;
            uint8_t button_west  : 1;
            uint8_t button_north : 1;
            uint8_t dpad_up    : 1;
            uint8_t dpad_down  : 1;
            uint8_t dpad_left  : 1;
            uint8_t dpad_right : 1;
        };
        uint8_t buttons_1;
    };

    union
    {
        struct
        {
            uint8_t button_stick_left : 1;
            uint8_t button_stick_right : 1;
            uint8_t button_l_shoulder : 1;
            uint8_t button_r_shoulder : 1;
            uint8_t button_l_trigger : 1;
            uint8_t button_r_trigger : 1;
            uint8_t button_l_paddle_1 : 1;
            uint8_t button_r_paddle_1 : 1;
        };
        uint8_t buttons_2;
    };

    union
    {
        struct
        {
            uint8_t button_start  : 1;
            uint8_t button_select : 1;
            uint8_t button_guide  : 1;
            uint8_t button_share  : 1;
            uint8_t button_l_paddle_2 : 1;
            uint8_t button_r_paddle_2 : 1;
            uint8_t button_l_touchpad : 1;
            uint8_t button_r_touchpad : 1;
        };
        uint8_t buttons_3;
    };

    union
    {
        struct
        {
            uint8_t button_power   : 1;
            uint8_t button_misc_4  : 1;
            uint8_t button_misc_5  : 1;
            uint8_t button_misc_6  : 1;
            
            // Misc 7 through 10 is unused by
            // SDL currently!
            uint8_t button_misc_7  : 1; 
            uint8_t button_misc_8  : 1;
            uint8_t button_misc_9  : 1;
            uint8_t button_misc_10 : 1;
        };
        uint8_t buttons_4;
    };

    int16_t left_x;             // Left stick X
    int16_t left_y;             // Left stick Y
    int16_t right_x;            // Right stick X
    int16_t right_y;            // Right stick Y
    int16_t trigger_l;          // Left trigger
    int16_t trigger_r;          // Right trigger

    uint32_t imu_timestamp_us;  // IMU Timestamp
    int16_t accel_x;            // Accelerometer X
    int16_t accel_y;            // Accelerometer Y
    int16_t accel_z;            // Accelerometer Z
    int16_t gyro_x;             // Gyroscope X
    int16_t gyro_y;             // Gyroscope Y
    int16_t gyro_z;             // Gyroscope Z

    int16_t touchpad_1_x;       // Touchpad/trackpad
    int16_t touchpad_1_y;
    int16_t touchpad_1_pressure;

    int16_t touchpad_2_x;
    int16_t touchpad_2_y;
    int16_t touchpad_2_pressure;

    uint8_t reserved_bulk[17];  // Reserved for command data
} sinput_input_s;
#pragma pack(pop)

#define SINPUT_INPUT_SIZE sizeof(sinput_input_s)

#pragma pack(push, 1) // Ensure byte alignment
typedef struct 
{
    uint8_t type;

    union 
    {
        // Frequency Amplitude pairs
        struct 
        {
            struct
            {
                uint16_t frequency_1;
                uint16_t amplitude_1;
                uint16_t frequency_2;
                uint16_t amplitude_2;
            } left;

            struct
            {
                uint16_t frequency_1;
                uint16_t amplitude_1;
                uint16_t frequency_2;
                uint16_t amplitude_2;
            } right;
            
        } type_1;

        // Basic ERM simulation model
        struct 
        {
            struct 
            {
                uint8_t amplitude;
                bool    brake;
            } left;

            struct 
            {
                uint8_t amplitude;
                bool    brake;
            } right;
            
        } type_2; 
    };
} sinput_haptic_s;
#pragma pack(pop)

#define SINPUT_HAPTIC_SIZE sizeof(sinput_haptic_s)

#pragma pack(push, 1) // Ensure byte alignment
typedef union
{
    struct
    {
        uint8_t rumble_supported : 1;
        uint8_t player_leds_supported : 1;
        uint8_t accelerometer_supported : 1;
        uint8_t gyroscope_supported : 1;
        uint8_t left_analog_stick_supported : 1;
        uint8_t right_analog_stick_supported : 1;
        uint8_t left_analog_trigger_supported : 1;
        uint8_t right_analog_trigger_supported : 1;
    };
    uint8_t value;
} sinput_featureflags_1_u;
#pragma pack(pop)

#pragma pack(push, 1) // Ensure byte alignment
typedef union
{
    struct
    {
        uint8_t left_touchpad_supported  : 1;
        uint8_t right_touchpad_supported : 1;
        uint8_t reserved : 6;
    };
    uint8_t value;
} sinput_featureflags_2_u;
#pragma pack(pop)

#define SINPUT_MASK_SEWN 0x0F
#define SINPUT_MASK_DPAD 0xF0

// Bumpers (L1, R1)
#define SINPUT_MASK_BUMPERS 0x0C

// Triggers (L2, R2)
#define SINPUT_MASK_TRIGGERS 0x30

// Start + Select
#define SINPUT_MASK_STARTSELECT 0x03

// Home
#define SINPUT_MASK_HOME 0x04

// Capture
#define SINPUT_MASK_CAPTURE 0x08

// Stick Click: L3
#define SINPUT_MASK_LSTICK 0x01

// Stick Click: R3
#define SINPUT_MASK_RSTICK 0x02

// Upper Grips (L4, R4)
#define SINPUT_MASK_UPPERGRIPS 0xC0

// Lower Grips (L5, R5)
#define SINPUT_MASK_LOWERGRIPS 0x30

// Power
#define SINPUT_MASK_POWER 0x01

#define SINPUT_MASK_0 ( SINPUT_MASK_SEWN | SINPUT_MASK_DPAD )
#define SINPUT_MASK_1 ( SINPUT_MASK_LSTICK | SINPUT_MASK_RSTICK | SINPUT_MASK_TRIGGERS | SINPUT_MASK_BUMPERS | SINPUT_MASK_UPPERGRIPS )
#define SINPUT_MASK_2 ( SINPUT_MASK_STARTSELECT | SINPUT_MASK_HOME | SINPUT_MASK_CAPTURE | SINPUT_MASK_LOWERGRIPS )
#define SINPUT_MASK_3 ( SINPUT_MASK_POWER )

void _si_generate_input_masks(uint8_t *masks)
{

}

uint16_t _sinput_report_interval_us = 1000;

void _si_cmd_haptics(const uint8_t *data)
{
    haptic_packet_s packet = {0};

    sinput_haptic_s haptic = {0};

    memcpy(&haptic, data, sizeof(sinput_haptic_s));

    switch (haptic.type)
    {
        default:
        break;

        case 1:
            // Haptic type
        break;

        case 2:
            // ERM type
        break;
    } 
}

void _si_generate_features(uint8_t *buffer)
{
    sinput_featureflags_1_u feature_flags = {0};

    // Gamepad Type (Derived from SDL)
    /* 
    typedef enum SDL_GamepadType
    {
        SDL_GAMEPAD_TYPE_UNKNOWN = 0,
        SDL_GAMEPAD_TYPE_STANDARD,
        SDL_GAMEPAD_TYPE_XBOX360,
        SDL_GAMEPAD_TYPE_XBOXONE,
        SDL_GAMEPAD_TYPE_PS3,
        SDL_GAMEPAD_TYPE_PS4,
        SDL_GAMEPAD_TYPE_PS5,
        SDL_GAMEPAD_TYPE_NINTENDO_SINPUT_PRO,
        SDL_GAMEPAD_TYPE_NINTENDO_SINPUT_JOYCON_LEFT,
        SDL_GAMEPAD_TYPE_NINTENDO_SINPUT_JOYCON_RIGHT,
        SDL_GAMEPAD_TYPE_NINTENDO_SINPUT_JOYCON_PAIR,
        SDL_GAMEPAD_TYPE_GAMECUBE,
        SDL_GAMEPAD_TYPE_COUNT
    } SDL_GamepadType;
     */
    #if defined(HOJA_SINPUT_GAMEPAD_TYPE)
    buffer[4] = HOJA_SINPUT_GAMEPAD_TYPE;
    #else 
    buffer[4] = 0;
    #endif

    uint8_t sub_type = 0;
    uint8_t face_style = 0;

    #if defined(HOJA_SINPUT_GAMEPAD_SUBTYPE)
    sub_type = HOJA_SINPUT_GAMEPAD_SUBTYPE & 0x1F; // Gamepad Sub-type (leave as zero in most cases)
    #endif

    #if defined(HOJA_SINPUT_GAMEPAD_FACESTYLE)
    face_style = HOJA_SINPUT_GAMEPAD_FACESTYLE & 0x7;
    #endif 

    buffer[5] = (face_style << 5) | sub_type;

    uint16_t polling_rate_us = 1000;

    //switch(hoja_get_status().gamepad_method)
    //{
    //    case GAMEPAD_METHOD_BLUETOOTH:
    //    polling_rate_us = 8000;
    //    break;
    //    case GAMEPAD_METHOD_WLAN:
    //    polling_rate_us = 2000;
    //    break;
    //}
        
    memcpy(&buffer[6], &polling_rate_us, sizeof(polling_rate_us));

    _si_generate_input_masks(&buffer[12]);

    buffer[16] = 0; // Touchpad count
    buffer[17] = 0; // Touchpad finger count

    core_params_s *params = core_current_params();

    // 18-23 is the MAC
    memcpy(&buffer[18], params->transport_dev_mac, 6);
}

int16_t _sinput_scale_trigger(uint16_t val)
{
    if (val > 4095) val = 4095; // Clamp just in case

    // Scale: map [0, 4095] → [INT16_MIN, INT16_MAX]
    // The range of INT16 is 65535, so multiply first to preserve precision
    return (int16_t)(((int32_t)val * 65535) / 4095 + INT16_MIN);
}

int16_t _sinput_scale_axis(int16_t input_axis)
{   
    return SINPUT_CLAMP(input_axis * 16, INT16_MIN, INT16_MAX);
}

volatile uint8_t _si_current_command = 0;
uint8_t _si_response_report[64] = {0};

void _core_sinput_report_tunnel_cb(const uint8_t *data, uint16_t len)
{
    if(len<2) return;

    switch(data[0])
    {
        default:
        return;

        case SINPUT_COMMAND_HAPTIC:
        _si_cmd_haptics(&(data[1]));
        break;

        case SINPUT_COMMAND_FEATURES:
        _si_current_command = SINPUT_COMMAND_FEATURES;
        break;

        case SINPUT_COMMAND_PLAYERLED:
        tp_evt_s evt = {
            .evt=TP_EVT_PLAYERLED,
            .evt_playernumber = {.player_number=data[1]}
        };
        transport_evt_cb(evt);
        break;
    }
}

bool _core_sinput_get_generated_report(core_report_s *out)
{
    out->reportformat=CORE_REPORTFORMAT_SINPUT;
    out->size=64; // 64 bytes including our report ID

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
bool core_sinput_init(core_params_s *params)
{
    params->transport_dev_mac[5] += 2;
    
    switch(params->transport_type)
    {
        case GAMEPAD_TRANSPORT_USB:
        params->core_pollrate_us = 1000;
        break;

        case GAMEPAD_TRANSPORT_BLUETOOTH:
        params->core_pollrate_us = 8000;
        break;

        case GAMEPAD_TRANSPORT_WLAN:
        params->core_pollrate_us = 2000;
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

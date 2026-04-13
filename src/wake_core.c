#include "wake_core.h"

#include <stdint.h>

#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

static const char *TAG = "wake_core";

static esp_err_t ble_init_internal(void);
static bool ble_connected;
static bool ble_hid_send_wake_signal(void);

static volatile bool s_usb_suspended = false;
static volatile bool s_remote_wakeup_allowed = false;

static const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_MOUSE()
};

#define EPNUM_HID 0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t config_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,
        1,
        0,
        CONFIG_TOTAL_LEN,
        TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP,
        100
    ),
    TUD_HID_DESCRIPTOR(
        0,
        0,
        HID_ITF_PROTOCOL_MOUSE,
        sizeof(hid_report_descriptor),
        EPNUM_HID,
        16,
        10
    ),
};

static esp_err_t usb_init_internal(void)
{
    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = NULL;
    tusb_cfg.descriptor.string = NULL;
    tusb_cfg.descriptor.full_speed_config = config_descriptor;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = config_descriptor;
#endif
    return tinyusb_driver_install(&tusb_cfg);
}

esp_err_t wake_init(void)
{
#if USE_USB
    esp_err_t err = usb_init_internal();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TinyUSB ready");
    }
    return err;
#elif USE_BLE
    return ble_init_internal();
#else
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

bool wake_trigger(void)
{
    #if USE_USB
        return wake_trigger_usb();
    #elif USE_BLE
        return wake_trigger_ble();
    #else
        return false;
    #endif
}

bool wake_trigger_usb(void)
{
    bool ok = tud_remote_wakeup();
    ESP_LOGI(TAG, "tud_remote_wakeup() -> %d", ok);
    return ok;
}

bool wake_trigger_ble(void)
{
    if (!ble_connected) {
        ESP_LOGW(TAG, "BLE not connected");
        return false;
    }

    return ble_hid_send_wake_signal();
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    s_usb_suspended = true;
    s_remote_wakeup_allowed = remote_wakeup_en;
    ESP_LOGI(TAG, "USB suspended, remote_wakeup_en=%d", remote_wakeup_en);
}

void tud_resume_cb(void)
{
    s_usb_suspended = false;
    s_remote_wakeup_allowed = false;
    ESP_LOGI(TAG, "USB resumed");
}

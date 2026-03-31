#include "wake_core.h"

#include <stdint.h>

#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

static const char *TAG = "wake_core";

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
    esp_err_t err = usb_init_internal();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "TinyUSB ready");
    }
    return err;
}

wake_state_t wake_get_state(void)
{
    wake_state_t st = {
        .mounted = tud_mounted(),
        .suspended = s_usb_suspended,
        .remote_wakeup_allowed = s_remote_wakeup_allowed,
    };
    return st;
}

bool wake_is_ready(void)
{
    wake_state_t st = wake_get_state();
    return st.mounted && st.suspended && st.remote_wakeup_allowed;
}

bool wake_trigger(void)
{
    ESP_LOGI(TAG,
             "wake request: mounted=%d suspended=%d remote_wakeup_allowed=%d",
             tud_mounted(),
             s_usb_suspended,
             s_remote_wakeup_allowed);

    if (!wake_is_ready()) {
        ESP_LOGW(TAG, "remote wakeup not allowed now");
        return false;
    }

    bool ok = tud_remote_wakeup();
    ESP_LOGI(TAG, "tud_remote_wakeup() -> %d", ok);
    return ok;
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

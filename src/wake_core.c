#include "wake_core.h"

#include <stdint.h>

#include "esp_log.h"

static const char *TAG = "wake_core";

#if defined(ARDUINO) && defined(CONFIG_IDF_TARGET_ESP32S3)
#error "ESP32-S3 Arduino builds must link precompiled src/esp32s3/libwakecore.a (run ./tools/build_idf_archive.sh esp32s3)."
#endif

#if USE_USB
#if defined(ARDUINO)
#if __has_include("esp32-hal-tinyusb.h") && __has_include("tusb.h")
#define WAKE_USB_BACKEND_ARDUINO 1
#elif __has_include("tinyusb.h") && __has_include("tinyusb_default_config.h")
#define WAKE_USB_BACKEND_IDF 1
#else
#define WAKE_USB_BACKEND_NONE 1
#endif
#elif __has_include("tinyusb.h") && __has_include("tinyusb_default_config.h")
#define WAKE_USB_BACKEND_IDF 1
#elif __has_include("esp32-hal-tinyusb.h") && __has_include("tusb.h")
#define WAKE_USB_BACKEND_ARDUINO 1
#else
#define WAKE_USB_BACKEND_NONE 1
#endif

#if WAKE_USB_BACKEND_IDF
#if __has_include("class/hid/hid.h")
#include "class/hid/hid.h"
#endif
#if __has_include("class/hid/hid_device.h")
#include "class/hid/hid_device.h"
#elif __has_include("tinyusb/src/class/hid/hid_device.h")
#include "tinyusb/src/class/hid/hid_device.h"
#endif
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#elif WAKE_USB_BACKEND_ARDUINO
#include "esp32-hal-tinyusb.h"
#include "tusb.h"
#endif

static volatile bool s_usb_initialized = false;
static volatile bool s_usb_suspended = false;
static volatile bool s_usb_remote_wakeup_allowed = false;
static volatile uint32_t s_usb_suspend_seq = 0;

#if WAKE_USB_BACKEND_IDF
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
    if (s_usb_initialized) {
        return ESP_OK;
    }

    tinyusb_config_t tusb_cfg = TINYUSB_DEFAULT_CONFIG();
    tusb_cfg.descriptor.device = NULL;
    tusb_cfg.descriptor.string = NULL;
    tusb_cfg.descriptor.full_speed_config = config_descriptor;
#if (TUD_OPT_HIGH_SPEED)
    tusb_cfg.descriptor.high_speed_config = config_descriptor;
#endif
    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_usb_initialized = true;
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "TinyUSB already initialized by another owner; reusing existing stack");
        }
        return ESP_OK;
    }
    return err;
}

static bool wake_trigger_usb(void)
{
    ESP_LOGI(TAG,
             "wake request[source=api]: mounted=%d suspended=%d remote_wakeup_allowed=%d",
             tud_mounted() ? 1 : 0,
             s_usb_suspended ? 1 : 0,
             s_usb_remote_wakeup_allowed ? 1 : 0);

    if (tud_mounted() && s_usb_suspended) {
        if (!s_usb_remote_wakeup_allowed) {
            ESP_LOGW(TAG, "remote_wakeup_allowed=0, trying tud_remote_wakeup() anyway");
        }

        bool ok = tud_remote_wakeup();
        ESP_LOGI(TAG, "tud_remote_wakeup() -> %d", ok ? 1 : 0);
        if (!ok && !s_usb_remote_wakeup_allowed) {
            ESP_LOGW(TAG, "host may not have enabled USB remote wakeup permission");
        }
        return ok;
    }

    ESP_LOGW(TAG, "remote wakeup not allowed now");
    return false;
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

#elif WAKE_USB_BACKEND_ARDUINO
static esp_err_t usb_init_internal(void)
{
#if defined(SOC_USB_OTG_SUPPORTED) && SOC_USB_OTG_SUPPORTED
#if defined(CONFIG_TINYUSB_ENABLED) && CONFIG_TINYUSB_ENABLED
    if (s_usb_initialized) {
        return ESP_OK;
    }

    tinyusb_device_config_t cfg = {
        .vid = USB_ESPRESSIF_VID,
        .pid = 0x0002,
        .product_name = "ESP32WakeCore",
        .manufacturer_name = "Espressif",
        .serial_number = "0",
        .fw_version = 0x0100,
        .usb_version = 0x0200,
        .usb_class = TUSB_CLASS_MISC,
        .usb_subclass = MISC_SUBCLASS_COMMON,
        .usb_protocol = MISC_PROTOCOL_IAD,
        .usb_attributes = (uint8_t)(TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP),
        .usb_power_ma = 100,
        .webusb_enabled = false,
        .webusb_url = "espressif.github.io/arduino-esp32/webusb.html",
    };

    esp_err_t err = tinyusb_init(&cfg);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_usb_initialized = true;
        return ESP_OK;
    }
    return err;
#else
    ESP_LOGE(TAG, "CONFIG_TINYUSB_ENABLED is off (set USB Mode to USB-OTG TinyUSB)");
    return ESP_ERR_NOT_SUPPORTED;
#endif
#else
    ESP_LOGE(TAG, "SOC_USB_OTG_SUPPORTED is false for this target");
    return ESP_ERR_NOT_SUPPORTED;
#endif
}

static bool wake_trigger_usb(void)
{
    ESP_LOGI(TAG,
             "wake request[source=api]: mounted=%d suspended=%d remote_wakeup_allowed=%d",
             tud_mounted() ? 1 : 0,
             s_usb_suspended ? 1 : 0,
             s_usb_remote_wakeup_allowed ? 1 : 0);

    if (tud_mounted() && s_usb_suspended) {
        if (!s_usb_remote_wakeup_allowed) {
            ESP_LOGW(TAG, "remote_wakeup_allowed=0, trying tud_remote_wakeup() anyway");
        }

        bool ok = tud_remote_wakeup();
        ESP_LOGI(TAG, "tud_remote_wakeup() -> %d", ok ? 1 : 0);
        if (!ok && !s_usb_remote_wakeup_allowed) {
            ESP_LOGW(TAG, "host may not have enabled USB remote wakeup permission");
        }
        return ok;
    }

    ESP_LOGW(TAG, "remote wakeup not allowed now");
    return false;
}
#else
static esp_err_t usb_init_internal(void)
{
    ESP_LOGE(TAG, "No TinyUSB backend available");
    return ESP_ERR_NOT_SUPPORTED;
}

static bool wake_trigger_usb(void)
{
    ESP_LOGW(TAG, "USB wake unavailable in this build");
    return false;
}
#endif

#if WAKE_USB_BACKEND_IDF || WAKE_USB_BACKEND_ARDUINO
void __attribute__((weak)) tud_suspend_cb(bool remote_wakeup_en)
{
    wake_usb_on_suspend(remote_wakeup_en);
}

void __attribute__((weak)) tud_resume_cb(void)
{
    wake_usb_on_resume();
}
#endif
#endif

#if USE_BLE
static volatile bool s_ble_connected = false;

esp_err_t __attribute__((weak)) wake_ble_init(void)
{
    s_ble_connected = false;
    ESP_LOGI(TAG, "BLE init stub");
    return ESP_OK;
}

bool __attribute__((weak)) wake_ble_is_connected(void)
{
    return s_ble_connected;
}

bool __attribute__((weak)) wake_ble_send_wake(void)
{
    ESP_LOGW(TAG, "wake_ble_send_wake() stub");
    return false;
}

static esp_err_t ble_init_internal(void)
{
    esp_err_t err = wake_ble_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "BLE ready");
    }
    return err;
}

static bool wake_trigger_ble(void)
{
    bool connected = wake_ble_is_connected();
    s_ble_connected = connected;
    if (!connected) {
        ESP_LOGW(TAG, "BLE not connected");
        return false;
    }

    {
        bool ok = wake_ble_send_wake();
        ESP_LOGI(TAG, "wake_ble_send_wake() -> %d", ok ? 1 : 0);
        return ok;
    }
}
#endif

void wake_usb_on_suspend(bool remote_wakeup_en)
{
#if USE_USB
    s_usb_suspended = true;
    s_usb_remote_wakeup_allowed = remote_wakeup_en;
    s_usb_suspend_seq++;
    ESP_LOGI(TAG, "USB suspended, remote_wakeup_en=%d", remote_wakeup_en ? 1 : 0);
#else
    (void)remote_wakeup_en;
#endif
}

void wake_usb_on_resume(void)
{
#if USE_USB
    s_usb_suspended = false;
    s_usb_remote_wakeup_allowed = false;
    ESP_LOGI(TAG, "USB resumed");
#endif
}

uint32_t wake_usb_get_suspend_seq(void)
{
#if USE_USB
    return s_usb_suspend_seq;
#else
    return 0;
#endif
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
    ESP_LOGW(TAG, "No wake transport selected");
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
    ESP_LOGW(TAG, "No wake transport selected");
    return false;
#endif
}

wake_state_t wake_get_state(void)
{
    wake_state_t st = {0};

#if USE_USB
#if WAKE_USB_BACKEND_IDF || WAKE_USB_BACKEND_ARDUINO
    st.mounted = tud_mounted();
#else
    st.mounted = false;
#endif
#if WAKE_USB_BACKEND_IDF
    st.suspended = s_usb_suspended;
    st.remote_wakeup_allowed = s_usb_remote_wakeup_allowed;
#elif WAKE_USB_BACKEND_ARDUINO
    st.suspended = s_usb_suspended;
    st.remote_wakeup_allowed = s_usb_remote_wakeup_allowed;
#else
    st.suspended = false;
    st.remote_wakeup_allowed = false;
#endif
#elif USE_BLE
    {
        bool connected = wake_ble_is_connected();
        s_ble_connected = connected;
        st.mounted = connected;               /* connected */
        st.suspended = false;                 /* n/a for BLE */
        st.remote_wakeup_allowed = connected; /* can attempt send */
    }
#endif

    return st;
}

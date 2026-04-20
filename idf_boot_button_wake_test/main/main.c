#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wake_core.h"

static const char *TAG = "boot_wake_test";

#define BOOT_BUTTON_GPIO GPIO_NUM_0
#define BOOT_BUTTON_ACTIVE_LEVEL 0
#define BUTTON_POLL_MS 10
#define BUTTON_DEBOUNCE_MS 30
#define STATUS_LOG_MS 5000

static void boot_button_init(void)
{
    const gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    wake_usb_on_suspend(remote_wakeup_en);
}

void tud_resume_cb(void)
{
    wake_usb_on_resume();
}

void app_main(void)
{
    ESP_LOGI(TAG, "init start");
    ESP_ERROR_CHECK(wake_init());
    boot_button_init();

    ESP_LOGI(TAG, "ready: press BOOT(GPIO0) to attempt remote wakeup");

    bool pressed_latched = false;
    TickType_t press_started = 0;
    TickType_t last_status_log = xTaskGetTickCount();
    uint32_t press_count = 0;
    uint32_t wake_ok_count = 0;
    uint32_t wake_fail_count = 0;

    while (1) {
        const bool is_pressed = (gpio_get_level(BOOT_BUTTON_GPIO) == BOOT_BUTTON_ACTIVE_LEVEL);
        const TickType_t now = xTaskGetTickCount();

        if (is_pressed) {
            if (press_started == 0) {
                press_started = now;
            }

            if (!pressed_latched && (now - press_started) >= pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS)) {
                pressed_latched = true;
                press_count++;

                const wake_state_t st = wake_get_state();
                const uint32_t suspend_seq = wake_usb_get_suspend_seq();
                ESP_LOGI(TAG,
                         "BOOT pressed #%" PRIu32 " (mounted=%d suspended=%d remote_wakeup_allowed=%d seq=%" PRIu32 ")",
                         press_count,
                         st.mounted ? 1 : 0,
                         st.suspended ? 1 : 0,
                         st.remote_wakeup_allowed ? 1 : 0,
                         suspend_seq);

                const bool ok = wake_trigger();
                if (ok) {
                    wake_ok_count++;
                } else {
                    wake_fail_count++;
                }

                ESP_LOGI(TAG,
                         "wake result=%d (ok=%" PRIu32 " fail=%" PRIu32 ")",
                         ok ? 1 : 0,
                         wake_ok_count,
                         wake_fail_count);
            }
        } else {
            press_started = 0;
            pressed_latched = false;
        }

        if ((now - last_status_log) >= pdMS_TO_TICKS(STATUS_LOG_MS)) {
            const wake_state_t st = wake_get_state();
            ESP_LOGI(TAG,
                     "status: mounted=%d suspended=%d remote_wakeup_allowed=%d seq=%" PRIu32 " presses=%" PRIu32 " ok=%" PRIu32 " fail=%" PRIu32,
                     st.mounted ? 1 : 0,
                     st.suspended ? 1 : 0,
                     st.remote_wakeup_allowed ? 1 : 0,
                     wake_usb_get_suspend_seq(),
                     press_count,
                     wake_ok_count,
                     wake_fail_count);
            last_status_log = now;
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

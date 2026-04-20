#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifndef USE_USB
#define USE_USB 1
#endif

#ifndef USE_BLE
#define USE_BLE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool mounted;
    bool suspended;
    bool remote_wakeup_allowed;
} wake_state_t;

esp_err_t wake_init(void);
bool wake_trigger(void);

/* Mirror main.c USB state tracking hooks. */
void wake_usb_on_suspend(bool remote_wakeup_en);
void wake_usb_on_resume(void);
uint32_t wake_usb_get_suspend_seq(void);

/* Optional debug state. USB/BLE use transport-specific semantics. */
wake_state_t wake_get_state(void);

#if USE_BLE
/*
 * Optional BLE hook points.
 * Provide strong definitions in your BLE module to replace these stubs.
 */
esp_err_t wake_ble_init(void);
bool wake_ble_is_connected(void);
bool wake_ble_send_wake(void);
#endif

#ifdef __cplusplus
}
#endif

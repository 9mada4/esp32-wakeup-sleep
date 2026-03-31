#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool mounted;
    bool suspended;
    bool remote_wakeup_allowed;
} wake_state_t;

esp_err_t wake_init(void);
bool wake_is_ready(void);
bool wake_trigger(void);
wake_state_t wake_get_state(void);

#ifdef __cplusplus
}
#endif

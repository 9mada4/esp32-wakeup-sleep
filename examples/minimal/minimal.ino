#include <wake_core.h>

#include "esp_system.h"

static const char* resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_UNKNOWN:
      return "UNKNOWN";
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT_RESET";
    case ESP_RST_SW:
      return "SW_RESET";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    default:
      return "OTHER";
  }
}

void setup() {
  Serial.begin(115200);
  delay(200);

  esp_reset_reason_t reason = esp_reset_reason();
  Serial.printf("reset_reason=%s (%d)\n", resetReasonToString(reason), (int)reason);

  esp_err_t err = wake_init();
  if (err != ESP_OK) {
    Serial.printf("wake_init failed: %d\n", (int)err);
    return;
  }

  Serial.println("wake core ready");

  // Pressing RESET reboots the ESP32. Treat external reset as a wake trigger.
  if (reason == ESP_RST_EXT) {
    Serial.println("external reset detected -> sending wake trigger");
    bool ok = wake_trigger();
    Serial.printf("wake_trigger -> %d\n", ok ? 1 : 0);
  } else {
    Serial.println("press RESET button to send wake trigger once");
  }
}

void loop() {
  wake_state_t st = wake_get_state();

  Serial.printf("mounted=%d suspended=%d remote_wakeup_allowed=%d\n",
                st.mounted ? 1 : 0,
                st.suspended ? 1 : 0,
                st.remote_wakeup_allowed ? 1 : 0);
  delay(1000);
}

#include <wake_core.h>

#if __has_include("USB.h")
#include "USB.h"
#define WAKE_HAS_ARDUINO_USB_EVENTS 1
#else
#define WAKE_HAS_ARDUINO_USB_EVENTS 0
#endif

static const int kBootButtonPin = 0;  // ESP32-S3 BOOT button (active low)
static bool s_lastBootLevel = true;

#if WAKE_HAS_ARDUINO_USB_EVENTS
static void onArduinoUsbEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  (void)arg;
  (void)event_base;

  if (event_id == ARDUINO_USB_SUSPEND_EVENT) {
    bool remoteWakeupEn = false;
    if (event_data != nullptr) {
      auto* data = reinterpret_cast<arduino_usb_event_data_t*>(event_data);
      remoteWakeupEn = data->suspend.remote_wakeup_en;
    }
    wake_usb_on_suspend(remoteWakeupEn);
  } else if (event_id == ARDUINO_USB_RESUME_EVENT) {
    wake_usb_on_resume();
  }
}
#endif

void setup() {
  Serial.begin(115200);
  delay(200);

  esp_err_t err = wake_init();
  if (err != ESP_OK) {
    Serial.printf("wake_init failed: %d\n", (int)err);
    return;
  }

  pinMode(kBootButtonPin, INPUT_PULLUP);
  s_lastBootLevel = (digitalRead(kBootButtonPin) == HIGH);

#if WAKE_HAS_ARDUINO_USB_EVENTS
  USB.onEvent(ARDUINO_USB_SUSPEND_EVENT, onArduinoUsbEvent);
  USB.onEvent(ARDUINO_USB_RESUME_EVENT, onArduinoUsbEvent);
  Serial.println("arduino usb suspend/resume hook ready");
#endif

  Serial.println("wake core ready");
  Serial.println("press BOOT button to send wake trigger");
}

void loop() {
  bool bootPressed = (digitalRead(kBootButtonPin) == LOW);
  if (bootPressed && s_lastBootLevel) {
    delay(20);  // debounce
    if (digitalRead(kBootButtonPin) == LOW) {
      Serial.println("BOOT pressed -> sending wake trigger");
      bool ok = wake_trigger();
      Serial.printf("wake_trigger -> %d\n", ok ? 1 : 0);
    }
  }
  s_lastBootLevel = !bootPressed;

  wake_state_t st = wake_get_state();

  Serial.printf("mounted=%d suspended=%d remote_wakeup_allowed=%d\n",
                st.mounted ? 1 : 0,
                st.suspended ? 1 : 0,
                st.remote_wakeup_allowed ? 1 : 0);
  delay(200);
}

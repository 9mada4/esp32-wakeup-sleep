#include <wake_core.h>

static const int kBootButtonPin = 0;  // ESP32-S3 BOOT button (active low)
static bool s_lastBootLevel = true;

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

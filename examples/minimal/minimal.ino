#include <wake_core.h>

void setup() {
  Serial.begin(115200);

  esp_err_t err = wake_init();
  if (err != ESP_OK) {
    Serial.printf("wake_init failed: %d\n", (int)err);
    return;
  }

  Serial.println("wake core ready");
}

void loop() {
  wake_state_t st = wake_get_state();

  Serial.println("sending wake trigger");
  bool ok = wake_trigger();
  Serial.printf("wake_trigger -> %d\n", ok ? 1 : 0);
  delay(1000);

  Serial.printf("mounted=%d suspended=%d remote_wakeup_allowed=%d\n",
                st.mounted ? 1 : 0,
                st.suspended ? 1 : 0,
                st.remote_wakeup_allowed ? 1 : 0);
  delay(1000);
}

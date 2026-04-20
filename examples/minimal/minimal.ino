#include <wake_core.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <USB.h>
#include <USBHID.h>
#include <USBHIDMouse.h>
#define WAKE_HAS_ARDUINO_USB_HID 1
#else
#define WAKE_HAS_ARDUINO_USB_HID 0
#endif

#ifndef WAKE_USB_REMOTE_WAKE_ATTR
#define WAKE_USB_REMOTE_WAKE_ATTR 0x20u
#endif

#if defined(ARDUINO_ARCH_ESP32)
#define WAKE_LOG_SERIAL Serial0
#else
#define WAKE_LOG_SERIAL Serial
#endif

static const int kBootButtonPin = 0;  // ESP32-S3 BOOT button (active low)
static bool s_lastBootLevel = true;

#if WAKE_HAS_ARDUINO_USB_HID
static USBHID sUsbHid(HID_ITF_PROTOCOL_MOUSE);
static USBHIDMouse sUsbMouse;

static void onArduinoUsbEvent(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  (void)arg;
  if (event_base != ARDUINO_USB_EVENTS) {
    return;
  }
  if (event_id == ARDUINO_USB_SUSPEND_EVENT) {
    bool remoteWakeEnabled = false;
    if (event_data != nullptr) {
      remoteWakeEnabled = ((arduino_usb_event_data_t*)event_data)->suspend.remote_wakeup_en;
    }
    wake_usb_on_suspend(remoteWakeEnabled);
    return;
  }
  if (event_id == ARDUINO_USB_RESUME_EVENT) {
    wake_usb_on_resume();
  }
}

static void configureUsbForWake() {
  USB.onEvent(onArduinoUsbEvent);
  USB.usbClass(TUSB_CLASS_HID);
  USB.usbSubClass(HID_SUBCLASS_BOOT);
  USB.usbProtocol(HID_ITF_PROTOCOL_MOUSE);
  USB.usbAttributes((uint8_t)WAKE_USB_REMOTE_WAKE_ATTR);
  USB.usbPower(100);
  USB.productName("ESP32WakeCore");

  (void)sUsbHid;
  sUsbMouse.begin();
  USB.begin();
}
#endif

// ---------------------------------------------------------------------------
// Put your application code here.
// ---------------------------------------------------------------------------
static void appSetup() {
  // Example:
  // pinMode(2, OUTPUT);
}

static void appLoop() {
  // Example:
  // digitalWrite(2, millis() % 1000 < 100 ? HIGH : LOW);
}

static void appOnWakeButton(bool wakeSent) {
  (void)wakeSent;
  // Example:
  // if (wakeSent) { /* send your own signal here */ }
}

void setup() {
  WAKE_LOG_SERIAL.begin(115200);
  delay(200);
  WAKE_LOG_SERIAL.println("wake minimal build tag: 2026-04-21-app-min");
  WAKE_LOG_SERIAL.printf("WAKE_HAS_ARDUINO_USB_HID=%d\n", WAKE_HAS_ARDUINO_USB_HID ? 1 : 0);

#if WAKE_HAS_ARDUINO_USB_HID
  configureUsbForWake();
#endif

  esp_err_t err = wake_init();
  if (err != ESP_OK) {
    WAKE_LOG_SERIAL.printf("wake_init failed: %d\n", (int)err);
    return;
  }

  pinMode(kBootButtonPin, INPUT_PULLUP);
  s_lastBootLevel = (digitalRead(kBootButtonPin) == HIGH);

  WAKE_LOG_SERIAL.println("wake core ready");
  WAKE_LOG_SERIAL.println("press BOOT to request remote wake");

  appSetup();
}

void loop() {
  bool bootPressed = (digitalRead(kBootButtonPin) == LOW);
  if (bootPressed && s_lastBootLevel) {
    delay(20);  // debounce
    if (digitalRead(kBootButtonPin) == LOW) {
      bool wakeSent = wake_trigger();
      WAKE_LOG_SERIAL.printf("wake_trigger -> %d\n", wakeSent ? 1 : 0);
      appOnWakeButton(wakeSent);
    }
  }
  s_lastBootLevel = !bootPressed;

  appLoop();
  delay(10);
}

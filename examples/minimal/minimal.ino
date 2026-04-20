#include <wake_core.h>
#include <Preferences.h>
#include <string.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <USB.h>
#include <USBHIDMouse.h>
#define WAKE_HAS_ARDUINO_USB_HID 1
#else
#define WAKE_HAS_ARDUINO_USB_HID 0
#endif
#if __has_include("tusb.h")
#include "tusb.h"
#endif

#if defined(ARDUINO_ARCH_ESP32)
#define WAKE_LOG_SERIAL Serial0
#else
#define WAKE_LOG_SERIAL Serial
#endif

#if WAKE_HAS_ARDUINO_USB_HID
static USBHIDMouse sUsbMouse;
#endif

static const int kBootButtonPin = 0;  // ESP32-S3 BOOT button (active low)
static bool s_lastBootLevel = true;
static uint32_t s_lastPrintedSuspendSeq = 0;
static uint32_t s_lastStatePrintMs = 0;
static uint32_t s_lastSerialRxMs = 0;
static bool s_prevSuspended = false;
static char s_serialLine[64];
static size_t s_serialLineLen = 0;

Preferences sPrefs;
static bool sPrefsReady = false;

typedef struct {
  uint32_t bootPressCount;
  uint32_t wakeSuccessCount;
  uint32_t wakeFailCount;
  uint32_t suspendSeqAtLastPress;
  uint32_t suspendEventCount;
  uint32_t resumeEventCount;
  uint8_t lastWakeResult;  // 0=fail,1=success,255=none
} WakeLog;

static WakeLog sLog = {
  0, 0, 0, 0, 0, 0, 255
};

static void saveWakeLog() {
  if (!sPrefsReady) {
    return;
  }
  sPrefs.putUInt("boot_cnt", sLog.bootPressCount);
  sPrefs.putUInt("wake_ok", sLog.wakeSuccessCount);
  sPrefs.putUInt("wake_ng", sLog.wakeFailCount);
  sPrefs.putUInt("last_seq", sLog.suspendSeqAtLastPress);
  sPrefs.putUInt("susp_evt", sLog.suspendEventCount);
  sPrefs.putUInt("resm_evt", sLog.resumeEventCount);
  sPrefs.putUChar("last_res", sLog.lastWakeResult);
}

static void loadWakeLog() {
  if (!sPrefsReady) {
    return;
  }
  sLog.bootPressCount = sPrefs.getUInt("boot_cnt", 0);
  sLog.wakeSuccessCount = sPrefs.getUInt("wake_ok", 0);
  sLog.wakeFailCount = sPrefs.getUInt("wake_ng", 0);
  sLog.suspendSeqAtLastPress = sPrefs.getUInt("last_seq", 0);
  sLog.suspendEventCount = sPrefs.getUInt("susp_evt", 0);
  sLog.resumeEventCount = sPrefs.getUInt("resm_evt", 0);
  sLog.lastWakeResult = sPrefs.getUChar("last_res", 255);
}

static void clearWakeLog() {
  sLog.bootPressCount = 0;
  sLog.wakeSuccessCount = 0;
  sLog.wakeFailCount = 0;
  sLog.suspendSeqAtLastPress = 0;
  sLog.suspendEventCount = 0;
  sLog.resumeEventCount = 0;
  sLog.lastWakeResult = 255;
  saveWakeLog();
}

static void printWakeLog(const char* prefix) {
  wake_state_t st = wake_get_state();
  uint32_t suspendSeqNow = wake_usb_get_suspend_seq();

  WAKE_LOG_SERIAL.printf("[%s] boot_press=%lu wake_ok=%lu wake_fail=%lu last_result=%d last_press_suspend_seq=%lu\n",
                         prefix ? prefix : "status",
                         (unsigned long)sLog.bootPressCount,
                         (unsigned long)sLog.wakeSuccessCount,
                         (unsigned long)sLog.wakeFailCount,
                         (int)sLog.lastWakeResult,
                         (unsigned long)sLog.suspendSeqAtLastPress);
  WAKE_LOG_SERIAL.printf("[%s] suspend_events=%lu resume_events=%lu suspend_seq_now=%lu mounted=%d suspended=%d remote_wakeup_allowed=%d\n",
                         prefix ? prefix : "status",
                         (unsigned long)sLog.suspendEventCount,
                         (unsigned long)sLog.resumeEventCount,
                         (unsigned long)suspendSeqNow,
                         st.mounted ? 1 : 0,
                         st.suspended ? 1 : 0,
                         st.remote_wakeup_allowed ? 1 : 0);
}

static void handleSerialCommand(const char* line) {
  if (line == nullptr || line[0] == '\0') {
    printWakeLog("status");
    return;
  }

  if (strcmp(line, "clear") == 0) {
    clearWakeLog();
    WAKE_LOG_SERIAL.println("[status] wake log cleared");
    printWakeLog("status");
    return;
  }

  if (strcmp(line, "help") == 0) {
    WAKE_LOG_SERIAL.println("commands: status | clear | help");
    printWakeLog("status");
    return;
  }

  // Any other input also shows status to match "type something and check result".
  printWakeLog("status");
}

static void pollSerialCommands() {
  while (WAKE_LOG_SERIAL.available() > 0) {
    int v = WAKE_LOG_SERIAL.read();
    if (v < 0) {
      break;
    }
    char c = (char)v;
    if (c == '\r' || c == '\n') {
      if (s_serialLineLen > 0) {
        s_serialLine[s_serialLineLen] = '\0';
        handleSerialCommand(s_serialLine);
        s_serialLineLen = 0;
      } else {
        handleSerialCommand("");
      }
      continue;
    }
    if (s_serialLineLen + 1 < sizeof(s_serialLine)) {
      s_serialLine[s_serialLineLen++] = c;
      s_lastSerialRxMs = millis();
    }
  }

  // Handle "No line ending" mode in Arduino Serial Monitor.
  if (s_serialLineLen > 0 && (millis() - s_lastSerialRxMs) > 300) {
    s_serialLine[s_serialLineLen] = '\0';
    handleSerialCommand(s_serialLine);
    s_serialLineLen = 0;
  }
}

void setup() {
  WAKE_LOG_SERIAL.begin(115200);
  delay(200);
  WAKE_LOG_SERIAL.println("wake minimal build tag: 2026-04-21-hid-seq-a");
  WAKE_LOG_SERIAL.printf("WAKE_HAS_ARDUINO_USB_HID=%d\n", WAKE_HAS_ARDUINO_USB_HID ? 1 : 0);
#if defined(ARDUINO_USB_MODE)
  WAKE_LOG_SERIAL.printf("ARDUINO_USB_MODE=%d\n", ARDUINO_USB_MODE);
#endif
#if defined(CONFIG_TINYUSB_ENABLED)
  WAKE_LOG_SERIAL.printf("CONFIG_TINYUSB_ENABLED=%d\n", CONFIG_TINYUSB_ENABLED ? 1 : 0);
#endif
#if defined(CONFIG_TINYUSB_HID_ENABLED)
  WAKE_LOG_SERIAL.printf("CONFIG_TINYUSB_HID_ENABLED=%d\n", CONFIG_TINYUSB_HID_ENABLED ? 1 : 0);
#endif

#if WAKE_HAS_ARDUINO_USB_HID
  sUsbMouse.begin();
#if defined(TUSB_DESC_CONFIG_ATT_SELF_POWERED) && defined(TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP)
  USB.usbAttributes((uint8_t)(TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP));
#endif
  USB.begin();
  WAKE_LOG_SERIAL.println("arduino USB HID begin done");
#else
  WAKE_LOG_SERIAL.println("arduino USB HID begin skipped");
#endif

  esp_err_t err = wake_init();
  if (err != ESP_OK) {
    WAKE_LOG_SERIAL.printf("wake_init failed: %d\n", (int)err);
    return;
  }

  sPrefsReady = sPrefs.begin("wakecore_log", false);
  if (!sPrefsReady) {
    WAKE_LOG_SERIAL.println("warning: failed to open NVS log namespace");
  } else {
    loadWakeLog();
  }

  pinMode(kBootButtonPin, INPUT_PULLUP);
  s_lastBootLevel = (digitalRead(kBootButtonPin) == HIGH);
  s_lastPrintedSuspendSeq = wake_usb_get_suspend_seq();
  s_prevSuspended = wake_get_state().suspended;

  WAKE_LOG_SERIAL.println("wake core ready");
  WAKE_LOG_SERIAL.println("press BOOT button to send wake trigger");
  WAKE_LOG_SERIAL.println("type anything in Serial Monitor to show persisted wake log");
  WAKE_LOG_SERIAL.println("commands: status | clear | help");
  printWakeLog("boot");
}

void loop() {
  wake_state_t st = wake_get_state();
  uint32_t suspendSeqNow = wake_usb_get_suspend_seq();

  if (suspendSeqNow != s_lastPrintedSuspendSeq) {
    uint32_t delta = suspendSeqNow - s_lastPrintedSuspendSeq;
    sLog.suspendEventCount += delta;
    s_lastPrintedSuspendSeq = suspendSeqNow;
    saveWakeLog();
    printWakeLog("usb-suspend");
  }

  if (s_prevSuspended && !st.suspended) {
    sLog.resumeEventCount++;
    saveWakeLog();
    printWakeLog("usb-resume");
  }
  s_prevSuspended = st.suspended;

  bool bootPressed = (digitalRead(kBootButtonPin) == LOW);
  if (bootPressed && s_lastBootLevel) {
    delay(20);  // debounce
    if (digitalRead(kBootButtonPin) == LOW) {
      WAKE_LOG_SERIAL.println("BOOT pressed -> sending wake trigger");
      bool ok = wake_trigger();
      WAKE_LOG_SERIAL.printf("wake_trigger -> %d\n", ok ? 1 : 0);

      sLog.bootPressCount++;
      if (ok) {
        sLog.wakeSuccessCount++;
        sLog.lastWakeResult = 1;
      } else {
        sLog.wakeFailCount++;
        sLog.lastWakeResult = 0;
      }
      sLog.suspendSeqAtLastPress = wake_usb_get_suspend_seq();
      saveWakeLog();
      printWakeLog("event");
    }
  }
  s_lastBootLevel = !bootPressed;

  pollSerialCommands();

  uint32_t now = millis();
  if (now - s_lastStatePrintMs >= 2000) {
    s_lastStatePrintMs = now;
    WAKE_LOG_SERIAL.printf("live: mounted=%d suspended=%d remote_wakeup_allowed=%d suspend_seq=%lu\n",
                           st.mounted ? 1 : 0,
                           st.suspended ? 1 : 0,
                           st.remote_wakeup_allowed ? 1 : 0,
                           (unsigned long)suspendSeqNow);
  }

  delay(20);
}

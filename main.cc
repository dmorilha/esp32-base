#include "time.h"
#include "esp_sntp.h"

#include "wifi.h" 

#include "logger.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_task_wdt.h"
#include "soc/rtc.h"
#include "Arduino.h"
#if (ARDUINO_USB_CDC_ON_BOOT | ARDUINO_USB_MSC_ON_BOOT | ARDUINO_USB_DFU_ON_BOOT) && !ARDUINO_USB_MODE
#include "USB.h"
#if ARDUINO_USB_MSC_ON_BOOT
#include "FirmwareMSC.h"
#endif
#endif

#include "chip-debug-report.h"

#ifndef ARDUINO_LOOP_STACK_SIZE
#ifndef CONFIG_ARDUINO_LOOP_STACK_SIZE
#define ARDUINO_LOOP_STACK_SIZE 8192
#else
#define ARDUINO_LOOP_STACK_SIZE CONFIG_ARDUINO_LOOP_STACK_SIZE
#endif
#endif

#if CONFIG_AUTOSTART_ARDUINO
#if CONFIG_FREERTOS_UNICORE
void yieldIfNecessary(void) {
  static uint64_t lastYield = 0;
  uint64_t now = millis();
  if ((now - lastYield) > 2000) {
    lastYield = now;
    vTaskDelay(5);  //delay 1 RTOS tick
  }
}
#endif

bool loopTaskWDTEnabled;

__attribute__((weak)) size_t getArduinoLoopTaskStackSize(void) {
  return ARDUINO_LOOP_STACK_SIZE;
}

__attribute__((weak)) bool shouldPrintChipDebugReport(void) {
  return false;
}

// this function can be changed by the sketch using the macro SET_TIME_BEFORE_STARTING_SKETCH_MS(time_ms)
__attribute__((weak)) uint64_t getArduinoSetupWaitTime_ms(void) {
  return 0;
}

TaskHandle_t logger_task_handle = nullptr;
void logger_task(void *) {
  Logger * logger = Logger::get_instance();
  while (true) {
    const bool result = logger->persist();
    struct tm t;
    t.tm_year = 0;
    if (getLocalTime(&t, 1'000)) {
      Serial.printf("%d-%02d-%02d %02d:%02d:%02d: logger::persist %s.\n",
          t.tm_year + 1'900, t.tm_mon + 1, t.tm_mday,
          t.tm_hour, t.tm_min, t.tm_sec,
          (result ? "worked" : "failed"));
    }
    delay(5 * 60 * 1'000 /* milliseconds */); /* 5 minutes delay */
  }
}

TaskHandle_t ntp_task_handle = nullptr;
void ntp_task(void *) {
  size_t synchronized_counter = 0;
  while (true) {
    bool synchronize = 0 == synchronized_counter;

    struct tm t;
    t.tm_year = 0;
    if (getLocalTime(&t, 1'000)) {
      Serial.printf("Now is %d-%02d-%02d %02d:%02d:%02d\n",
          t.tm_year + 1'900, t.tm_mon + 1, t.tm_mday,
          t.tm_hour, t.tm_min, t.tm_sec);
    } else {
      synchronize = true;
    }

    if (synchronize) {
      /* hide your WIFI_SSD, WIFI_PASSPHRASE as define macros in a separate header */
      WiFi.begin(WIFI_SSD, WIFI_PASSPHRASE);
      for (size_t counter = 20; 0 < counter; counter -= 1) {
        if (WiFi.status() != WL_CONNECTED) {
          delay(500 /* milliseconds */);
          continue;
        }

        Serial.println("Connected to " WIFI_SSD);
        configTime(3'600, 3'600, "pool.ntp.org", "time.nist.gov", nullptr);
        delay(2'000 /* milliseconds */);

        {
          if (getLocalTime(&t, 5'000)) {
            synchronized_counter = 12;
            Serial.printf("Now is %d-%02d-%02d %02d:%02d:%02d\n",
                t.tm_year + 1'900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
          }
        }
        WiFi.disconnectAsync();
      }
    }

    /* 5 minutes delay */
    delay(5 * 60 * 1'000 /* milliseconds */);
    synchronized_counter -= 0 < synchronized_counter ? 1 : 0;
  }
}

TaskHandle_t main_task_handle = nullptr;
void main_task(void *) {
  delay(getArduinoSetupWaitTime_ms());
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
  printBeforeSetupInfo();
#else
  if (shouldPrintChipDebugReport()) {
    printBeforeSetupInfo();
  }
#endif
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_SERIAL)
  // sets UART0 (default console) RX/TX pins as already configured in boot or as defined in variants/pins_arduino.h
  Serial0.setPins(gpioNumberToDigitalPin(SOC_RX0), gpioNumberToDigitalPin(SOC_TX0));
  // time in ms that the sketch may wait before starting its execution - default is zero
  // usually done for opening the Serial Monitor and seeing all debug messages
#endif
  setup();
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_DEBUG
  printAfterSetupInfo();
#else
  if (shouldPrintChipDebugReport()) {
    printAfterSetupInfo();
  }
#endif

  /* the real application loop comes here */

  /*
   * You can log messages calling
   * `Logger::get_instance()->log_with_time("my message\n");`
   **/
}

extern "C" void app_main() {
#ifdef F_CPU
  printf("%s F_CPU: %lu\n", __func__,  F_CPU);
  setCpuFrequencyMhz(F_CPU / 1'000'000);
#endif
#if ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE
  Serial.begin();
#endif
#if ARDUINO_USB_MSC_ON_BOOT && !ARDUINO_USB_MODE
  MSC_Update.begin();
#endif
#if ARDUINO_USB_DFU_ON_BOOT && !ARDUINO_USB_MODE
  USB.enableDFU();
#endif
#if ARDUINO_USB_ON_BOOT && !ARDUINO_USB_MODE
  USB.begin();
#endif
  loopTaskWDTEnabled = false;

  initArduino();

  /* ntp */ 
  xTaskCreateUniversal(ntp_task, "ntp_task", getArduinoLoopTaskStackSize(),
      nullptr, 1, &ntp_task_handle, ARDUINO_RUNNING_CORE);

  /* wait a seconds here to give it a chance for the ntp to kick in */
  delay(1'000 /* milliseconds */);

  /* logger */
  xTaskCreateUniversal(logger_task, "logger_task", getArduinoLoopTaskStackSize(),
      nullptr, 1, &logger_task_handle, ARDUINO_RUNNING_CORE);

  /* wait a seconds here to give it a chance for the logger to kick in */
  delay(1'000 /* milliseconds */);

  /* application */
#if 1
  xTaskCreateUniversal(main_task, "main_task", getArduinoLoopTaskStackSize(),
      nullptr, 1, &main_task_handle, ARDUINO_RUNNING_CORE);
#else
  main_task(nullptr);
#endif
}

#endif

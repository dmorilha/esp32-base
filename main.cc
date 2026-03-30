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
  if ((now - lastYield) > 2'000) {
    lastYield = now;
    vTaskDelay(5);  //delay 1 RTOS tick
  }
}
#endif

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
  Logger * const logger = Logger::get_instance();
  while (true) {
    delay(5 * 60 * 1'000 /* milliseconds */); /* 5 minutes delay */
    const bool result = logger->persist();
    struct tm t;
    t.tm_year = 0;
    if (getLocalTime(&t, 1'000)) {
      Serial.printf("%d-%02d-%02d %02d:%02d:%02d: Logger::persist %s.\n",
          t.tm_year + 1'900, t.tm_mon + 1, t.tm_mday,
          t.tm_hour, t.tm_min, t.tm_sec,
          (result ? "worked" : "failed"));
    } else {
      Serial.printf("(no time): Logger::persist %s.\n",
          (result ? "worked" : "failed"));
    }
  }
}

TaskHandle_t ntp_task_handle = nullptr;
void ntp_task(void *) {
  Logger * const logger = Logger::get_instance();
  size_t synchronized_counter = 0;
  while (true) {
    bool synchronize = 0 == synchronized_counter;

    struct tm t;
    t.tm_year = 0;
    if (getLocalTime(&t, 100)) {
      Serial.printf("Now is %d-%02d-%02d %02d:%02d:%02d\n",
          t.tm_year + 1'900, t.tm_mon + 1, t.tm_mday,
          t.tm_hour, t.tm_min, t.tm_sec);
    } else {
      synchronize = true;
    }

    if (synchronize) {
      /*
       * Hide your WIFI_SSD and WIFI_PASSPHRASE as define
       * macros in a separate header file
       **/
      WiFi.begin(WIFI_SSD, WIFI_PASSPHRASE);
      size_t counter;
      for (counter = 20; 0 < counter; --counter) {
        if (WiFi.status() != WL_CONNECTED) {
          delay(500 /* milliseconds */);
          continue;
        }
        logger->log_with_time("Connected to " WIFI_SSD ".\n");
        configTzTime("BRT+3", "pool.ntp.org", "time.nist.gov", nullptr);
        if (getLocalTime(&t, 3'000)) {
          synchronized_counter = 12;
          logger->log_with_time("Time updated.\n");
        }
        WiFi.disconnectAsync();
        break;
      }
      if (0 == counter) {
        logger->log_with_time("Time update failed.\n");
      }
    }

    /* 5 minutes delay */
    delay(5 * 60 * 1'000 /* milliseconds */);
    synchronized_counter -= 0 < synchronized_counter ? 1 : 0;
  }
}

TaskHandle_t main_task_handle = nullptr;
void main_task(void *) {
  Logger * const logger = Logger::get_instance();

  while (true) {
    /* the real application loop comes here */

    delay(5 * 60 * 1'000 /* milliseconds */);

    /*
     * You can log messages calling
     * `logger->log_with_time("my message\n");`
     **/

    if (serialEventRun) {
      serialEventRun();
    }
  }
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

  initArduino();

  if (shouldPrintChipDebugReport()) {
    printBeforeSetupInfo();
  }

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_SERIAL)
  // sets UART0 (default console) RX/TX pins as already configured in boot or as defined in variants/pins_arduino.h
  Serial0.setPins(gpioNumberToDigitalPin(SOC_RX0), gpioNumberToDigitalPin(SOC_TX0));
  // time in ms that the sketch may wait before starting its execution - default is zero
  // usually done for opening the Serial Monitor and seeing all debug messages
#endif
  setup();

  if (shouldPrintChipDebugReport()) {
    printAfterSetupInfo();
  }

  /* instantiates logger */
  Logger::get_instance();

  /* ntp */ 
  xTaskCreateUniversal(ntp_task, "ntp_task", getArduinoLoopTaskStackSize(),
      nullptr, 1, &ntp_task_handle, ARDUINO_RUNNING_CORE);

  /* wait three seconds here to give it a chance for the ntp to kick in */
  delay(3'000 /* milliseconds */);

  /* logger */
  xTaskCreateUniversal(logger_task, "logger_task", getArduinoLoopTaskStackSize(),
      nullptr, 1, &logger_task_handle, ARDUINO_RUNNING_CORE);

  /* application */
#if 1
  xTaskCreateUniversal(main_task, "main_task", getArduinoLoopTaskStackSize(),
      nullptr, 1, &main_task_handle, ARDUINO_RUNNING_CORE);
#else
  main_task(nullptr);
#endif
}

#endif

/* Copyright 2026 - Daniel Morilha */

#include <FS.h>
#include <SD_MMC.h>

#include "logger.h"

Logger::~Logger() {
  delete [] buffer_;
}

Logger::Logger() : buffer_(new char[SIZE]),
  head_(buffer_),
  tail_(buffer_) { }

void Logger::log(char * message) {
  const size_t message_length = strlen(message);
  char * local_tail = tail_;
  char * new_tail;

  do {
    new_tail = local_tail + message_length;
    if (buffer_ + SIZE <= new_tail) {
      new_tail = buffer_ + (new_tail - (buffer_ + SIZE));
    }
  } while( ! std::atomic_compare_exchange_strong(&tail_, &local_tail, new_tail));

  for (size_t i = 0; message_length > i; ++i) {
    *local_tail++ = message[i];
    if (SIZE == local_tail - buffer_) {
      local_tail = buffer_;
    }
  }
}

void Logger::log_with_time(char * in) {
  char message[256];
  struct tm t;
  t.tm_year = 0;
  if (getLocalTime(&t, 100)) {
    snprintf(message, 256, "%03d-%02d:%02d:%02d: %s",
        t.tm_yday, t.tm_hour, t.tm_min, t.tm_sec, in);
  } else {
    snprintf(message, 256, "(no time): %s", in);
  }
  log(message);
}

bool Logger::persist() {
  bool result = false;
  if (SD_MMC.begin("/sdcard", true)) {
    for (size_t i = 0; 3 > i; ++i) {
      File file = SD_MMC.open("/log.txt", FILE_APPEND);
      if ( ! file) {
        Serial.println("Failed to open file for appending");
        delay(10 /* milliseconds */);
        continue;
      }
      char * const local_tail = tail_;
      if (head_ < local_tail) {
        result = file.write(reinterpret_cast<uint8_t*>(head_), local_tail - head_);
        head_ = local_tail;
      } else if (head_ > local_tail) {
        result = file.write(reinterpret_cast<uint8_t*>(head_), buffer_ + SIZE - head_);
        if (buffer_ < local_tail) {
          result &= file.write(reinterpret_cast<uint8_t*>(buffer_), local_tail - buffer_);
        }
        head_ = local_tail;
      }
      file.close();
      break;
    }
  }
  return result;
}

Logger * Logger::get_instance() {
  if (nullptr == instance_) {
    Logger * expected = nullptr;
    if (std::atomic_compare_exchange_strong(&instance_, &expected, new Logger())) {
      instance_.load()->log_with_time("Logger instantiated.\n");
    }
  }
  return instance_;
}

/* static initialization */
size_t Logger::SIZE = 10240;
std::atomic<Logger *> Logger::instance_ = nullptr;

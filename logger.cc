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
  char * tail = tail_;
  while ('\0' != *message) {
    *tail++ = *message++;
    if (SIZE == tail - buffer_) {
      tail = buffer_;
    }
  }
  tail_ = tail;
}

void Logger::log_with_time(char * in) {
  char message[256];
  struct tm t;
  t.tm_year = 0;
  if (getLocalTime(&t, 100)) {
    snprintf(message, 256, "%03d-%02d-%02d-%02d: %s",
        t.tm_yday, t.tm_hour, t.tm_min, t.tm_sec, in);
  } else {
    snprintf(message, 256, "(no time): %s", in);
  }
  log(message);
  Serial.printf(message);
}

bool Logger::persist() {
  bool result = false;
  if (SD_MMC.begin("/sdcard", true)) {
    File file = SD_MMC.open("/esp32.log", FILE_APPEND);
    if ( ! file) {
      Serial.println("Failed to open file for appending");
      return false;
    }
    char * const tail = tail_;
    if (head_ < tail) {
      result = file.write(reinterpret_cast<uint8_t*>(head_), tail - head_);
      head_ = tail;
    } else if (head_ > tail) {
      result = file.write(reinterpret_cast<uint8_t*>(head_), buffer_ + SIZE - head_);
      result &= file.write(reinterpret_cast<uint8_t*>(buffer_), tail - buffer_);
      head_ = tail;
    }
    file.close();
  }
  return result;
}

Logger * Logger::get_instance() {
  if (nullptr == instance_) {
    instance_ = new Logger();
    instance_->log_with_time("Logger instantiated\n");
  }
  return instance_;
}

/* static initialization */
size_t Logger::SIZE = 10240;
Logger * Logger::instance_ = nullptr;

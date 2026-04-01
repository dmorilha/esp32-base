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

  if ( ! static_cast<bool>(log_file_)) {
    Serial.print(message);
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
  if (open_log_file()) {
    char * const local_tail = tail_;
    if (head_ < local_tail) {
      result = log_file_.write(reinterpret_cast<uint8_t*>(head_), local_tail - head_);
      head_ = local_tail;
    } else if (head_ > local_tail) {
      result = log_file_.write(reinterpret_cast<uint8_t*>(head_), buffer_ + SIZE - head_);
      if (buffer_ < local_tail) {
        result &= log_file_.write(reinterpret_cast<uint8_t*>(buffer_), local_tail - buffer_);
      }
      head_ = local_tail;
    }
    if ( ! result) {
      log_file_.close();
    }
  }
  return result;
}

bool Logger::open_log_file() {
  if ( ! static_cast<bool>(log_file_)) {
    if (SD_MMC.begin("/sdcard", true)) {
      for (size_t i = 0; 3 > i; ++i) {
        if (static_cast<bool>(log_file_ = SD_MMC.open("/log.txt", FILE_APPEND))) {
          break;
        }
        delay(10 /* milliseconds */);
      }
    }
    if ( ! static_cast<bool>(log_file_)) {
      Serial.println("Failed to open log file for appending");
    }
  }
  return static_cast<bool>(log_file_);
}

Logger * Logger::get_instance() {
  if (nullptr == instance_) {
    Logger * local_instance = nullptr;
    if (std::atomic_compare_exchange_strong(&instance_, &local_instance,
          new Logger())) {
      local_instance = instance_.load();
      local_instance->open_log_file();
      local_instance->log_with_time("Logger instantiated.\n");
    }
  }
  return instance_;
}

/* static initialization */
size_t Logger::SIZE = 10240;
std::atomic<Logger *> Logger::instance_ = nullptr;

/* Copyright 2026 - Daniel Morilha */

/*
 * This simple logger class is designed to accumulate messages into
 * a 10k buffer and persist them into a SD card every 5 minutes.
 *
 * It allows multiple tasks to produce messages, with a separate
 * task persisting them.
 */
#pragma once

#include <atomic>

#include <FS.h>

struct Logger;

struct Logger {
  ~Logger();

  bool open_log_file();
  bool persist();
  void log(char *);
  void log_with_time(char *);

  static Logger * get_instance();

private:
  Logger();

  char * buffer_;
  char * head_ = nullptr;
  std::atomic<char *> tail_ = nullptr;
  File log_file_;;

  static size_t SIZE;
  static std::atomic<Logger *> instance_;
};

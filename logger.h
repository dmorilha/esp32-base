/* Copyright 2026 - Daniel Morilha */

/*
 * This simple logger class is designed to accumulate messages into
 * a 10k buffer and persist them into a SD card every 5 minutes.
 *
 * It is supposed to have just one thread producing messages,
 * with another thread persisting them.
 */
#pragma once

struct Logger;

struct Logger {
  ~Logger();
  bool persist(void);
  void log(char *);
  void log_with_time(char *);

  static Logger * get_instance();

private:
  Logger();

  char * buffer_;
  char * head_ = nullptr;
  char * tail_ = nullptr;

  static size_t SIZE;
  static Logger * instance_;
};

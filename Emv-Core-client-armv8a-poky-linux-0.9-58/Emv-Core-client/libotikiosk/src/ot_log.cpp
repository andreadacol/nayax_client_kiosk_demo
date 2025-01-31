#include "ot_log.h"

#include <map>
#include <string>
#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/time.h>

extern "C" {

using namespace std;

static map<string, e_OT_LOG_LEVEL> log_levels;
static e_OT_LOG_LEVEL default_level = e_OT_LOG_LEVEL_INFO;
static bool direct_logs = true;

void oT_Log_Set_Global_Level(e_OT_LOG_LEVEL level) {
  default_level = level;

  map<string, e_OT_LOG_LEVEL>::iterator it;
  for (it = log_levels.begin(); it != log_levels.end(); it++)
    it->second = level;
}

void oT_Log_Set_Module_Level(const char *log_module, e_OT_LOG_LEVEL level) {
  log_levels[log_module] = level;
}

e_OT_LOG_LEVEL oT_Log_Get_Module_Level(const char *log_module) {
  if (log_levels.find(log_module) == log_levels.end())
    log_levels[log_module] = default_level;

  return log_levels[log_module];
}

void oT_Log_Write_Format_v(e_OT_LOG_LEVEL level, const char *log_module, const char *format, va_list args) {
  // don't do anything if log is disabled
  if (level <= 0 || level > oT_Log_Get_Module_Level(log_module))
    return;

  // convert internal log level to syslog level
  int syslog_level = 0;
  switch (level) {
  case 1:
    syslog_level = LOG_ERR;
    break;
  case 2:
    syslog_level = LOG_WARNING;
    break;
  case 3:
    syslog_level = LOG_NOTICE;
    break;
  case 4:
    syslog_level = LOG_INFO;
    break;
  case 5:
  default:
    syslog_level = LOG_DEBUG;
  }

  char *s_buff = (char*) malloc(1024);
  int len = 0;

  struct timeval now;

  if (syslog_level == LOG_DEBUG) {
    gettimeofday(&now, nullptr);
    len = snprintf(s_buff, 1024, "[%06lu]", now.tv_usec);
  }

  //va_start(args, format);
  vsnprintf(s_buff + len, 1024 - len, format, args);

  syslog(LOG_MAKEPRI(LOG_USER, syslog_level), "%s", s_buff);
  //	va_end(args);
  if (direct_logs)
    printf("%s", s_buff);
  free(s_buff);

}

void oT_Log_Write_Format(e_OT_LOG_LEVEL level, const char *log_module, const char *format, ...) {
  va_list args;
  va_start(args, format);
  oT_Log_Write_Format_v(level, log_module, format, args);
  va_end(args);
}

void oT_Log_Write_Hex_Buf(e_OT_LOG_LEVEL level, const char *log_module, unsigned char *buf, unsigned int len, const char *format, ...) {
  if (level <= 0 || level > oT_Log_Get_Module_Level(log_module))
    return;

  // first log the regular message
  va_list args;
  va_start(args, format);
  oT_Log_Write_Format_v(level, log_module, format, args);
  va_end(args);

  // now log the buffer
  uint s_len = len * 3;
  char *s_buf = (char*) malloc(s_len + 1);
  for (uint i = 0; i < len; i++)
    sprintf(s_buf + 3 * i, "%02X ", buf[i]);
  oT_Log_Write_Format(level, log_module, "  %s\n", s_buf);
  free(s_buf);
}

void oT_Log_Write_9bit_Hex_Buf(e_OT_LOG_LEVEL level, const char *log_module, unsigned char *buf, unsigned int len, const char *format, ...) {
  if (level <= 0 || level > oT_Log_Get_Module_Level(log_module) || (len % 2) != 0)
    return;

  // first log the regular message
  va_list args;
  va_start(args, format);
  oT_Log_Write_Format_v(level, log_module, format, args);
  va_end(args);

  // now log the buffer
  uint s_len = (len / 2) * 3;
  char *s_buf = (char*) malloc(s_len + 1);
  for (uint i = 0; i < (len / 2); i++) {
    if (buf[2 * i] == 1)
      sprintf(s_buf + 3 * i, "%02X'", buf[2 * i + 1]);
    else
      sprintf(s_buf + 3 * i, "%02X ", buf[2 * i + 1]);
  }
  oT_Log_Write_Format(level, log_module, "  %s\n", s_buf);
  free(s_buf);
}

}

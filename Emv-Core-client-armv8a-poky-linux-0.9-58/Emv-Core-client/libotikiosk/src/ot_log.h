/*
 * ot_log.h
 */

#ifndef INCLUDE_OT_LOG_H_
#define INCLUDE_OT_LOG_H_

#ifdef __cplusplus
extern "C" {
#endif
  typedef enum {
    e_OT_LOG_LEVEL_ERROR = 1,
    e_OT_LOG_LEVEL_INFO = 3,
    e_OT_LOG_LEVEL_DEBUG = 5,
    e_OT_LOG_LEVEL_DEEP_DEBUG,
  } e_OT_LOG_LEVEL;

  void oT_Log_Write_Format(e_OT_LOG_LEVEL level, const char* log_module, const char* format, ...);
  void oT_Log_Write_Hex_Buf(e_OT_LOG_LEVEL level, const char* log_module, unsigned char* buf, unsigned int len, const char* format, ...);
  void oT_Log_Write_9bit_Hex_Buf(e_OT_LOG_LEVEL level, const char* log_module, unsigned char* buf, unsigned int len, const char* format, ...);
  void oT_Log_Set_Global_Level(e_OT_LOG_LEVEL level);
  void oT_Log_Set_Module_Level(const char* log_module, e_OT_LOG_LEVEL level);
  e_OT_LOG_LEVEL oT_Log_Get_Module_Level(const char* log_module);

  // some helper macros
#define OT_LOG_ERROR(module, format, ...) oT_Log_Write_Format(e_OT_LOG_LEVEL_ERROR, module, "["module"] ERR %s:%d %s() : " format,__FILE__,__LINE__, __FUNCTION__, ##__VA_ARGS__)
#define OT_LOG_INFO(module, format, ...) oT_Log_Write_Format(e_OT_LOG_LEVEL_INFO, module, "["module"] INF : " format, ##__VA_ARGS__)
#define OT_LOG_DEBUG(module, format, ...) oT_Log_Write_Format(e_OT_LOG_LEVEL_DEBUG, module, "["module"] DBG : " format, ##__VA_ARGS__)
#define OT_LOG_DDEBUG(module, format, ...) oT_Log_Write_Format(e_OT_LOG_LEVEL_DEEP_DEBUG, module, "["module"] DBG %s:%d %s() : " format,__FILE__,__LINE__, __FUNCTION__, ##__VA_ARGS__)


#ifdef __cplusplus
}
#endif
#endif /* INCLUDE_OT_LOG_H_ */

/*
 * ot_pos_log.h
 *
 *  Created on: Mar 6, 2020
 *      Author: Gael Pouger
 */

#ifndef INCLUDE_OIKIOSK_LOG_H_
#define INCLUDE_OIKIOSK_LOG_H_

#include "ot_log.h"

#define KIOSK_ERROR(fmt, ...) OT_LOG_ERROR("KIOSK", fmt, ##__VA_ARGS__)
#define KIOSK_INFO(fmt, ...) OT_LOG_INFO("KIOSK", fmt, ##__VA_ARGS__)
#define KIOSK_DEBUG(fmt, ...) OT_LOG_DEBUG("KIOSK", fmt, ##__VA_ARGS__)
#define KIOSK_DDEBUG(fmt, ...) OT_LOG_DDEBUG("KIOSK", fmt, ##__VA_ARGS__)

#endif /* INCLUDE_OIKIOSK_LOG_H_ */

/*
 * kiosk_commands.h
 *
 *  Created on: Feb 8, 2024
 *      Author: Gael Pouger
 */

#ifndef LIBOTIKIOSK_SRC_KIOSK_COMMANDS_H_
#define LIBOTIKIOSK_SRC_KIOSK_COMMANDS_H_

#include <stdbool.h>
#include "libotikiosk.h"

char* build_command(const char* template, ...);
KIOSK_RET parse_id(char *json, int json_len, int *out_id);
KIOSK_RET parse_resp_result(char* json, int json_len, int expected_cmd_id, char* out_result, int max_out_size);
KIOSK_RET check_response_ok(char* json, int json_len, int expected_id);
KIOSK_RET parse_get_status(char* json, int json_len, int expected_id, KIOSK_STATUS* out_status);
KIOSK_RET parse_transaction_complete(char* json, int json_len, otiKioskPaymentResponse *out_pmt_resp, int* out_id);
KIOSK_RET parse_cancel_resp(char* json, int json_len, int expected_cmd_id);

#endif /* LIBOTIKIOSK_SRC_KIOSK_COMMANDS_H_ */

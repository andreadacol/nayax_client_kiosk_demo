/*
 * kiosk_commands.c
 *
 *  Created on: Feb 8, 2024
 *      Author: Gael Pouger
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include "kiosk_commands.h"
#include "mjson.h"
#include "../libotikiosk_types.h"
#include "otiKiosk_log.h"

enum json_field_type {
  JSON_TYPE_STRING,
  JSON_TYPE_BOOL,
  JSON_TYPE_INT,
  JSON_TYPE_DOUBLE
};

struct json_field_parser_options {
  const char* json_path;
  enum json_field_type type;
  void* out;
  int out_len;
};

char* build_command(const char* template, ...) {
  char* ret = NULL;
  va_list args;

  va_start(args, template);
  int ret_len = vsnprintf(NULL, 0, template, args) + 1;
  va_end(args);

  ret = calloc(ret_len, 1);
  if(ret == NULL)
    return NULL;

  va_start(args, template);
  vsprintf(ret, template, args);
  va_end(args);

  return ret;
}

KIOSK_RET parse_id(char *json, int json_len, int *out_id) {
  double d;
  if (mjson_get_number(json, json_len, "$.id", &d) != 1) {
    return KIOSK_RET_PARSING_ERROR;
  }
  *out_id = d;
  return KIOSK_RET_OK;
}

KIOSK_RET parse_resp_result(char* json, int json_len, int expected_cmd_id, char* out_result, int max_out_size) {
  int id = 0;

  // expect to have an ID field
  if(parse_id(json, json_len, &id) != KIOSK_RET_OK) {
    KIOSK_ERROR("missing 'id' field\n");
    return KIOSK_RET_PARSING_ERROR;
  }
  if(id != expected_cmd_id) {
    KIOSK_ERROR("unexpected id (got %d, expected %d)\n", id, expected_cmd_id);
    return KIOSK_RET_PARSING_ERROR;
  }

  // get the result as a string and parse it
  if(mjson_get_string(json, json_len, "$.result", out_result, max_out_size) <= 0) {
    KIOSK_ERROR("missing 'result' field\n");
    return KIOSK_RET_PARSING_ERROR;
  }

  return KIOSK_RET_OK;
}

KIOSK_RET check_response_ok(char* json, int json_len, int expected_cmd_id) {
  char buff[32] = "";
  int id = 0;
  int val = 0;

  // expect to have an ID field
  if(parse_id(json, json_len, &id) != KIOSK_RET_OK) {
    KIOSK_ERROR("missing 'id' field\n");
    return KIOSK_RET_PARSING_ERROR;
  }
  if(id != expected_cmd_id) {
    KIOSK_ERROR("unexpected id (got %d, expected %d)\n", id, expected_cmd_id);
    return KIOSK_RET_PARSING_ERROR;
  }

  // check for error
  const char *p;
  int n;
  if(mjson_find(json, json_len, "$.error", &p, &n) == MJSON_TOK_OBJECT) {
    KIOSK_ERROR("kiosk returned an error: %.*s\n", n, p);
    return KIOSK_RET_NEGATIVE_RESP;
  }

  if(!mjson_get_bool(json, json_len, "$.result", &val)) {
    KIOSK_ERROR("missing 'result' field or wrong format\n");
    return KIOSK_RET_PARSING_ERROR;
  }

  return(val ? KIOSK_RET_OK : KIOSK_RET_NEGATIVE_RESP);
}

KIOSK_RET parse_get_status(char* json, int json_len, int expected_cmd_id, KIOSK_STATUS* out_status) {
  char buff[32] = "";

  KIOSK_RET ret = parse_resp_result(json, json_len, expected_cmd_id, buff, sizeof(buff));
  if(ret != KIOSK_RET_OK)
    return ret;

  if(strcmp(buff, "Ready") == 0)
    *out_status = OK_READY;
  else if(strcmp(buff, "PaymentTransaction") == 0)
    *out_status = OK_TRANSACTION;
  else if(strcmp(buff, "Update") == 0)
    *out_status = OK_UPDATE;
  else if(strcmp(buff, "Unconfirmed") == 0)
    *out_status = OK_UNCONFIRMED;
  else if(strcmp(buff, "NotReady") == 0)
    *out_status = OK_NOT_READY;
  else if(strcmp(buff, "NoReader") == 0)
    *out_status = OK_NO_READER;
  else if(strcmp(buff, "NoTerminalId") == 0)
    *out_status = OK_NO_TERMINAL_ID;
  else
    return KIOSK_RET_PARSING_ERROR;

  return KIOSK_RET_OK;
}

KIOSK_RET parse_cancel_resp(char* json, int json_len, int expected_cmd_id) {
	char buff[32] = "";
	KIOSK_RET ret = parse_resp_result(json, json_len, expected_cmd_id, buff, sizeof(buff));
	if(ret != KIOSK_RET_OK)
		return ret;

	if(strcmp(buff, "Ok") == 0)
		ret = KIOSK_RET_OK;
	else if(strcmp(buff, "NoTransaction" == 0))
		ret = KIOSK_RET_OK;
	else if(strcmp(buff, "CannotCancel" == 0))
		ret = KIOSK_RET_NEGATIVE_RESP;

	return ret;
}

bool parse_json_fields(const char* json, int json_len, struct json_field_parser_options *fields, int nb_fields) {
  double dv;
  int bv;
  int len;
  for(int i=0; i < nb_fields; i++) {
    switch(fields[i].type) {
    case JSON_TYPE_STRING:
      len = mjson_get_string(json, json_len, fields[i].json_path, (char*)fields[i].out, fields[i].out_len-1);
      if(len < 0) {
        KIOSK_ERROR("failed to parse string field %s\n", fields[i].json_path);
        return false;
      }
      ((char*)fields[i].out)[len] = '\0';
      break;
    case JSON_TYPE_BOOL:
      if(mjson_get_bool(json, json_len, fields[i].json_path, &bv) == 0) {
        KIOSK_ERROR("failed to parse boolean field %s\n", fields[i].json_path);
        return false;
      }
      *(bool*)fields[i].out = (bv != 0);
      break;
    case JSON_TYPE_INT:
      if(mjson_get_number(json, json_len, fields[i].json_path, &dv) == 0 || dv - (int)dv != 0) {
        KIOSK_ERROR("failed to parse integer field %s\n", fields[i].json_path);
        return false;
      }
      *(int*)fields[i].out = (int)dv;
      break;
    case JSON_TYPE_DOUBLE:
      if(mjson_get_number(json, json_len, fields[i].json_path, &dv) == 0) {
        KIOSK_ERROR("failed to parse float field %s\n", fields[i].json_path);
        return false;
      }
      *(double*)fields[i].out = dv;
      break;
    }
  }
  return true;
}

KIOSK_RET parse_transaction_complete(char* json, int json_len, otiKioskPaymentResponse *out_pmt_resp, int* out_id) {
  // expect to have an ID field
  if(parse_id(json, json_len, out_id) != KIOSK_RET_OK) {
    KIOSK_ERROR("missing 'id' field\n");
    return KIOSK_RET_PARSING_ERROR;
  }

  memset(out_pmt_resp, 0, sizeof(otiKioskPaymentResponse));

  // make sure that the method is "TransactionComplete"
  const char *p;
  int n;
  if(mjson_find(json, json_len, "$.method", &p, &n) != MJSON_TOK_STRING || n != strlen("\"TransactionComplete\"") || memcmp("\"TransactionComplete\"", p, n) != 0)
    return KIOSK_RET_PARSING_ERROR;

  // parse the fields
  const char* s_params;
  int params_len;
  if(mjson_find(json, json_len, "$.params", &s_params, &params_len) != MJSON_TOK_OBJECT) {
    KIOSK_ERROR("failed to parse params in TransactionComplete event\n");
    return KIOSK_RET_PARSING_ERROR;
  }

  char str_status[32];
  struct json_field_parser_options fields[] = {
      {"$.status", JSON_TYPE_STRING, str_status, sizeof(str_status)},
      {"$.errorDescription", JSON_TYPE_STRING, &(out_pmt_resp->error_message), sizeof(out_pmt_resp->error_message)},
      {"$.errorCode", JSON_TYPE_INT, &(out_pmt_resp->error_code), sizeof(out_pmt_resp->error_code)},
      {"$.authorizationDetails.AmountAuthorized", JSON_TYPE_DOUBLE, &(out_pmt_resp->amount_authorized), sizeof(out_pmt_resp->amount_authorized)},
      {"$.authorizationDetails.AmountRequested", JSON_TYPE_DOUBLE, &(out_pmt_resp->amount_requested), sizeof(out_pmt_resp->amount_requested)},
      {"$.authorizationDetails.Transaction_Referance", JSON_TYPE_STRING, &(out_pmt_resp->transaction_reference), sizeof(out_pmt_resp->transaction_reference)},
      {"$.authorizationDetails.PartialPan", JSON_TYPE_STRING, &(out_pmt_resp->partial_PAN), sizeof(out_pmt_resp->partial_PAN)},
      {"$.authorizationDetails.CardType", JSON_TYPE_STRING, &(out_pmt_resp->card_type), sizeof(out_pmt_resp->card_type)},
      {"$.authorizationDetails.Card_ID", JSON_TYPE_STRING, &(out_pmt_resp->card_id), sizeof(out_pmt_resp->card_id)},
      {"$.authorizationDetails.CardToken", JSON_TYPE_STRING, &(out_pmt_resp->card_token), sizeof(out_pmt_resp->card_token)},
  };
  if(!parse_json_fields(s_params, params_len, fields, sizeof(fields)/sizeof(fields[0])))
    return KIOSK_RET_PARSING_ERROR;

  // translate the status
  if(strcmp(str_status, "OK") == 0)
    out_pmt_resp->status = otiTransactionStatus_OK;
  else if(strcmp(str_status, "Declined") == 0)
    out_pmt_resp->status = otiTransactionStatus_Declined;
  else if(strcmp(str_status, "Error") == 0)
    out_pmt_resp->status = otiTransactionStatus_Error;
  else if(strcmp(str_status, "Timeout") == 0)
    out_pmt_resp->status = otiTransactionStatus_Timeout;
  else if(strcmp(str_status, "Cancelled") == 0)
    out_pmt_resp->status = otiTransactionStatus_Cancelled;
  else if(strcmp(str_status, "Void") == 0)
    out_pmt_resp->status = otiTransactionStatus_Voided;
  else if(strcmp(str_status, "LocalMifare") == 0)
    out_pmt_resp->status = otiTransactionStatus_LocalMifare;
  else {
    KIOSK_ERROR("unsupported transaction status '%s'\n", str_status);
    return KIOSK_RET_PARSING_ERROR;
  }

  return KIOSK_RET_OK;
}

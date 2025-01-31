/*
 * libotikiosk.h
 *
 *  Created on: Feb 7, 2024
 *      Author: Gael Pouger
 */

#ifndef LIBOTIKIOSK_LIBOTIKIOSK_TYPES_H_
#define LIBOTIKIOSK_LIBOTIKIOSK_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

typedef struct {
  uint32_t amount_cents;
  uint32_t fee_cents;
  uint32_t product_id;
  uint16_t currency_code;
  unsigned int timeout_sec; // only used if not continuous
  bool continuous;
} otiKioskPaymentParameters;

typedef enum
{
  otiTransactionStatus_Error,
  otiTransactionStatus_OK,
  otiTransactionStatus_Declined,
  otiTransactionStatus_Voided,
  otiTransactionStatus_Timeout,
  otiTransactionStatus_Cancelled,
  otiTransactionStatus_LocalMifare,
  otiTransactionStatus__Undefined = 0xff
} otiTransactionStatus;

typedef struct {
  otiTransactionStatus status;
  int error_code;
  char error_message[100];
  double amount_requested;
  double amount_authorized;
  char transaction_reference[128];
  char partial_PAN[20];
  char card_type[32];
  char card_id[32];
  char card_token[128];
// TODO: add missing fields
} otiKioskPaymentResponse;

typedef enum {
  OK_READY,
  OK_TRANSACTION,
  OK_UPDATE,
  OK_UNCONFIRMED,
  OK_NOT_READY,
  OK_NO_KIOSK,
  OK_NO_READER,
  OK_NO_TERMINAL_ID,
  OK_ERROR
} KIOSK_STATUS;

typedef enum {
  KIOSK_RET_OK,
  KIOSK_RET_GENERAL_ERROR,
  KIOSK_RET_MEMORY_ERROR,
  KIOSK_RET_PARSING_ERROR,
  KIOSK_RET_COMM_ERROR,
  KIOSK_RET_NEGATIVE_RESP,
} KIOSK_RET;

// callback types
typedef void (*RdrEventCb_t)(uint8_t msg_index, char* s_line1, char* s_line2);
typedef void (*TransactionCompleteCb_t)(otiKioskPaymentResponse* resp);

#endif /* LIBOTIKIOSK_LIBOTIKIOSK_TYPES_H_ */

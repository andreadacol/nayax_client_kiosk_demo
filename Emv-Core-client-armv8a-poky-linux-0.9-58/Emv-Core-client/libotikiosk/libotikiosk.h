/*
 * libotikiosk.h
 *
 *  Created on: Feb 7, 2024
 *      Author: Gael Pouger
 */
#ifndef LIBOTIKIOSK_LIBOTIKIOSK_H_
#define LIBOTIKIOSK_LIBOTIKIOSK_H_

#include "libotikiosk_types.h"

/**
 * Initializes the kiosk library and starts the connection to the kiosk sockets.
 * @param is_local: uses Unix domain socket if true, TCP sockets if false
 * @param server_address: for Unix domain sockets, path to the folder containing the sockets (can be NULL). For TCP sockets, IP address or hostname of the server (can be NULL, defaults to localhost).
 */
bool LibOtiKiosk_Init(const char* server_address, bool is_local);

/**
 * Registers a function that will be called when a transaction (started with LibOtiKiosk_PayTransaction or LibOtiKiosk_PreAuthorize) completes.
 * It is called even if the transaction failed or was cancelled.
 * The callback should copy the data that the application needs to keep before returning, the provided parameters do not persist after the callback returns.
 */
void LibOtiKiosk_Register_TransactionComplete_Callback(TransactionCompleteCb_t cb);

/**
 * Registers a function that will be called when the reader display changes.
 * The callback should copy the data that the application needs to keep before returning, the provided parameters do not persist after the callback returns.
 */
void LibOtiKiosk_Register_ReaderEvent_Callback(RdrEventCb_t cb);

/**
 * Makes the library's logs more verbose.
 */
void LibOtiKiosk_Enable_Debug_Logs(bool enabled);

/**
 * Ask the Kiosk for its current status.
 */
KIOSK_RET LibOtiKiosk_GetStatus(KIOSK_STATUS* out_status);

/**
 * Request the Kiosk to show a message on the reader screen.
 */
KIOSK_RET LibOtiKiosk_ShowMessage(const char* line1, const char* line2);

/**
 * Read the Kiosk's identification number as a string in the provided buffer.
 */
KIOSK_RET LibOtiKiosk_GetKioskId(char* out_id, int max_out_size);

/**
 * Read the Kiosk's version number as a string in the provided buffer.
 */
KIOSK_RET LibOtiKiosk_GetKioskVersion(char* out_version, int max_out_size);

/**
 * Read the reader's firmware version number as a string in the provided buffer.
 */
KIOSK_RET LibOtiKiosk_GetReaderVersion(char* out_version, int max_out_size);

/**
 * Start a Pre-Authorization process.
 * If the pre-authorization approved, the requested amount is only reserved and the transaction should then be either confirmed (with LibOtiKiosk_ConfirmTransaction) or voided (with  LibOtiKiosk_VoidTransaction).
 */
KIOSK_RET LibOtiKiosk_PreAuthorize(otiKioskPaymentParameters *params);

/**
 * Start a payment process.
 * If the payment is approved the transaction is complete, no extra step needs to be taken.
 */
KIOSK_RET LibOtiKiosk_PayTransaction(otiKioskPaymentParameters *params);

/**
 * Confirm a previously pre-authorized transaction.
 */
KIOSK_RET LibOtiKiosk_ConfirmTransaction(uint32_t amount_cents, uint32_t fee_cents, uint32_t product_id, char* transaction_reference);

/**
 * Void a previously authorized transaction.
 */
KIOSK_RET LibOtiKiosk_VoidTransaction(char* transaction_reference);

/**
 * Cancel an ongoing payment process started with LibOtiKiosk_PayTransaction or LibOtiKiosk_PreAuthorize.
 */
KIOSK_RET LibOtiKiosk_CancelTransaction(void);

#endif /* LIBOTIKIOSK_LIBOTIKIOSK_H_ */

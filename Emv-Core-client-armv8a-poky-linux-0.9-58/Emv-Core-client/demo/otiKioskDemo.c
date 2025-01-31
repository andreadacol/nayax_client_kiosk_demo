#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <string.h>
#include "libotikiosk.h"

const char* ok_status_to_text(KIOSK_STATUS s) {
  switch(s) {
  case OK_ERROR:
    return "ERROR";
  case OK_NOT_READY:
    return "NOT READY";
  case OK_NO_KIOSK:
    return "NO KIOSK";
  case OK_READY:
    return "READY";
  case OK_TRANSACTION:
    return "TRANSACTION";
  case OK_UNCONFIRMED:
    return "UNCONFIRMED";
  case OK_UPDATE:
    return "UPDATE";
  case OK_NO_READER:
    return "NO READER";
  case OK_NO_TERMINAL_ID:
    return "NO TERMINAL ID";
  default:
    return "UNKNOWN";
  }
}

const char* trans_status_to_text(otiTransactionStatus s) {
  switch(s) {
  case otiTransactionStatus_OK: return "SUCCESS";
  case otiTransactionStatus_Declined: return "DECLINED";
  case otiTransactionStatus_Cancelled: return "CANCELLED";
  case otiTransactionStatus_Timeout: return "TIMEOUT";
  case otiTransactionStatus_Voided: return "VOIDED";
  case otiTransactionStatus_Error: return "ERROR";
  default: return "UNKNOWN";
  }
}

enum DEMO_STATE {
  ST_INIT,
  ST_IDLE,
  ST_TRANS,
  ST_TRANS_COMPLETE,
  ST_MESSAGE,
  _ST_INVALID
};

uint32_t get_time_sec(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);

  return((uint32_t)ts.tv_sec);
}

KIOSK_STATUS last_status = OK_NO_KIOSK;
void check_kiosk_status() {
  KIOSK_STATUS new_status = OK_NO_KIOSK;
  KIOSK_RET ret = LibOtiKiosk_GetStatus(&new_status);
  if(ret != KIOSK_RET_OK)
    new_status = OK_NO_KIOSK;
  if(new_status != last_status) {
    printf("  Kiosk status changed: %s->%s\n", ok_status_to_text(last_status), ok_status_to_text(new_status));
    last_status = new_status;
  }
}

enum DEMO_STATE cur_state = _ST_INVALID;
enum DEMO_STATE new_state = ST_INIT;
otiKioskPaymentResponse last_payment_response;

void Transaction_Complete_Callback(otiKioskPaymentResponse* resp) {
  // store the received data
  memcpy(&last_payment_response, resp, sizeof(otiKioskPaymentResponse));

  // do something with it
  if(cur_state == ST_TRANS)
    new_state = ST_TRANS_COMPLETE;
}

void Reader_Event_Callback(uint8_t msg_index, char* s_line1, char* s_line2) {
  printf("    Reader message 0x%.2X: '%s', '%s'\n", msg_index, s_line1, s_line2);
}

int main(void) {
  otiKioskPaymentParameters pmt_params;
  uint32_t time_state_expiration = 0;

  char kiosk_id[32] = "";
  char kiosk_version[32] = "";
  char reader_version[32] = "";

  pmt_params.amount_cents = 450;
  pmt_params.fee_cents = 0;
  pmt_params.currency_code = 978;
  pmt_params.timeout_sec = 10;
  pmt_params.continuous = false;
  pmt_params.product_id = 0;

  LibOtiKiosk_Enable_Debug_Logs(true);

  /* Uses internal (Unix Domain) sockets, this is the default behavior for Kiosk Core.
   * Will work only if both applications are started from the same folder, or
   * if the OTI_KIOSK_SOCKET_DIR environment variable is set.
   */
  LibOtiKiosk_Init(NULL, true);

  /*
   * If Kiosk Core is not started from the same folder as this demo application, replace with:
   *  LibOtiKiosk_Init("[Kiosk Core 'var' folder path]", false);
   *
   * If Kiosk Core is running on the same machine and using TCP sockets, replace with:
   *  LibOtiKiosk_Init(NULL, false);
   *
   * If Kiosk Core is running on an other machine and using TCP sockets, replace with:
   *  LibOtiKiosk_Init([Kiosk Core machine IP address], false);
   */

  LibOtiKiosk_Register_TransactionComplete_Callback(Transaction_Complete_Callback);
  LibOtiKiosk_Register_ReaderEvent_Callback(Reader_Event_Callback);

  while(true) {
    check_kiosk_status();

    // if kiosk is not answering - wait
    while(last_status == OK_NO_KIOSK) {
      sleep(1);
      check_kiosk_status();
      new_state = ST_INIT;
    }

    // check for state change
    if(new_state != cur_state) {
      cur_state = new_state;
      switch(cur_state) {
      case ST_INIT:
        // get kiosk ID and versions and print them
        if(LibOtiKiosk_GetKioskId(kiosk_id, sizeof(kiosk_id)) == KIOSK_RET_OK &&
            LibOtiKiosk_GetKioskVersion(kiosk_version, sizeof(kiosk_version)) == KIOSK_RET_OK &&
            LibOtiKiosk_GetReaderVersion(reader_version, sizeof(reader_version)) == KIOSK_RET_OK) {
          printf(" Kiosk ID: %s\n", kiosk_id);
          printf(" Kiosk version: %s\n", kiosk_version);
          printf(" Reader version: %s\n", reader_version);
          new_state = ST_IDLE;
        } else {
          // something went wrong, stay in init state
          sleep(1);
        }
        break;
      case ST_IDLE:
        LibOtiKiosk_ShowMessage("Welcome", "Please wait");
        time_state_expiration = get_time_sec() + 5; // show this message for 5 seconds
        break;
      case ST_TRANS:
        // start pre-auth
        if(LibOtiKiosk_PreAuthorize(&pmt_params) == KIOSK_RET_OK)
          time_state_expiration = 0; // no expiration, state change will be triggered by TransactionComplete event
        else {
          // failed to start transaction, show error message
          LibOtiKiosk_ShowMessage("pmt start", "failed");
          new_state = ST_MESSAGE;
        }
        break;
      case ST_TRANS_COMPLETE:
        printf("transaction complete with status: %s\n", trans_status_to_text(last_payment_response.status));
        if(last_payment_response.status == otiTransactionStatus_OK || last_payment_response.status == otiTransactionStatus_LocalMifare) {
          // TODO: print some details
          LibOtiKiosk_ShowMessage("approved", "thank you!");
          static bool confirm = true;
          if(confirm) {
            // confirm with product ID 42
            LibOtiKiosk_ConfirmTransaction(450, 0, 42, last_payment_response.transaction_reference);
          } else {
            //void transaction
            LibOtiKiosk_VoidTransaction(last_payment_response.transaction_reference);
          }
          confirm = !confirm;
        } else {
          LibOtiKiosk_ShowMessage(trans_status_to_text(last_payment_response.status), "");
        }
        new_state = ST_MESSAGE;
        break;
      case ST_MESSAGE:
        // message is already set, just set expiration
        time_state_expiration = get_time_sec() + 5; // show this message for 5 seconds
        break;
      }
    }

    // check for expiration
    if(time_state_expiration > 0 && get_time_sec() > time_state_expiration) {
      switch(cur_state) {
        case ST_IDLE:
          if(last_status == OK_READY)
            new_state = ST_TRANS; // start a transaction
          break;
        case ST_MESSAGE:
          new_state = ST_IDLE; // go back to idle state
      }
    }

    sleep(1);
  }
  return 0;
}

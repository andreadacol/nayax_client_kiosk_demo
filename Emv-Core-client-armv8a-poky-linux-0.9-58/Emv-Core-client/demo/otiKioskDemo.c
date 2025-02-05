#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "libotikiosk.h"

// ADACOL:
#include <libwebsockets.h>

#include "cJSON.h"

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


// ADACOL: libwebsocket ////////////////////////////////////////////////////////
static int interrupted = 0;
static int _payment_status = 0;

static struct lws *_wsi = NULL;  // Store the WebSocket instance
static int _send_message_flag = 0;  // Flag to indicate a message should be sent
char _json_message[256];

/* Signal handler to stop the loop */
void sigint_handler(int sig) {
    interrupted = 1;
}

/* WebSocket callback */
static int callback_websocket(struct lws *wsi, enum lws_callback_reasons reason,
                              void *user, void *in, size_t len) {

//	char buff[len];
//	memset(buff,0,len);

    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            printf("Client connected!\n");
            _wsi = wsi;  // Store the WebSocket instance
            _payment_status = 1;
            break;
        case LWS_CALLBACK_RECEIVE:

        	// ADACOL:

//        	memset(buff,0,len);
//        	memcpy(buff, in, len);

//        	printf("sizeof buff : %d\n", (int)sizeof(buff));
//        	for(uint8_t ind=0; ind<len; ind++)
//        	{
//            	printf("buff[i]: %c\n", buff[ind]);
//        	}

//        	printf("sizeof buff : %d\n", (int)sizeof(buff));
//        	printf("Received len: %d\n", (int)len);
//        	if(memcmp(buff, "Hello, WebSocket!", sizeof(buff)) == 0) {
//        		printf("Received buff: %s\n", (char *)buff);
//        	}

//            lws_write(wsi, in, len, LWS_WRITE_TEXT);  // Echo back the message
            break;
        case LWS_CALLBACK_SERVER_WRITEABLE:
        	if (_send_message_flag) {
                  // Prepare message buffer (libwebsockets requires LWS_SEND_BUFFER_PRE_PADDING)
                  unsigned char buf[LWS_PRE + 256];
                  unsigned char *p = &buf[LWS_PRE];
                  size_t json_len = strlen(_json_message);
                  memcpy(p, _json_message, json_len);

                  // Send the message
                  lws_write(wsi, p, json_len, LWS_WRITE_TEXT);

                  _send_message_flag = 0;  // Reset flag
        	}
        	break;

        case LWS_CALLBACK_CLOSED:
            printf("Client disconnected!\n");
            _payment_status = 0;
            _wsi = NULL;
            break;
        default:
            break;
    }
    return 0;
}
// Function to send a JSON message
void send_json_message(const char* content) {
    if (_wsi) {

		_send_message_flag = 1;  // Set flag to trigger sending in LWS_CALLBACK_SERVER_WRITEABLE

		// Create JSON object
		cJSON *root = cJSON_CreateObject();
		cJSON_AddStringToObject(root, "type", "transaction");
		cJSON_AddStringToObject(root, "content", content);

		// Convert JSON to string
		char *json_str = cJSON_PrintUnformatted(root);
		snprintf(_json_message, sizeof(_json_message), "%s", json_str);
		cJSON_free(json_str);  // Free memory
		cJSON_Delete(root);  // Delete JSON object

		lws_callback_on_writable(_wsi);  // Notify libwebsockets to send data
    }
}

static void test_cjson() {
    // Create a JSON object
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "message");
    cJSON_AddStringToObject(root, "content", "Hello, WebSocket!");

    // Convert JSON object to a string
    char *json_str = cJSON_Print(root);
    printf("Generated JSON:\n%s\n", json_str);

    // Clean up
    free(json_str);
    cJSON_Delete(root);
}

/* WebSocket protocol list */
static const struct lws_protocols protocols[] = {
    {"example-protocol", callback_websocket, 0, 4096},
    {NULL, NULL, 0, 0}
};


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

  printf("ADACOL: Kiosk IMX  program \n");

//  test_cjson();

  // ADACOL: libwebsocket //////////////////////////////////////////////////////
  signal(SIGINT, sigint_handler);

  struct lws_context_creation_info info = {0};
  info.port = 9000;  // WebSocket server will listen on this port
  info.protocols = protocols;

  struct lws_context *context = lws_create_context(&info);
  if (!context) {
      printf("Failed to create WebSocket server!\n");
      return -1;
  }
  printf("WebSocket server started on port 9000...\n");
  // ADACOL: libwebsocket end ///////////////////////////////////////////////////

  //LibOtiKiosk_Enable_Debug_Logs(true);

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
        _payment_status = 0;
        send_json_message(trans_status_to_text(last_payment_response.status));
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
//          _payment_status = 0;
        }
        new_state = ST_MESSAGE;
        break;
      case ST_MESSAGE:
        // message is already set, just set expiration
        time_state_expiration = get_time_sec() + 5; // show this message for 5 seconds
        break;
      }
    }

    if (!interrupted) {
        lws_service(context, 1000);  // Process WebSocket events
    }

    if (_payment_status == 1)  {
    	if (cur_state == ST_IDLE) {
    		new_state = ST_TRANS; // start a transaction
    		printf("new_state = ST_TRANS");
    	}

    }

    // check for expiration
    if(time_state_expiration > 0 && get_time_sec() > time_state_expiration) {
      switch(cur_state) {
//        case ST_IDLE:
//          if(last_status == OK_READY)
//            new_state = ST_TRANS; // start a transaction
//          break;
        case ST_MESSAGE:
          new_state = ST_IDLE; // go back to idle state
//          break;
      }
    }

    sleep(1);
  }

  lws_context_destroy(context);
  printf("Server stopped.\n");

  return 0;
}

// implements
#include "libotikiosk.h"

// uses
#include <pthread.h>
#include <semaphore.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include "mjson.h"
#include "kiosk_commands.h"
#include "otiKiosk_log.h"
#include "emv-core-lib-version.h"

// internal types
typedef struct {
  bool is_tcp;
  char* server_addr; // TODO: add more options for handling domain sockets
  uint16_t tcp_port;
  int incoming_timeout_ms;
  int sockfd;
  pthread_mutex_t mutex;
  void (*recv_cb)(unsigned char* data, int data_len);
  uint8_t work_buffer[1024];
} KioskSocketOptions;

// variables
static pthread_t _commands_thread;
static pthread_t _reader_thread;

static KioskSocketOptions _commands_socket_options;
static KioskSocketOptions _reader_socket_options;

static sem_t sema_resp_ready; // for signaling when the response to a command has been received
static sem_t sema_resp_done; // for signaling when the received response has been handled and reception can resume
static uint32_t current_resp_len;

static otiKioskPaymentResponse pmt_resp;

static TransactionCompleteCb_t _trans_complete_app_cb = NULL;
static RdrEventCb_t _reader_event_app_cb = NULL;

static int _receive_raw(int sfd, char* buff, int buff_size, int timeout_ms) {
  struct pollfd pfd = {0};
  pfd.fd = sfd;
  pfd.events = POLLIN;

  // wait for incoming data
  int ret = poll(&pfd, 1, timeout_ms);
  if(ret < 0) {
    KIOSK_ERROR("error on poll (%s)\n", strerror(errno));
    return -1;
  } else if(ret > 0) {
    // something happened, either there is something to read or the socket got closed by the other side
    int len = read(sfd, buff, buff_size);
    if(len > 0) {
      if(len < buff_size) {
        // clear the remaining part of the buffer
        memset(&buff[len], 0, buff_size-len);
      }
      return len;
    }

    // read with zero length after a poll event means that the socket is closed
    return -1;
  } else {
    // timeout
    return 0;
  }
}

static void* _kiosk_comm_loop(void* arg) {
  KioskSocketOptions* socket_options = (KioskSocketOptions*)arg;
  // outer loop to maintain the connection with the server
  while(true) {
    // take the mutex to prevent other accesses while the socket is not connected
    pthread_mutex_lock(&socket_options->mutex);

    if(socket_options->sockfd >= 0) {
      KIOSK_INFO("closing socket to %s:%d\n", socket_options->server_addr, socket_options->tcp_port);
      shutdown(socket_options->sockfd, SHUT_RDWR);
      close(socket_options->sockfd);
      socket_options->sockfd = -1;
      sleep(1);
    }

    KIOSK_INFO("opening socket to %s:%d\n", socket_options->server_addr, socket_options->tcp_port);

    if(socket_options->is_tcp) {
      // resolve address
      struct hostent* host_info = gethostbyname(socket_options->server_addr);
      if(host_info == NULL) {
        KIOSK_ERROR("failed to resolve hostname %s\n", socket_options->server_addr);
        pthread_mutex_unlock(&socket_options->mutex);
        sleep(1);
        continue;
      }

      // create socket
      socket_options->sockfd = socket(AF_INET, SOCK_STREAM, 0);
      if(socket_options->sockfd < 0) {
        KIOSK_ERROR("failed to create socket (%s)\n", strerror(errno));
        pthread_mutex_unlock(&socket_options->mutex);
        sleep(1);
        continue;
      }

      // connect to server
      struct sockaddr_in s_addr = {0};
      s_addr.sin_addr = *(struct in_addr*) host_info->h_addr;
      s_addr.sin_port = htons(socket_options->tcp_port);
      s_addr.sin_family = AF_INET;
      if(connect(socket_options->sockfd, (struct sockaddr*)&s_addr, sizeof(struct sockaddr)) != 0) {
        KIOSK_ERROR("failed to connect socket to %s:%d (%s)\n", socket_options->server_addr, socket_options->tcp_port, strerror(errno));
        close(socket_options->sockfd);
        socket_options->sockfd = -1;
        pthread_mutex_unlock(&socket_options->mutex);
        sleep(1);
        continue;
      }
    } else {
      struct sockaddr_un s_addr = {0};
      if(strlen(socket_options->server_addr) >= sizeof(s_addr.sun_path)-1) {
        KIOSK_ERROR("socket path is too long: %s\n", socket_options->server_addr);
        pthread_mutex_unlock(&socket_options->mutex);
        sleep(1);
        continue;
      }

      // create socket
      socket_options->sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
      if(socket_options->sockfd < 0) {
        KIOSK_ERROR("failed to create socket (%s)\n", strerror(errno));
        pthread_mutex_unlock(&socket_options->mutex);
        sleep(1);
        continue;
      }

      // connect to server
      s_addr.sun_family = AF_UNIX;
      strncpy(s_addr.sun_path, socket_options->server_addr, sizeof(s_addr.sun_path)-1);
      if(connect(socket_options->sockfd, (struct sockaddr*)&s_addr, sizeof(s_addr)) != 0) {
        KIOSK_ERROR("failed to connect socket to %s (%s)\n", socket_options->server_addr, strerror(errno));
        close(socket_options->sockfd);
        socket_options->sockfd = -1;
        pthread_mutex_unlock(&socket_options->mutex);
        sleep(1);
        continue;
      }
    }

    pthread_mutex_unlock(&socket_options->mutex);
    KIOSK_INFO("successfully connected to %s:%d\n", socket_options->server_addr, socket_options->tcp_port);

    // inner loop to receive events
    while(true) {
      if(socket_options->sockfd < 0) {
        break;
      }

      // wait for incoming data
      int received = _receive_raw(socket_options->sockfd, socket_options->work_buffer, sizeof(socket_options->work_buffer), socket_options->incoming_timeout_ms);
      if(received < 0) {
        KIOSK_ERROR("error on _receive_raw (%s)\n", strerror(errno));
        break;
      } else if(received > 0) {
        socket_options->recv_cb(socket_options->work_buffer, received);
      } else {
        // timeout, just do nothing and continue
      }
    }
  }
  pthread_exit(NULL);
}

static KIOSK_RET send_to_kiosk(char* data, int len) {
  KIOSK_DEBUG("sending message to kiosk: %.*s\n", len, data);

  if(_commands_socket_options.sockfd < 0) {
    KIOSK_ERROR("kiosk socket is not connected\n");
    return KIOSK_RET_COMM_ERROR;
  }

  int written = write(_commands_socket_options.sockfd, data, len);
  if(written != len) {
    pthread_mutex_lock(&_commands_socket_options.mutex);
    close(_commands_socket_options.sockfd);
    _commands_socket_options.sockfd = -1;
    pthread_mutex_unlock(&_commands_socket_options.mutex);
  }

  if(written != len) {
    KIOSK_ERROR("not all data written (%d instead of %d)\n", written, len);
    return KIOSK_RET_COMM_ERROR;
  }

  KIOSK_DEBUG("sent successfully\n");
  return KIOSK_RET_OK;
}

static int _sema_wait_timeout(sem_t *sema, uint32_t timeout_ms) {
  // compute absolute timeout
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  ts.tv_sec += timeout_ms / 1000;
  ts.tv_nsec += (timeout_ms % 1000) * 1000000;
  // adjust in case nanoseconds overflowed
  ts.tv_sec += ts.tv_nsec / 1000000000;
  ts.tv_nsec = ts.tv_nsec % 1000000000;

  return sem_timedwait(sema, &ts);
}

static void _sema_clear(sem_t *sema) {
  while(sem_trywait(sema) == 0);
}

static int _expected_id = -1;
static KIOSK_RET send_receive(char* cmd, int cmd_len, char* resp, int* resp_len, int timeout_ms) {
  int id = 0;
  if(parse_id(cmd, cmd_len, &id) != KIOSK_RET_OK) {
    KIOSK_ERROR("missing 'id' in command, can't send to kiosk\n");
    return KIOSK_RET_GENERAL_ERROR;
  }

  // store expected response id
  _expected_id = id;

  // clear response semaphore
  _sema_clear(&sema_resp_ready);

  KIOSK_RET ret = send_to_kiosk(cmd, cmd_len);
  if(ret != KIOSK_RET_OK)
    return ret;

  // wait on semaphore (signalled when a response is ready)
  if(_sema_wait_timeout(&sema_resp_ready, timeout_ms) == 0) {
    // copy from the response buffer
    if(current_resp_len > *resp_len) {
      KIOSK_ERROR("received message is larger than output buffer (%d > %d)\n", current_resp_len, *resp_len);
      return KIOSK_RET_GENERAL_ERROR;
    }
    *resp_len = current_resp_len;
    memcpy(resp, _commands_socket_options.work_buffer, *resp_len);
    // signal that response is handled
    sem_post(&sema_resp_done);
    // clean up
    _expected_id = -1;
  } else {
    KIOSK_ERROR("error waiting for response: %s\n", strerror(errno));
    return KIOSK_RET_COMM_ERROR;
  }

  return KIOSK_RET_OK;
}

static bool LibOtiKiosk_Init_Common() {
  // initialize semaphore
  if(sem_init(&sema_resp_ready, 0, 0) != 0)
    return false;
  if(sem_init(&sema_resp_done, 0, 0) != 0)
    return false;

  // start a thread for handling each socket
  pthread_create(&_commands_thread, NULL, _kiosk_comm_loop, &_commands_socket_options);
  pthread_create(&_reader_thread, NULL, _kiosk_comm_loop, &_reader_socket_options);
  return true;
}

static void reader_event_received(unsigned char* data, int data_len) {
  // parse the JSON message and call the application's reader message callback
  KIOSK_DEBUG("received event from reader: %*s\n", data_len, data);

  if(_reader_event_app_cb == NULL)
    return;

  // expect "method" to be "ReaderMessageEvent"
  const char *p;
  int n;
  if(mjson_find(data, data_len, "$.method", &p, &n) != MJSON_TOK_STRING || n != strlen("\"ReaderMessageEvent\"") || memcmp("\"ReaderMessageEvent\"", p, n) != 0)
  {
    KIOSK_ERROR("failed to parse 'method' field in ReaderMessageEvent: %.*s\n", data_len, data);
    return;
  }

  // parse the fields
  const char* s_params;
  int params_len;
  if(mjson_find(data, data_len, "$.params", &s_params, &params_len) != MJSON_TOK_OBJECT) {
    KIOSK_ERROR("failed to parse 'params' in ReaderMessageEvent: %.*s\n", data_len, data);
    return;
  }

  double msg_idx;
  if(mjson_get_number(s_params, params_len, "$.index", &msg_idx) == 0 || msg_idx < 0 || msg_idx > 0xFF) {
    KIOSK_ERROR("failed to parse 'index' in ReaderMessageEvent: %.*s\n", data_len, data);
    return;
  }

  char* line1 = NULL;
  char* line2 = NULL;
  if(mjson_find(s_params, params_len, "$.line1", &p, &n) == MJSON_TOK_STRING) {
    line1 = calloc(n-1, sizeof(char));
    memcpy(line1, p+1, n-2);
  }

  if(mjson_find(s_params, params_len, "$.line2", &p, &n) == MJSON_TOK_STRING) {
    line2 = calloc(n-1, sizeof(char));
    memcpy(line2, p+1, n-2);
  }

  _reader_event_app_cb(msg_idx, line1 == NULL ? "" : line1, line2 == NULL ? "" : line2);

  if(line1 != NULL)
    free(line1);

  if(line2 != NULL)
    free(line2);
}

static void kiosk_msg_received(unsigned char* data, int data_len) {
  KIOSK_DEBUG("received data from kiosk: %*s\n", data_len, data);

  // check if it's a response that we expect
  int id = 0;
  if(_expected_id >= 0 && parse_id(data, data_len, &id) == KIOSK_RET_OK && id == _expected_id) {
    current_resp_len = data_len;
    // clear the "response done" semaphore
    _sema_clear(&sema_resp_done);
    // signal that the response is ready
    sem_post(&sema_resp_ready);
    // wait for the response to be handled
    if(_sema_wait_timeout(&sema_resp_done, 100) != 0) {
      KIOSK_ERROR("kiosk response not handled after 100ms\n");
    }
    current_resp_len = 0;
    return;
  }

  // not a response, check for supported events

  //identify TransactionComplete event
  int evt_id = 0;
  if(parse_transaction_complete(data, data_len, &pmt_resp, &evt_id) == KIOSK_RET_OK) {
    // send ACK
    const char* ack_template = "{\"jsonrpc\": \"2.0\", \"result\": true, \"id\": %d}";
    char* ack = build_command(ack_template, evt_id);
    send_to_kiosk(ack, strlen(ack));
    free(ack);

    // call the application callback
    if(_trans_complete_app_cb != NULL)
      _trans_complete_app_cb(&pmt_resp);
    return;
  } else {
    KIOSK_ERROR("unexpected message received from kiosk: %.*s\n", data_len, data);
  }
}

bool LibOtiKiosk_Init(const char* server_address, bool is_local) {
  // initialize common socket params
  memset(&_commands_socket_options, 0, sizeof(_commands_socket_options));
  _commands_socket_options.is_tcp = !is_local;
  _commands_socket_options.incoming_timeout_ms = 1000;
  pthread_mutex_init(&_commands_socket_options.mutex, NULL);
  _commands_socket_options.sockfd = -1;
  _commands_socket_options.recv_cb = kiosk_msg_received;

  memset(&_reader_socket_options, 0, sizeof(_reader_socket_options));
  _reader_socket_options.is_tcp = !is_local;
  _reader_socket_options.incoming_timeout_ms = 1000;
  pthread_mutex_init(&_reader_socket_options.mutex, NULL);
  _reader_socket_options.sockfd = -1;
  _reader_socket_options.recv_cb = reader_event_received;

  KIOSK_INFO("initializing EmvCore Client Library "EMV_CORE_GIT_TAG"-"EMV_CORE_REV_COUNT"\n");

  if(is_local) {
    KIOSK_DEBUG("initializing for Unix domain sockets\n");
    // setting up for Unix domain sockets
    if(server_address == NULL || strlen(server_address) == 0) {
      KIOSK_DEBUG("no path provided, trying to get from OTI_KIOSK_SOCKET_DIR\n");
      // no socket path given, get it from environment
      char* env_val = getenv("OTI_KIOSK_SOCKET_DIR");
      if(env_val != NULL) {
        // it is defined, use it
        server_address = env_val;
      } else {
        KIOSK_DEBUG("OTI_KIOSK_SOCKET_DIR not defined, using './var'\n");
        // nothing to use, just go with the current directory
        server_address = "./var";
      }
    }

    // we have a base path, now build actual socket paths
    int len = snprintf(NULL, 0, "%s/socket_cmd", server_address)+1;
    _commands_socket_options.server_addr = calloc(len, 1);
    if(_commands_socket_options.server_addr == NULL) {
      KIOSK_ERROR("failed to allocate %d characters\n", len);
      return false;
    }
    snprintf(_commands_socket_options.server_addr, len, "%s/socket_cmd", server_address);

    len = snprintf(NULL, 0, "%s/socket_events", server_address)+1;
    _reader_socket_options.server_addr = calloc(len, 1);
    if(_reader_socket_options.server_addr == NULL) {
      KIOSK_ERROR("failed to allocate %d characters\n", len);
      return false;
    }
    snprintf(_reader_socket_options.server_addr, len, "%s/socket_events", server_address);
  } else {
    KIOSK_DEBUG("initializing for TCP sockets\n");
    // setting up for TCP sockets
    if(server_address == NULL || strlen(server_address) == 0) {
      KIOSK_DEBUG("no server address provided, using 127.0.0.1\n");
      // no server address provided, use localhost
      server_address = "127.0.0.1";
    }
    _commands_socket_options.server_addr = (char*)server_address;
    _commands_socket_options.tcp_port = 10000;
    _reader_socket_options.server_addr = (char*)server_address;
    _reader_socket_options.tcp_port = 10001;
  }

  return LibOtiKiosk_Init_Common();
}

void LibOtiKiosk_Register_TransactionComplete_Callback(TransactionCompleteCb_t cb) {
  _trans_complete_app_cb = cb;
}

void LibOtiKiosk_Register_ReaderEvent_Callback(RdrEventCb_t cb) {
  _reader_event_app_cb = cb;
}

void LibOtiKiosk_Enable_Debug_Logs(bool enabled) {
  oT_Log_Set_Module_Level("KIOSK", enabled ? e_OT_LOG_LEVEL_DEBUG : e_OT_LOG_LEVEL_INFO);
}

KIOSK_RET LibOtiKiosk_GetStatus(KIOSK_STATUS *out_status) {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  KIOSK_RET ret = KIOSK_RET_GENERAL_ERROR;
  *out_status = OK_NOT_READY;

  char* cmd = "{\"jsonrpc\": \"2.0\", \"method\": \"GetStatus\", \"params\": {}, \"id\": 1}";

  ret = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  if(ret != KIOSK_RET_OK) {
    return ret;
  }

  // parse response
  ret = parse_get_status(resp_buff, resp_len, 1, out_status);

  return ret;
}

KIOSK_RET LibOtiKiosk_ShowMessage(const char* line1, const char* line2) {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  const char* cmd_template = "{\"jsonrpc\": \"2.0\", \"method\": \"ShowMessage\", \"params\": {\"strLine1\":\"%s\", \"strLine2\":\"%s\"}, \"id\": 2}";
  char* cmd = build_command(cmd_template, line1, line2);
  if(cmd == NULL)
    return KIOSK_RET_MEMORY_ERROR;

  KIOSK_RET status = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  free(cmd);
  if(status != KIOSK_RET_OK) {
    return status;
  }

  // parse response
  return(check_response_ok(resp_buff, resp_len, 2));
}

KIOSK_RET LibOtiKiosk_GetKioskId(char* out_kiosk_id, int max_out_size) {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  KIOSK_RET ret = KIOSK_RET_GENERAL_ERROR;
  memset(out_kiosk_id, 0, max_out_size);

  char* cmd = "{\"jsonrpc\": \"2.0\", \"method\": \"GetKioskID\", \"params\": {}, \"id\": 3}";

  ret = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  if(ret != KIOSK_RET_OK) {
    return ret;
  }

  // parse response
  return(parse_resp_result(resp_buff, resp_len, 3, out_kiosk_id, max_out_size));
}

KIOSK_RET LibOtiKiosk_GetKioskVersion(char* out_kiosk_version, int max_out_size) {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  KIOSK_RET ret = KIOSK_RET_GENERAL_ERROR;
  memset(out_kiosk_version, 0, max_out_size);

  char* cmd = "{\"jsonrpc\": \"2.0\", \"method\": \"GetVersion\", \"params\": {\"SoftwareComponent\": \"otiKiosk\"}, \"id\": 4}";

  ret = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  if(ret != KIOSK_RET_OK) {
    return ret;
  }

  // parse response
  return(parse_resp_result(resp_buff, resp_len, 4, out_kiosk_version, max_out_size));
}

KIOSK_RET LibOtiKiosk_GetReaderVersion(char* out_version, int max_out_size) {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  KIOSK_RET ret = KIOSK_RET_GENERAL_ERROR;
  memset(out_version, 0, max_out_size);

  char* cmd = "{\"jsonrpc\": \"2.0\", \"method\": \"GetVersion\", \"params\": {\"SoftwareComponent\": \"Reader\"}, \"id\": 5}";

  ret = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  if(ret != KIOSK_RET_OK) {
    return ret;
  }

  // parse response
  return(parse_resp_result(resp_buff, resp_len, 5, out_version, max_out_size));
}

KIOSK_RET LibOtiKiosk_PreAuthorize(otiKioskPaymentParameters *params) {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  const char* cmd_template = "{\"jsonrpc\":\"2.0\",\"method\":\"PreAuthorize\", \"params\": {\"amount\":%d, \"currency\":%d, \"timeout\":%d, \"fee\":%d, \"productID\":%d, \"continuous\":%s}, \"id\":6}";
  char* cmd = build_command(cmd_template, params->amount_cents, params->currency_code, params->timeout_sec, params->fee_cents, params->product_id, params->continuous ? "true" : "false");
  if(cmd == NULL)
    return KIOSK_RET_MEMORY_ERROR;

  KIOSK_RET status = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  free(cmd);
  if(status != KIOSK_RET_OK) {
    return status;
  }

  // parse response
  return(check_response_ok(resp_buff, resp_len, 6));
}

KIOSK_RET LibOtiKiosk_PayTransaction(otiKioskPaymentParameters *params) {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  const char* cmd_template = "{\"jsonrpc\":\"2.0\",\"method\":\"PayTransaction\", \"params\": {\"amount\":%d, \"currency\":%d, \"timeout\":%d, \"fee\":%d, \"productID\":%d, \"continuous\":%s}, \"id\":7}";
  char* cmd = build_command(cmd_template, params->amount_cents, params->currency_code, params->timeout_sec, params->fee_cents, params->product_id, params->continuous ? "true" : "false");
  if(cmd == NULL)
    return KIOSK_RET_MEMORY_ERROR;

  KIOSK_RET status = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  free(cmd);
  if(status != KIOSK_RET_OK) {
    return status;
  }

  // parse response
  return(check_response_ok(resp_buff, resp_len, 6));
}

KIOSK_RET LibOtiKiosk_ConfirmTransaction(uint32_t amount_cents, uint32_t fee_cents, uint32_t product_id, char* transaction_reference) {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  const char* cmd_template = "{\"jsonrpc\":\"2.0\",\"method\":\"ConfirmTransaction\", \"params\": {\"amount\":%d, \"fee\":%d, \"productID\":%d, \"transaction_Reference\":\"%s\"}, \"id\":8}";
  char* cmd = build_command(cmd_template, amount_cents, fee_cents, product_id, transaction_reference);
  if(cmd == NULL)
    return KIOSK_RET_MEMORY_ERROR;

  KIOSK_RET status = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  free(cmd);
  if(status != KIOSK_RET_OK) {
    return status;
  }

  // parse response
  return(check_response_ok(resp_buff, resp_len, 8));
}

KIOSK_RET LibOtiKiosk_VoidTransaction(char* transaction_reference) {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  const char* cmd_template = "{\"jsonrpc\":\"2.0\",\"method\":\"VoidTransaction\", \"params\": {\"transaction_Reference\":\"%s\"}, \"id\":9}";
  char* cmd = build_command(cmd_template, transaction_reference);
  if(cmd == NULL)
    return KIOSK_RET_MEMORY_ERROR;

  KIOSK_RET status = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  free(cmd);
  if(status != KIOSK_RET_OK) {
    return status;
  }

  // parse response
  return(check_response_ok(resp_buff, resp_len, 9));
}

KIOSK_RET LibOtiKiosk_CancelTransaction() {
  char resp_buff[128] = "";
  int resp_len = sizeof(resp_buff);

  char* cmd = "{\"jsonrpc\":\"2.0\",\"method\":\"CancelTransaction\", \"params\": {}, \"id\":10}";

  KIOSK_RET status = send_receive(cmd, strlen(cmd), resp_buff, &resp_len, 500);
  if(status != KIOSK_RET_OK) {
    return status;
  }

  // parse response
  return(parse_cancel_resp(resp_buff, resp_len, 10));
}

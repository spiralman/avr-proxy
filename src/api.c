#include "os_type.h"
#include "osapi.h"
#include "user_interface.h"
#include "espmissingincludes.h"
#include "ip_addr.h"
#include "espconn.h"
#include "platform.h"
#include "espfs.h"
#include "httpd.h"
#include "httpdespfs.h"
#include "webpages-espfs.h"
#include "ssdp.h"

static struct espconn avrConn;
static esp_tcp avrTcp;

static os_timer_t connectPauseTimer;

static os_timer_t responseTimer;
static bool awaitingResponse = false;
static bool responseReady = false;
static char responseBuf[256];

/* Documented max command length */
static char cmd[135];
static int cmdLen;

static int cmdErr = ESPCONN_OK;

static ConnTypePtr openCmdConn = NULL;
static uint8 openCmdRemoteIp[4];
static int openCmdRemotePort;

#define OS_QUEUE_LEN 4
static os_event_t osQueue[OS_QUEUE_LEN];

#define OS_TASK_DISCONNECT 0


void ICACHE_FLASH_ATTR os_task_handler(os_event_t * evt) {
  switch (evt->sig) {
  case OS_TASK_DISCONNECT:
    espconn_disconnect(&avrConn);
    break;
  }
}

int ICACHE_FLASH_ATTR
ssdp_dev_template(HttpdConnData * connData, char * token, void ** arg) {
  if (token == NULL) { return HTTPD_CGI_DONE; }

  if (strcmp(token, "uuid") == 0) { httpdSend(connData, ssdp_uuid(), -1); }

  return HTTPD_CGI_DONE;
}

void ICACHE_FLASH_ATTR connect_pause_cb(void * arg) {
  os_timer_disarm(&connectPauseTimer);

  os_printf("reconnecting\n");
  espconn_connect(&avrConn);
}

void ICACHE_FLASH_ATTR response_done_cb(void * arg) {
  awaitingResponse = false;
  responseReady = true;
  os_timer_disarm(&responseTimer);

  /* Pretend like some data was sent, so libesphttpd calls the CGI
     callback again to respond asynchronously */
  httpdSentCb(openCmdConn, (char *)openCmdRemoteIp, openCmdRemotePort);

  openCmdConn = NULL;
}

void ICACHE_FLASH_ATTR cmd_sent_cb(void * arg) {
  os_printf("sent\n");
  os_timer_arm(&responseTimer, 200, false);
}

void ICACHE_FLASH_ATTR cmd_received_cb(void * arg, char * data, unsigned short len) {
  data[len-1] = '\n';
  os_printf("recieved %s\n", data);
  if (awaitingResponse) {
    strcat(responseBuf, data);
  }
}

void ICACHE_FLASH_ATTR cmd_connected_cb(void * arg) {
  os_printf("connected\n");
  espconn_send(&avrConn, (unsigned char *)cmd, cmdLen);
}

void ICACHE_FLASH_ATTR cmd_disconnected_cb(void * arg) {
  os_printf("disconnected\n");
  avrConn.state = ESPCONN_NONE;
}

void ICACHE_FLASH_ATTR cmd_reconnect_cb(void * arg, sint8 err) {
  os_printf("reconnect error: %d\n", err);

  /* Happens when we try to reconnect too quickly, if we get requests
     too close together; wait and try again */
  if (err == ESPCONN_RST) {
    os_timer_arm(&connectPauseTimer, 100, false);
  }
  else {
    awaitingResponse = false;
    responseReady = true;

    cmdErr = err;
    /* Pretend like some data was sent, so libesphttpd calls the CGI
       callback again to respond asynchronously */
    httpdSentCb(openCmdConn, (char *)openCmdRemoteIp, openCmdRemotePort);

    openCmdConn = NULL;
  }
}

int ICACHE_FLASH_ATTR send_command(HttpdConnData * connData) {
  if (!awaitingResponse && !responseReady) {

    if (connData->conn==NULL) {
      //Connection aborted. Clean up.
      return HTTPD_CGI_DONE;
    }

    if (connData->requestType != HTTPD_METHOD_GET) {
      httpdStartResponse(connData, 405);
      httpdEndHeaders(connData);
      return HTTPD_CGI_DONE;
    }

    cmdLen = httpdFindArg(connData->getArgs, "cmd", cmd, sizeof(cmd));
    if (cmdLen == -1) {
      httpdStartResponse(connData, 400);
      httpdEndHeaders(connData);
      return HTTPD_CGI_DONE;
    }

    cmd[cmdLen++] = '\r';
    cmd[cmdLen] = 0;

    os_printf("Queuing %s\n", cmd);

    os_printf("connecting\n");
    cmdErr = ESPCONN_OK;
    espconn_connect(&avrConn);

    openCmdConn = connData->conn;
    memcpy(openCmdRemoteIp, connData->remote_ip, 4*sizeof(uint8));
    openCmdRemotePort = connData->remote_port;

    awaitingResponse = true;
    return HTTPD_CGI_MORE;
  }
  else if(responseReady) {
    if (cmdErr == ESPCONN_OK) {
      os_printf("\nresponse:\n%s\n", responseBuf);
      httpdStartResponse(connData, 200);
      httpdEndHeaders(connData);

      httpdSend(connData, responseBuf, -1);
    }
    else {
      char errMessage[25];

      os_sprintf(errMessage, "Communication error: %d\n", cmdErr);
      httpdStartResponse(connData, 500);
      httpdEndHeaders(connData);

      httpdSend(connData, errMessage, -1);
    }

    responseBuf[0] = 0;
    responseReady = false;

    cmd[0] = 0;
    cmdLen = 0;
    system_os_post(USER_TASK_PRIO_0, OS_TASK_DISCONNECT, 0);

    return HTTPD_CGI_DONE;
  }
  else {
    os_printf(".");

    return HTTPD_CGI_MORE;
  }
}

int ICACHE_FLASH_ATTR hello_world(HttpdConnData * connData) {
  if (connData->conn == NULL) {
    return HTTPD_CGI_DONE;
  }

  if (connData->requestType!=HTTPD_METHOD_GET) {
    httpdStartResponse(connData, 405);
    httpdEndHeaders(connData);
    return HTTPD_CGI_DONE;
  }

  httpdStartResponse(connData, 200);
  httpdHeader(connData, "Content-Type", "text/plain");
  httpdEndHeaders(connData);

  httpdSend(connData, "Hello, world\n", -1);

  return HTTPD_CGI_DONE;
};

HttpdBuiltInUrl builtInUrls[]={
  {"/", hello_world, NULL},
  {"/ssdp/device_description.xml", cgiEspFsTemplate, ssdp_dev_template},
  {"/avr/command", send_command, NULL},
  {NULL, NULL, NULL}
};

void ICACHE_FLASH_ATTR api_init() {
  system_os_task(os_task_handler, USER_TASK_PRIO_0, osQueue, OS_QUEUE_LEN);

  espFsInit((void*)(webpages_espfs_start));
  httpdInit(builtInUrls, 80);

  os_timer_setfn(&responseTimer,
                 response_done_cb,
                 NULL);

  os_timer_setfn(&connectPauseTimer,
                 connect_pause_cb,
                 NULL);

  avrConn.type = ESPCONN_TCP;
  avrConn.state = ESPCONN_NONE;

  avrTcp.local_port = espconn_port();
  avrTcp.remote_port = 23;
  avrTcp.remote_ip[0] = 192;
  avrTcp.remote_ip[1] = 168;
  avrTcp.remote_ip[2] = 1;
  avrTcp.remote_ip[3] = 115;

  avrConn.proto.tcp = &avrTcp;

  espconn_regist_recvcb(&avrConn, cmd_received_cb);
  espconn_regist_sentcb(&avrConn, cmd_sent_cb);
  espconn_regist_connectcb(&avrConn, cmd_connected_cb);
  espconn_regist_disconcb(&avrConn, cmd_disconnected_cb);
  espconn_regist_reconcb(&avrConn, cmd_reconnect_cb);
};

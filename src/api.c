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

static bool avrConnected = false;
static int avrConnectTime = 0;

static os_timer_t responseTimer;
static bool awaitingResponse = false;
static bool responseReady = false;
static char responseBuf[256];

int ICACHE_FLASH_ATTR
ssdp_dev_template(HttpdConnData * connData, char * token, void ** arg) {
  if (token == NULL) { return HTTPD_CGI_DONE; }

  if (strcmp(token, "uuid") == 0) { httpdSend(connData, ssdp_uuid(), -1); }

  return HTTPD_CGI_DONE;
}

void ICACHE_FLASH_ATTR response_done_cb(void * arg) {
  awaitingResponse = false;
  responseReady = true;
  os_timer_disarm(&responseTimer);
}

void ICACHE_FLASH_ATTR cmd_sent_cb(void * arg) {
  os_printf("sent\n");
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
  avrConnected = true;
  avrConnectTime = system_get_time();
}

void ICACHE_FLASH_ATTR cmd_disconnected_cb(void * arg) {
  os_printf("disconnected\n");
  avrConnected = false;
}

void ICACHE_FLASH_ATTR cmd_reconnect_cb(void * arg, sint8 err) {
  os_printf("reconnect: %d\n", err);

}

int ICACHE_FLASH_ATTR send_command(HttpdConnData * connData) {
  if (!awaitingResponse && !responseReady) {
    /* Documented max command length */
    char cmd[135];
    int cmdLen;

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

    os_printf("Sending %s\n", cmd);

    espconn_send(&avrConn, (unsigned char *)cmd, cmdLen);
    os_timer_arm(&responseTimer, 200, false);

    httpdStartResponse(connData, 200);
    httpdEndHeaders(connData);

    awaitingResponse = true;
    return HTTPD_CGI_MORE;
  }
  else if(responseReady) {
    os_printf("\nresponse:\n%s\n", responseBuf);
    httpdSend(connData, responseBuf, -1);
    responseBuf[0] = 0;
    responseReady = false;

    os_printf("sent\n");
    return HTTPD_CGI_DONE;
  }
  else {
    os_printf(".");
    httpdSend(connData, "\n", 1);
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

void ICACHE_FLASH_ATTR api_wifi_status_cb(System_Event_t * evt) {
  if (evt->event == EVENT_STAMODE_GOT_IP) {
    os_printf("connecting\n");
    espconn_connect(&avrConn);
  }
}

void ICACHE_FLASH_ATTR api_init() {
  espFsInit((void*)(webpages_espfs_start));
  httpdInit(builtInUrls, 80);

  os_timer_setfn(&responseTimer,
                 response_done_cb,
                 NULL);

  /* TODO: open and close connection dynamically, so we're not sitting
     on it while idle */
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

  wifi_set_event_handler_cb(api_wifi_status_cb);
};

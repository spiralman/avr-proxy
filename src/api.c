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

int ICACHE_FLASH_ATTR
ssdp_dev_template(HttpdConnData * connData, char * token, void ** arg) {
  if (token == NULL) { return HTTPD_CGI_DONE; }

  if (strcmp(token, "uuid") == 0) { httpdSend(connData, ssdp_uuid(), -1); }

  return HTTPD_CGI_DONE;
}

void ICACHE_FLASH_ATTR cmd_sent_cb(void * arg) {
  os_printf("sent\n");
}

void ICACHE_FLASH_ATTR cmd_received_cb(void * arg, char * data, unsigned short len) {
  os_printf("recieved %s\n", data);
}

void ICACHE_FLASH_ATTR cmd_connected_cb(void * arg) {
  os_printf("connected\n");
}

int ICACHE_FLASH_ATTR send_command(HttpdConnData * connData) {
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
  return HTTPD_CGI_DONE;
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
    espconn_connect(&avrConn);
  }
}

void ICACHE_FLASH_ATTR api_init() {
  espFsInit((void*)(webpages_espfs_start));
  httpdInit(builtInUrls, 80);

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

  wifi_set_event_handler_cb(api_wifi_status_cb);
};

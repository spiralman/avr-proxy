#include "os_type.h"
#include "osapi.h"
#include "espmissingincludes.h"
#include "platform.h"
#include "espfs.h"
#include "httpd.h"
#include "httpdespfs.h"
#include "webpages-espfs.h"
#include "ssdp.h"

int ICACHE_FLASH_ATTR
ssdp_dev_template(HttpdConnData * connData, char * token, void ** arg) {
  if (token == NULL) { return HTTPD_CGI_DONE; }

  if (strcmp(token, "uuid") == 0) { httpdSend(connData, ssdp_uuid(), -1); }

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
  {NULL, NULL, NULL}
};

void ICACHE_FLASH_ATTR api_init() {
  espFsInit((void*)(webpages_espfs_start));
  httpdInit(builtInUrls, 80);
};

#include "os_type.h"
#include "espmissingincludes.h"
#include "platform.h"
#include "httpd.h"

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
  {"/", hello_world, NULL}
};

void ICACHE_FLASH_ATTR api_init() {
    httpdInit(builtInUrls, 80);
};

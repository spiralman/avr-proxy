#include "user_interface.h"
#include "osapi.h"
#include "espmissingincludes.h"

#include "wifi.h"


int ICACHE_FLASH_ATTR wifi_init() {
  struct station_config station_conf;

  char ssid[32] = WIFI_SSID;
  char password[64] = WIFI_PASS;

  //Set station mode
  wifi_set_opmode( 0x1 );

  //Set ap settings
  os_memcpy(&station_conf.ssid, ssid, 32);
  os_memcpy(&station_conf.password, password, 64);

  return wifi_station_set_config(&station_conf);
}

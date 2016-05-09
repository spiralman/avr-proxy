#include "os_type.h"
#include "osapi.h"
#include "user_interface.h"
#include "espmissingincludes.h"
#include "ip_addr.h"
#include "espconn.h"
#include "ssdp.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

static struct espconn ssdpConn;
static esp_udp ssdpUdp;

static char * HEX = "0123456789ABCDEF";

static char * ssdpURN = "urn:schemas-upnp-org:device:AVRProxy:1";
static char * uuid = "XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX";

static bool respondingToSearch = false;
static os_timer_t searchResponseTimer;

void ICACHE_FLASH_ATTR respond_to_search(void * arg) {
  os_printf("Responding to M-SEARCH\n");
  os_timer_disarm(&searchResponseTimer);
  respondingToSearch = false;
}

void ICACHE_FLASH_ATTR ssdp_multicast_recv(void * arg, char * pdata, unsigned short len) {
  if (strncmp("M-SEARCH * HTTP/1.1", pdata, MIN(len, 19)) == 0) {
    os_printf("Received SSDP Search\n");
    int mx = 0;
    char * search = NULL;
    remot_info *remote = NULL;

    char * line = strtok(&pdata[21], "\r\n");
    while (line != NULL) {
      switch(line[0]) {
      case 'M':
        if (line[1] == 'X' && line[2] == ':') {
          mx = atoi(&line[4]);
        }
        break;
      case 'S':
        search = &line[4];
        break;
      }
      line = strtok(NULL, "\r\n");
    }

    if (mx == 0 || search == NULL) {
      os_printf("Invalid M-SEARCH\n");
      return;
    }

    if (espconn_get_connection_info(&ssdpConn, &remote, 0) != ESPCONN_OK) {
      os_printf("Couldn't get connection info from M-SEARCH\n");
      return;
    }

    if (strcmp(search, "ssdp:all") != 0
        && (strstr(search, "uuid:") != 0 || strcmp(&search[5], uuid) !=0)
        && strcmp(search, ssdpURN) != 0) {
      os_printf("No match for search: %s\n", search);
      return;
    }

    os_printf("Got matching M-SEARCH: MX: %d ST: %s from %d.%d.%d.%d at %d\n",
              mx,
              search,
              remote->remote_ip[0],
              remote->remote_ip[1],
              remote->remote_ip[2],
              remote->remote_ip[3],
              remote->remote_port);

    if (respondingToSearch) {
      os_printf("Already responding to M-SEARCH; ignoring\n");
      return;
    }

    respondingToSearch = true;

    int sleepTime = os_random() % (mx * 1000);
    os_printf("Waiting %d ms to respond\n", sleepTime);

    os_timer_setfn(&searchResponseTimer, respond_to_search, NULL);
    os_timer_arm(&searchResponseTimer, sleepTime, false);
  }
}

void wifi_status_cb(System_Event_t * evt) {
  if (evt->event == EVENT_STAMODE_GOT_IP) {
    ip_addr_t remoteIP;
    IP4_ADDR(&remoteIP, 239, 255, 255, 250);

    espconn_igmp_join(&evt->event_info.got_ip.ip, &remoteIP);
  }
}

void ICACHE_FLASH_ATTR ssdp_init() {
  uint8 macAddr[6];
  int uuidLen = strlen(uuid);

  wifi_get_macaddr(STATION_IF, macAddr);
  os_printf("MAC Address: %x:%x:%x:%x:%x:%x\n",
            macAddr[0],
            macAddr[1],
            macAddr[2],
            macAddr[3],
            macAddr[4],
            macAddr[5]);

  /* Swizzle the MAC into the UUID, using all the bits of the MAC */
  for (int i = 0; i < uuidLen; i++) {
    if (uuid[i] == 'X') {
      uint8 macOctet = macAddr[i%6];
      if (i % 2 == 0) {
        macOctet >>= 4;
      }

      macOctet &= 0xF;

      uuid[i] = HEX[macOctet];
    }
  }

  os_printf("SSDP UUID: %s\n", uuid);

  ssdpConn.type = ESPCONN_UDP;
  ssdpConn.state = ESPCONN_NONE;

  ssdpUdp.local_port = 1900;
  ssdpUdp.remote_ip[0] = 239;
  ssdpUdp.remote_ip[1] = 255;
  ssdpUdp.remote_ip[2] = 255;
  ssdpUdp.remote_ip[3] = 250;

  ssdpConn.proto.udp = &ssdpUdp;

  espconn_regist_recvcb(&ssdpConn, ssdp_multicast_recv);
  espconn_create(&ssdpConn);

  wifi_set_event_handler_cb(wifi_status_cb);
}

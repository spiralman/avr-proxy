#include "os_type.h"
#include "osapi.h"
#include "user_interface.h"
#include "espmissingincludes.h"
#include "ip_addr.h"
#include "espconn.h"
#include "mem.h"
#include "ssdp.h"

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

typedef struct _msearch_response_t {
  uint8 ipaddr[4];
  int port;
  os_timer_t responseTimer;
  char * searchTerm;
} msearch_response_t;

static struct espconn ssdpConn;
static esp_udp ssdpUdp;

static char * HEX = "0123456789ABCDEF";

static char * ssdpURN = "urn:schemas-upnp-org:device:AVRProxy:1";
static char * uuid = "uuid:XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX";
int uuidLen = 0;

int cacheAge = 86400;

static char * responseStart =
  "HTTP/1.1 200 OK\r\n"
  "CACHE-CONTROL: max-age=86400\r\n"
  "EXT:\r\n"
  "LOCATION: ";
static int responseStartLen = 61;

void _response_sent(void * arg) {
  os_printf("Response sent");
}

void send_response(msearch_response_t * response, char * st) {
  esp_udp responseUDP;
  espconn responseConn;

  unsigned char * buf = (unsigned char *)os_malloc(1024);
  uint16 pos = 0;

  responseConn.type = ESPCONN_UDP;
  responseConn.state = ESPCONN_NONE;

  os_memcpy(responseUDP.remote_ip, response->ipaddr, 4*sizeof(uint8));
  responseUDP.remote_port = response->port;

  responseConn.proto.udp = &responseUDP;

  memcpy(buf, responseStart, responseStartLen);
  pos += responseStartLen;

  /* TODO: We need the device IP address */
  memcpy(&buf[pos], "http://foobar/ssdp/device_description.xml\r\n", 43);
  pos += 43;

  memcpy(&buf[pos], "SERVER: nonos/1.0 UPnP/1.1 AVRProxy/1.0\r\n", 41);
  pos += 41;

  memcpy(&buf[pos], "ST: ", 4);
  pos += 4;

  int stLen = strlen(st);
  memcpy(&buf[pos], st, stLen);
  pos += stLen;

  memcpy(&buf[pos], "\r\n", 2);
  pos += 2;

  memcpy(&buf[pos], "USN: ", 5);
  pos += 5;

  memcpy(&buf[pos], uuid, uuidLen);
  pos += uuidLen;

  memcpy(&buf[pos], "::", 2);
  pos += 2;

  int urnLen = strlen(ssdpURN);
  memcpy(&buf[pos], ssdpURN, urnLen);
  pos += urnLen;

  memcpy(&buf[pos], "\r\nBOOTID.UPNP.ORG: 1\r\n\r\n", 22);
  pos += 22;

  buf[pos] = 0;

  espconn_create(&responseConn);
  /* Doesn't work without this (maybe need to mem-set 0 the conn struct?) */
  espconn_regist_sentcb(&responseConn, _response_sent);

  espconn_send(&responseConn, buf, pos);

  os_free(buf);
}

void ICACHE_FLASH_ATTR respond_to_search(void * arg) {
  msearch_response_t * response = (msearch_response_t *)arg;

  os_printf("Responding to M-SEARCH at %d.%d.%d.%d:%d\n",
            response->ipaddr[0],
            response->ipaddr[1],
            response->ipaddr[2],
            response->ipaddr[3],
            response->port);

  os_timer_disarm(&(response->responseTimer));

  if (strcmp(response->searchTerm, uuid) == 0) {
    send_response(response, uuid);
  }
  else if (strcmp(response->searchTerm, ssdpURN) == 0) {
    send_response(response, ssdpURN);
  }
  else if (strcmp(response->searchTerm, "upnp:rootdevice") == 0) {
    send_response(response, "upnp:rootdevice");
  }
  else if (strcmp(response->searchTerm, "ssdp:all") == 0) {
    send_response(response, "upnp:rootdevice");
    send_response(response, uuid);
    send_response(response, ssdpURN);
  }

  os_free(response->searchTerm);
  os_free(response);
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
        && strcmp(search, uuid) !=0
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

    msearch_response_t * response =
      (msearch_response_t *)os_malloc(sizeof(msearch_response_t));

    int searchLen = strlen(search) + 1;

    response->searchTerm = os_malloc(searchLen);
    os_memcpy(response->searchTerm, search, searchLen);

    os_memcpy(response->ipaddr, remote->remote_ip, 4*sizeof(uint8));
    response->port = remote->remote_port;

    int sleepTime = os_random() % (mx * 1000);
    os_printf("Waiting %d ms to respond\n", sleepTime);

    os_timer_setfn(&(response->responseTimer),
                   respond_to_search,
                   response);
    os_timer_arm(&(response->responseTimer), sleepTime, false);
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
  uuidLen = strlen(uuid);

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

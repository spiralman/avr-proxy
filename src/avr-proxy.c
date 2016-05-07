#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_interface.h"

#include "espmissingincludes.h"

#include "wifi.h"

static const int pin = 5;
static os_timer_t some_timer;

void some_timerfunc(void *arg)
{
  uint8 wifi_status = wifi_station_get_connect_status();

  os_printf("Wifi Status: %d\n", wifi_status);

  //Do blinky stuff
  if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & (1 << pin))
  {
    // set gpio low
    gpio_output_set(0, (1 << pin), 0, 0);
  }
  else
  {
    // set gpio high
    gpio_output_set((1 << pin), 0, 0, 0);
  }
}

void ICACHE_FLASH_ATTR user_init()
{
  wifi_init();

  // init gpio sussytem
  gpio_init();

  uart_div_modify( 0, UART_CLK_FREQ / ( 921600 ) );

  gpio_output_set(0, 0, (1 << pin), 0);

  os_printf("Running");

  // setup timer (500ms, repeating)
  os_timer_setfn(&some_timer, (os_timer_func_t *)some_timerfunc, NULL);
  os_timer_arm(&some_timer, 1000, 1);
}

#include <stdio.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "soc/rtc_cntl_reg.h"
#include "soc/sens_reg.h"
#include "soc/rtc_periph.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_sleep.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "pulse_counter.h"
#include "wifi_client.h"

int init_pin(int gpio_num)
{
    int rtcio_num = rtc_io_number_get(gpio_num);
    assert(rtc_gpio_is_valid_gpio(gpio_num) && "GPIO used for pulse counting must be an RTC IO");

    /* Initialize selected GPIO as RTC IO, enable input, disable pullup and pulldown */
    rtc_gpio_init(gpio_num);
    rtc_gpio_set_direction(gpio_num, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(gpio_num);
    // rtc_gpio_pullup_en(gpio_num);
    if (gpio_num == GPIO_NUM_27) {
      rtc_gpio_pullup_en(gpio_num);
    } else {
      rtc_gpio_pullup_dis(gpio_num);
    }
    rtc_gpio_hold_en(gpio_num);

    return rtcio_num;
}

void reset_nvs(uint32_t rate_1) {
    reset_nvs_2(rate_1, 0);
}

// only call to initialise NVS to meter reading or zero
void reset_nvs_2(uint32_t rate_1, uint32_t rate_2) {
  const char* namespace = "plusecnt";

  nvs_handle_t handle;
  nvs_flash_init();
  ESP_ERROR_CHECK(nvs_open(namespace, NVS_READWRITE, &handle));
  ESP_ERROR_CHECK(nvs_set_u32(handle, "rate_1", rate_1));
  ESP_ERROR_CHECK(nvs_set_u32(handle, "rate_2", rate_2));
  ESP_ERROR_CHECK(nvs_set_u32(handle, "count", rate_1 + rate_2));
  ESP_ERROR_CHECK(nvs_commit(handle));
  nvs_close(handle);
}


void update_pulse_count(char* topic, uint32_t* edge_count,
        uint32_t* pulse_count, uint32_t* pulse_count_total)
{
    const char* namespace = "plusecnt";
    const char* total_key = "count";

    nvs_handle_t handle;
    ESP_ERROR_CHECK( nvs_open(namespace, NVS_READWRITE, &handle));
    esp_err_t err = nvs_get_u32(handle, topic, pulse_count);
    assert(err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);
    err = nvs_get_u32(handle, total_key, pulse_count_total);
    assert(err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND);
    printf("Read pulse count from NVS: %5d, %5d\n", *pulse_count, *pulse_count_total);

    /* ULP program counts signal edges, convert that to the number of pulses */
    uint32_t pulse_count_from_ulp = (*edge_count & UINT16_MAX) / 2;
    /* In case of an odd number of edges, keep one until next time */
    *edge_count = *edge_count % 2;
    printf("Pulse count from ULP: %5d\n", pulse_count_from_ulp);

    /* Save the new pulse count to NVS */
    *pulse_count = *pulse_count + pulse_count_from_ulp;
    ESP_ERROR_CHECK(nvs_set_u32(handle, topic, *pulse_count));
    *pulse_count_total = *pulse_count_total + pulse_count_from_ulp;
    ESP_ERROR_CHECK(nvs_set_u32(handle, total_key, *pulse_count_total));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
    printf("Wrote updated pulse count to NVS: %5d, %5d\n", *pulse_count, *pulse_count_total);
}

void pulse_count_main(void init_ulp_program(void), void on_wifi_connect(void)) {
  // flash LED
  gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
  gpio_set_level(GPIO_NUM_2, 1);

  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
  if (cause != ESP_SLEEP_WAKEUP_ULP) {
      printf("Not ULP wakeup, initializing ULP\n");
      init_ulp_program();

      /* Disconnect GPIO12 and GPIO15 to remove current drain through
       * pullup/pulldown resistors.
       * GPIO12 may be pulled high to select flash voltage.
       */
      rtc_gpio_isolate(GPIO_NUM_12);
      rtc_gpio_isolate(GPIO_NUM_15);
      esp_deep_sleep_disable_rom_logging(); // suppress boot messages
      vTaskDelay(pdMS_TO_TICKS(500));
  } else {
      printf("ULP wakeup, saving pulse count\n");

      //Initialize NVS
      esp_err_t ret = nvs_flash_init();
      if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
      }
      ESP_ERROR_CHECK(ret);

      // connect to wifi
      ESP_ERROR_CHECK(esp_netif_init());
      wifi_init_sta(on_wifi_connect);
  }

  // unflash LED
  gpio_set_level(GPIO_NUM_2, 0);

  printf("Entering deep sleep\n\n");
  ESP_ERROR_CHECK( esp_sleep_enable_ulp_wakeup() );
  esp_deep_sleep_start();
}

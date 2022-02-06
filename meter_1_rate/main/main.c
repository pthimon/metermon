/*
   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

#include "ulp_main.h"
#include "esp32/ulp.h"
#include "driver/gpio.h"

#include "mqtt_client.h"

#include "battery.h"
#include "mqtt_connect.h"
#include "pulse_counter.h"
#include "wifi_client.h"

extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");

static void init_ulp_program(void);

static float battery_voltage;

#if CONFIG_RESET_NVS
#define RESET_NVS true
#else
#define RESET_NVS false
#endif

#if CONFIG_USE_BUTTON_DEBUG
#define USE_BUTTON_DEBUG true
#else
#define USE_BUTTON_DEBUG false
#endif


void on_mqtt_connect(esp_mqtt_client_handle_t mqtt_client) {
  uint32_t pulse_count = 0;
  uint32_t pulse_count_total = 0;
  update_pulse_count("rate_1", &ulp_edge_count, &pulse_count, &pulse_count_total);

  char pulse_count_str[50];
  sprintf(pulse_count_str, "%u", pulse_count);
  char total_topic_str[50];
  sprintf(total_topic_str, "%s/total", CONFIG_METER_NAME);
  esp_mqtt_client_publish(mqtt_client, total_topic_str, pulse_count_str, 0, 1, 0);
  printf("Publishing %s to %s/total\n", pulse_count_str, CONFIG_METER_NAME);

  char battery_str[50];
  sprintf(battery_str, "%4.3f", battery_voltage);
  char battery_topic_str[50];
  sprintf(battery_topic_str, "%s/battery", CONFIG_METER_NAME);
  esp_mqtt_client_publish(mqtt_client, battery_topic_str, battery_str, 0, 1, 0);
}

void on_wifi_connect(void) {
    mqtt_publish(on_mqtt_connect);
}

void app_main(void) {
    // read the battery volatage and go into deep sleep if below critical voltage
    battery_voltage = check_battery();
    if (RESET_NVS) {
        // recompile to reset the stored counter if necessary
        reset_nvs(CONFIG_RESET_NVS_VALUE);
        printf("Reset NVS values to %d %d", CONFIG_RESET_NVS_VALUE);
    } else {
        pulse_count_main(init_ulp_program, on_wifi_connect);
    }
}

static void init_ulp_program(void) {
    esp_err_t err = ulp_load_binary(0, ulp_main_bin_start,
            (ulp_main_bin_end - ulp_main_bin_start) / sizeof(uint32_t));
    ESP_ERROR_CHECK(err);

    /* GPIO used for pulse counting. */
    gpio_num_t gpio_num;
    if (USE_BUTTON_DEBUG) {
        gpio_num = GPIO_NUM_27;  // button
    } else {
        gpio_num = GPIO_NUM_35;  // A3
    }

    int rtcio_num = init_pin(gpio_num);

    /* Initialize some variables used by ULP program.
     * Each 'ulp_xyz' variable corresponds to 'xyz' variable in the ULP program.
     * These variables are declared in an auto generated header file,
     * 'ulp_main.h', name of this file is defined in component.mk as ULP_APP_NAME.
     * These variables are located in RTC_SLOW_MEM and can be accessed both by the
     * ULP and the main CPUs.
     *
     * Note that the ULP reads only the lower 16 bits of these variables.
     */
    ulp_debounce_counter = 3;
    ulp_debounce_max_count = 3;
    ulp_next_edge = 0;
    ulp_io_number = rtcio_num; /* map from GPIO# to RTC_IO# */
    if (USE_BUTTON_DEBUG) {
        // wake up after each button press (2 edge counts)
        ulp_edge_count_to_wake_up = 2;
    } else {
        // wake up after 10 pulses (e.g. 0.01 kWh or 0.01 m3)
        ulp_edge_count_to_wake_up = 20;
    }

    /* Set ULP wake up period to T = 20ms.
     * Minimum pulse width has to be T * (ulp_debounce_counter + 1) = 80ms.
     */
    ulp_set_wakeup_period(0, 20000);

    /* Start the program */
    err = ulp_run(&ulp_entry - RTC_SLOW_MEM);
    ESP_ERROR_CHECK(err);
}

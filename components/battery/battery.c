#include "esp_system.h"
#include "esp_sleep.h"
#include "esp_adc_cal.h"

#include "battery.h"

#define LOW_BATTERY_VOLTAGE 3.20
#define VERY_LOW_BATTERY_VOLTAGE 3.10
#define CRITICALLY_LOW_BATTERY_VOLTAGE 3.00

/******************************************************************************
Description.: reads the battery voltage through the voltage divider at GPIO34
              if the ESP32-E has calibration eFused those will be used.
              In comparison with a regular voltmeter the values of ESP32 and
              multimeter differ only about 0.05V
Input Value.: -
Return Value: battery voltage in volts
******************************************************************************/
float read_battery() {
  uint32_t value = 0;
  int rounds = 11;
  esp_adc_cal_characteristics_t adc_chars;

  //battery voltage divided by 2 can be measured at GPIO34, which equals ADC1_CHANNEL6
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11);
  switch(esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars)) {
    case ESP_ADC_CAL_VAL_EFUSE_TP:
      printf("Characterized using Two Point Value");
      break;
    case ESP_ADC_CAL_VAL_EFUSE_VREF:
      printf("Characterized using eFuse Vref (%d mV)\r\n", adc_chars.vref);
      break;
    default:
      printf("Characterized using Default Vref (%d mV)\r\n", 1100);
  }

  //to avoid noise, sample the pin several times and average the result
  for(int i=1; i<=rounds; i++) {
    value += adc1_get_raw(ADC1_CHANNEL_6);
  }
  value /= (uint32_t)rounds;

  //due to the voltage divider (1M+1M) values must be multiplied by 2
  //and convert mV to V
  return esp_adc_cal_raw_to_voltage(value, &adc_chars)*2.0/1000.0;
}

float check_battery() {
  float battery_voltage = read_battery();
  printf("Battery voltage %f", battery_voltage);

  if (battery_voltage < CRITICALLY_LOW_BATTERY_VOLTAGE) {
      printf("Battery below critical level, switching off...");
      //switch off everything that might consume power
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_OFF);
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_OFF);
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
      esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);
      esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
      // never wake up until reset
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
      esp_deep_sleep_start();
  }

  return battery_voltage;
}

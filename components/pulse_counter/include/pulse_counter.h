
int init_pin(int gpio_num);

void reset_nvs(uint32_t rate_1);
void reset_nvs_2(uint32_t rate_1, uint32_t rate_2);

void update_pulse_count(char* topic, uint32_t* edge_count,
        uint32_t* pulse_count, uint32_t* pulse_count_total);

void pulse_count_main(void init_ulp_program(void), void on_wifi_connect(void));

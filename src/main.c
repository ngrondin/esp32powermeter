#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "soc/rtc.h"

#define rtc_clock 150000ULL
#define uS_TO_S_FACTOR 1000000ULL
#define period_in_seconds 60

#define WAKEPIN GPIO_NUM_25

RTC_DATA_ATTR int flash_count = 0;
RTC_DATA_ATTR uint32_t last_send = 0;
RTC_DATA_ATTR uint32_t next_send = 0;

static uint8_t broadcast_mac[ESP_NOW_ETH_ALEN] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };



uint32_t get_seconds() {
    uint64_t ticks = rtc_time_get();
    uint32_t secs = ticks / rtc_clock;
    return secs;
}

void setup_wifi() {
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( nvs_flash_init() );
    ESP_ERROR_CHECK( esp_netif_init() );
    
    ESP_ERROR_CHECK( esp_event_loop_create_default() );
    ESP_ERROR_CHECK( esp_wifi_init(&init_cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_country_code("AU", true) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    ESP_ERROR_CHECK( esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE) );
    
    printf("Wifi initiated\n");

    ESP_ERROR_CHECK( esp_now_init() );
    esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
    memset(peer, 0, sizeof(esp_now_peer_info_t));
    peer->channel = 1;
    peer->ifidx = ESP_IF_WIFI_STA;
    peer->encrypt = false;
    memcpy(peer->peer_addr, broadcast_mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK( esp_now_add_peer(peer) );
    free(peer);

    printf("ESP Now initiated\n");
}

void shutdown_wifi() {
    ESP_ERROR_CHECK( esp_wifi_stop() );
    ESP_ERROR_CHECK( esp_event_loop_delete_default() );
}

void send_data() {
    uint32_t now = get_seconds();
    uint32_t seconds_since_last = now - last_send;
    double kWh = flash_count / 1000.0;
    double kW = flash_count * 3.6 / seconds_since_last;
    setup_wifi();

    char *msg = (char*)malloc(100 * sizeof(char));
    sprintf(msg, "{\"t\":%d, \"kWh\":%.3f, \"kW\": %.3f}", now, kWh, kW);
    int slen = strlen(msg);
    ESP_ERROR_CHECK( esp_now_send(broadcast_mac, (uint8_t *)msg, slen) );
    free(msg);

    shutdown_wifi();
    flash_count = 0;
    last_send = now;
    next_send = now + period_in_seconds;
}

void received_flash() {
    flash_count++;
    printf("Flash\n");
}

void timer_wakeup() {
    printf("Timer\n");
}

void just_powered_on() {
    next_send = get_seconds() + period_in_seconds;
    printf("Powered on\n");
}

void app_main() {
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    switch(wakeup_reason) {
        case ESP_SLEEP_WAKEUP_EXT0: received_flash(); break;
        case ESP_SLEEP_WAKEUP_TIMER: timer_wakeup(); break;
        default: just_powered_on(); break;
    }

    uint32_t now = get_seconds();
    if(next_send <= now + 1) {
        send_data();
    }

    while(wakeup_reason == ESP_SLEEP_WAKEUP_EXT0 && gpio_get_level(WAKEPIN) == 1) {
        vTaskDelay(200 / portTICK_RATE_MS);
    }

    esp_sleep_enable_ext0_wakeup(WAKEPIN, 1);
    esp_sleep_enable_timer_wakeup((next_send - now) * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
}
/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "AP/AP.h"
#include "HTTP_Server/HTTP_Server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "UART/UART.h"
#include "NVS/NVS.h"
#include "ESP_NOW/ESP_NOW.h"
#include "rom/ets_sys.h"
#include "esp_timer.h"

extern "C"
{
    void app_main(void);
}

void app_main(void)
{
    NVS *nvs = new NVS();
    AP *ap = new AP("Detector", "123456789");
    SharedData *data = new SharedData();
    HTTP_Server *server = new HTTP_Server(data, nvs);
    server->init();
    uint8_t array[6] = {0};
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1};
    UART *uart = new UART(UART_NUM_1, &cfg, 21, 20, -1, -1, 256);
    uart->init();

    ESP_NOW *now = new ESP_NOW();
    uint8_t mac[6];
    cJSON *dev_cfg = nvs->getConfig();
    printf("config => %s\n\r", cJSON_PrintUnformatted(dev_cfg));
    strToMAC(cJSON_GetObjectItem(dev_cfg, "MAC")->valuestring, 17, mac, 6);
    float treshold = atof(cJSON_GetObjectItem(dev_cfg, "treshold")->valuestring);
    printf("%0.2f\n\r", treshold);

    now->addPeer(mac);
    now->send_message(mac, (uint8_t *)"HH", 2);

    memset(mac, 0xff, 6);
    now->addPeer(mac);
    now->send_message(mac, (uint8_t *)"HH", 2);

    int64_t start = esp_timer_get_time();
    int64_t stop = esp_timer_get_time();
    bool active = true;
    while (true)
    {
        vTaskDelay(300 / portTICK_PERIOD_MS);
        uart->recieveString(array, 6, 300);
        printf("%x %x %x %x %x %x\n\r", array[0], array[1], array[2], array[3], array[4], array[5]);
        float actDB = ((int32_t)(((array[0] == 0xbb && array[1] == 0xaa) ? (array[3] + (array[4] << 8)) : 0) * 10)) / 100.;
        printf("decibels = %0.2f\n\r", ((array[0] == 0xbb && array[1] == 0xaa) ? (array[3] + (array[4] << 8)) * 0.1 : 0));
        data->set_dB(actDB);
        memset(array, 0, 6);
        if (actDB > treshold)
        {
            uint64_t total = nvs->getTotalyOver();
            nvs->saveTotalyOver(total+1);
            data->increment();
            now->send_message(mac, (uint8_t *)"Over", 2);
        }
        if (active)
        {
            if (((esp_timer_get_time() - start) / 1000) > 60000)
            {
                ap->stop();
                stop = esp_timer_get_time();
                active = false;
            }
        }
        else
        {
            if ((esp_timer_get_time() - stop) / 1000 > 240000)
            {
                ap->start();
                start = esp_timer_get_time();
                active = true;
            }
        }
    }
}

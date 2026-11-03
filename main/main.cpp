/*
 * SPDX-FileCopyrightText: 2010-2022 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lwip/inet.h"
#include "AP/AP.h"
#include "DNS_Server/DNS_Server.h"
#include "ESP_NOW/ESP_NOW.h"
#include "HTTP_Server/HTTP_Server.h"
#include "NVS/NVS.h"
#include "Shared_data/SharedData.h"
#include "UART/UART.h"

static const char *TAG = "main";
static constexpr int64_t PORTAL_WINDOW_US = 5LL * 60 * 1000000;

extern "C"
{
    void app_main(void);
}

void app_main(void)
{
    NVS *nvs = new NVS();
    SharedData *data = new SharedData();
    data->setOverTotal(nvs->getTotalyOver());

    Settings settings;
    cJSON *stored = nvs->getConfig();
    if (!parseSettings(stored, settings))
    {
        ESP_LOGW(TAG, "Stored settings contain invalid values, ignoring those");
    }
    cJSON_Delete(stored);
    data->setSettings(settings);

    AP *ap = new AP("Detector", "123456789");
    ESP_NOW *now = new ESP_NOW(); // runs beside the AP, on the same channel
    now->setGenerator(settings.hasGenerator ? settings.generator : nullptr);

    DNS_Server *dns = new DNS_Server(inet_addr(AP_IP_STR));
    dns->start();
    HTTP_Server *server = new HTTP_Server(data, nvs, now, ap);
    server->init();
    data->setPortalDeadline(esp_timer_get_time() + PORTAL_WINDOW_US);

    uart_config_t cfg = {};
    cfg.baud_rate = 115200;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    UART *uart = new UART(UART_NUM_1, &cfg, 21, 20, -1, -1, 256);
    uart->init();

    bool portalOpen = true;
    uint8_t frame[6] = {0};
    while (true)
    {
        vTaskDelay(300 / portTICK_PERIOD_MS);
        memset(frame, 0, sizeof(frame));
        uart->recieveString(frame, sizeof(frame), 300);

        if (frame[0] == 0xbb && frame[1] == 0xaa)
        {
            float dB = (frame[3] + (frame[4] << 8)) * 0.1f;
            data->recordSample(dB);
            Settings current = data->getSettings();
            if (current.hasThreshold && dB > current.threshold)
            {
                nvs->saveTotalyOver(data->recordOver());
                now->sendOverEvent();
            }
        }
        else
        {
            data->recordBadFrame();
        }

        if (portalOpen && esp_timer_get_time() >= data->getPortalDeadline())
        {
            ESP_LOGI(TAG, "Portal window over, switching to ESP-NOW only");
            server->stop();
            dns->stop();
            now->suspend();
            ap->switchToEspNowOnly();
            now->resume(false);
            portalOpen = false;
        }
    }
}

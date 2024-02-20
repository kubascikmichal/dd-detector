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

extern "C"
{
    void app_main(void);
}

void app_main(void)
{
    AP *ap = new AP("Detector", "123456789");
    SharedData *data = new SharedData();
    HTTP_Server *server = new HTTP_Server(data);
    server->init();
    uint8_t array[6] = {0};
    uart_config_t cfg = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1};
    UART *uart = new UART(UART_NUM_1, &cfg, 21, 20, -1, -1, 256);
    uart->init();
    while (true)
    {
        vTaskDelay(300 / portTICK_PERIOD_MS);
        uart->recieveString(array, 6, 300);
        printf("%x %x %x %x %x %x\n\r", array[0], array[1], array[2], array[3], array[4], array[5]);
        float actDB = ((int32_t)(((array[0] == 0xbb && array[1] == 0xaa) ? (array[3] + (array[4] << 8)) : 0) * 10)) / 100.;
        printf("decibels = %0.2f\n\r", ((array[0] == 0xbb && array[1] == 0xaa) ? (array[3] + (array[4] << 8)) * 0.1 : 0));
        data->set_dB(actDB);
        memset(array, 0, 6);
    }
}

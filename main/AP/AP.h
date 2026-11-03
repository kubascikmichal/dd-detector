#ifndef __ACCESS_POINT_H__
#define __ACCESS_POINT_H__

#include "esp_netif.h"
#include "esp_wifi.h"

#define AP_IP_A 192
#define AP_IP_B 168
#define AP_IP_C 1
#define AP_IP_D 150
#define AP_IP_STR "192.168.1.150"

class AP
{
public:
    // Starts the visible portal AP on the channel shared with the generator.
    AP(const char *ssid, const char *password);

    // Hides the AP and switches the radio to LR only. Phones can no longer connect.
    // ESP-NOW must be suspended around this call.
    void switchToEspNowOnly();

    int connectedClients();

private:
    void configureNetwork();

    esp_netif_t *m_netif;
    wifi_config_t m_config;
};

#endif

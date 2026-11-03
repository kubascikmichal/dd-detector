#include "AP.h"

#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "dhcpserver/dhcpserver.h"

// DHCP option 114 keeps a pointer to this string, so it has to outlive the server.
static char s_captive_uri[] = "http://" AP_IP_STR "/";

AP::AP(const char *ssid, const char *password)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    m_netif = esp_netif_create_default_wifi_ap();
    configureNetwork();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));

    memset(&m_config, 0, sizeof(m_config));
    strlcpy((char *)m_config.ap.ssid, ssid, sizeof(m_config.ap.ssid));
    strlcpy((char *)m_config.ap.password, password, sizeof(m_config.ap.password));
    m_config.ap.channel = WIFI_CHANNEL;
    m_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    m_config.ap.max_connection = 8; // CONFIG_LWIP_DHCPS_MAX_STATION_NUM
    m_config.ap.beacon_interval = 100;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &m_config));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N | WIFI_PROTOCOL_LR));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void AP::configureNetwork()
{
    ESP_ERROR_CHECK(esp_netif_dhcps_stop(m_netif));

    esp_netif_ip_info_t ip = {};
    esp_netif_set_ip4_addr(&ip.ip, AP_IP_A, AP_IP_B, AP_IP_C, AP_IP_D);
    esp_netif_set_ip4_addr(&ip.gw, AP_IP_A, AP_IP_B, AP_IP_C, AP_IP_D);
    esp_netif_set_ip4_addr(&ip.netmask, 255, 255, 255, 0);
    ESP_ERROR_CHECK(esp_netif_set_ip_info(m_netif, &ip));

    // Clients must use our DNS, which resolves every name to the portal.
    esp_netif_dns_info_t dns = {};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4 = ip.ip;
    ESP_ERROR_CHECK(esp_netif_set_dns_info(m_netif, ESP_NETIF_DNS_MAIN, &dns));
    dhcps_offer_t offer_dns = OFFER_DNS;
    ESP_ERROR_CHECK(esp_netif_dhcps_option(m_netif, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns)));
    ESP_ERROR_CHECK(esp_netif_dhcps_option(m_netif, ESP_NETIF_OP_SET, ESP_NETIF_CAPTIVEPORTAL_URI, s_captive_uri, strlen(s_captive_uri)));

    ESP_ERROR_CHECK(esp_netif_dhcps_start(m_netif));
}

void AP::switchToEspNowOnly()
{
    ESP_ERROR_CHECK(esp_wifi_stop());
    // WIFI_IF_AP stays in use so the ESP-NOW source MAC does not change between phases.
    m_config.ap.ssid_hidden = 1;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &m_config));
    ESP_ERROR_CHECK(esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_LR));
    ESP_ERROR_CHECK(esp_wifi_start());
}

int AP::connectedClients()
{
    wifi_sta_list_t list = {};
    return esp_wifi_ap_get_sta_list(&list) == ESP_OK ? list.num : 0;
}

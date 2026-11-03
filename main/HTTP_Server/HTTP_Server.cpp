#include "HTTP_Server.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_timer.h"
#include "../Resolvers/MACResolving.h"

static SharedData *s_data;
static NVS *s_nvs;
static ESP_NOW *s_now;
static AP *s_ap;

static constexpr size_t MAX_SETTINGS_BODY = 256;

#define EMBEDDED_FILE(handler, symbol, mime)                                       \
    static esp_err_t handler(httpd_req_t *req)                                     \
    {                                                                              \
        extern const unsigned char start[] asm("_binary_" symbol "_start");        \
        extern const unsigned char end[] asm("_binary_" symbol "_end");            \
        httpd_resp_set_type(req, mime);                                            \
        return httpd_resp_send(req, (const char *)start, end - start);             \
    }

EMBEDDED_FILE(get_root, "index_html", "text/html; charset=utf-8")
EMBEDDED_FILE(get_style, "style_css", "text/css")
EMBEDDED_FILE(get_script, "script_js", "application/javascript")
EMBEDDED_FILE(get_favicon, "favicon_ico", "image/x-icon")

// Sends and frees json.
static esp_err_t send_json(httpd_req_t *req, cJSON *json, const char *status = HTTPD_200)
{
    char *text = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (text == NULL)
    {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, text);
    cJSON_free(text);
    return err;
}

static esp_err_t send_result(httpd_req_t *req, bool ok, const char *error)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddBoolToObject(json, "ok", ok);
    if (!ok)
    {
        cJSON_AddStringToObject(json, "error", error);
    }
    return send_json(req, json, ok ? HTTPD_200 : HTTPD_400);
}

static double oneDecimal(float value)
{
    return round(value * 10.0) / 10.0;
}

static const char *handshakeName(ESP_NOW::Handshake state)
{
    switch (state)
    {
    case ESP_NOW::Handshake::Sending:
        return "sending";
    case ESP_NOW::Handshake::WaitingReply:
        return "waiting_reply";
    case ESP_NOW::Handshake::Connected:
        return "connected";
    case ESP_NOW::Handshake::NoReply:
        return "no_reply";
    case ESP_NOW::Handshake::NoAck:
        return "no_ack";
    default:
        return "idle";
    }
}

// Printable payloads are shown as text, anything else as hex.
static void formatReply(const ESP_NOW::Stats &link, char *out, size_t size)
{
    int len = link.replyLen;
    while (len > 0 && link.reply[len - 1] == '\0')
    {
        len--;
    }
    bool printable = len > 0;
    for (int i = 0; i < len; i++)
    {
        printable = printable && link.reply[i] >= 0x20 && link.reply[i] < 0x7f;
    }
    out[0] = '\0';
    if (printable)
    {
        snprintf(out, size, "%.*s", len, (const char *)link.reply);
        return;
    }
    size_t pos = 0;
    for (int i = 0; i < link.replyLen && pos + 3 < size; i++)
    {
        pos += snprintf(out + pos, size - pos, "%02x ", link.reply[i]);
    }
}

static esp_err_t get_stats(httpd_req_t *req)
{
    int64_t now = esp_timer_get_time();
    SensorStats sensor = s_data->getStats();
    Settings settings = s_data->getSettings();
    ESP_NOW::Stats link = s_now->getStats();

    cJSON *json = cJSON_CreateObject();
    if (sensor.hasSample)
    {
        cJSON_AddNumberToObject(json, "db", oneDecimal(sensor.current));
        cJSON_AddNumberToObject(json, "min", oneDecimal(sensor.min));
        cJSON_AddNumberToObject(json, "max", oneDecimal(sensor.max));
        cJSON_AddNumberToObject(json, "avg", oneDecimal(sensor.avg));
    }
    cJSON_AddBoolToObject(json, "sensor_ok", sensor.sensorOk);
    cJSON_AddNumberToObject(json, "samples", sensor.samples);
    cJSON_AddNumberToObject(json, "bad_frames", sensor.badFrames);
    cJSON_AddNumberToObject(json, "over_boot", sensor.overSinceBoot);
    cJSON_AddNumberToObject(json, "over_total", (double)sensor.overTotal);
    if (settings.hasThreshold)
    {
        cJSON_AddNumberToObject(json, "threshold", oneDecimal(settings.threshold));
    }

    cJSON_AddNumberToObject(json, "uptime_s", (double)(now / 1000000));
    cJSON_AddNumberToObject(json, "free_heap", esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "clients", s_ap->connectedClients());
    int64_t remaining = s_data->getPortalDeadline() - now;
    cJSON_AddNumberToObject(json, "portal_s", remaining > 0 ? (double)(remaining / 1000000) : 0);

    // The generator sees the AP interface MAC, not the base MAC.
    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_AP, mac);
    char text[18];
    snprintf(text, sizeof(text), MACSTR, MAC2STR(mac));
    cJSON_AddStringToObject(json, "mac", text);

    cJSON *gen = cJSON_AddObjectToObject(json, "gen");
    cJSON_AddBoolToObject(gen, "configured", link.hasGenerator);
    cJSON_AddStringToObject(gen, "state", handshakeName(link.handshake));
    cJSON_AddBoolToObject(gen, "link_lost", link.linkLost);
    cJSON_AddNumberToObject(gen, "attempts", link.attempts);
    cJSON_AddNumberToObject(gen, "tx_frames", link.txFrames);
    cJSON_AddNumberToObject(gen, "tx_acked", link.txAcked);
    cJSON_AddNumberToObject(gen, "tx_failed", link.txFailed);
    cJSON_AddNumberToObject(gen, "rx_frames", link.rxFrames);
    cJSON_AddNumberToObject(gen, "tx_bytes", (double)link.txBytes);
    cJSON_AddNumberToObject(gen, "rx_bytes", (double)link.rxBytes);
    cJSON_AddNumberToObject(gen, "tx_bps", oneDecimal(link.txBytesPerSec));
    cJSON_AddNumberToObject(gen, "rx_bps", oneDecimal(link.rxBytesPerSec));
    if (link.lastRxUs > 0)
    {
        cJSON_AddNumberToObject(gen, "rssi", link.rssi);
        cJSON_AddNumberToObject(gen, "last_rx_s", (double)((now - link.lastRxUs) / 1000000));
    }
    if (link.replyMs >= 0)
    {
        cJSON_AddNumberToObject(gen, "reply_ms", link.replyMs);
    }
    char reply[ESPNOW_MAX_REPLY * 3 + 1];
    formatReply(link, reply, sizeof(reply));
    cJSON_AddStringToObject(gen, "reply", reply);

    return send_json(req, json);
}

static esp_err_t get_settings(httpd_req_t *req)
{
    cJSON *settings = s_nvs->getConfig();
    return send_json(req, settings != NULL ? settings : cJSON_CreateObject());
}

static esp_err_t set_settings(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > MAX_SETTINGS_BODY)
    {
        return send_result(req, false, "Invalid request size");
    }
    char body[MAX_SETTINGS_BODY + 1];
    size_t received = 0;
    while (received < req->content_len)
    {
        int ret = httpd_req_recv(req, body + received, req->content_len - received);
        if (ret == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;
        }
        if (ret <= 0)
        {
            return ESP_FAIL;
        }
        received += ret;
    }
    body[received] = '\0';

    cJSON *input = cJSON_Parse(body);
    Settings settings;
    bool valid = input != NULL && parseSettings(input, settings);
    cJSON_Delete(input);
    if (!valid)
    {
        return send_result(req, false, "Threshold must be 0-200 dB and MAC like aa:bb:cc:dd:ee:ff");
    }

    // Store a normalized copy rather than whatever the client sent.
    char threshold[12] = "";
    char mac[18] = "";
    if (settings.hasThreshold)
    {
        snprintf(threshold, sizeof(threshold), "%.1f", settings.threshold);
    }
    if (settings.hasGenerator)
    {
        snprintf(mac, sizeof(mac), MACSTR, MAC2STR(settings.generator));
    }
    cJSON *stored = cJSON_CreateObject();
    cJSON_AddStringToObject(stored, "treshold", threshold);
    cJSON_AddStringToObject(stored, "MAC", mac);
    bool saved = s_nvs->saveConfig(stored);
    cJSON_Delete(stored);
    if (!saved)
    {
        return send_result(req, false, "Could not save settings");
    }

    s_data->setSettings(settings);
    s_now->setGenerator(settings.hasGenerator ? settings.generator : nullptr);
    return send_result(req, true, nullptr);
}

static esp_err_t connect_generator(httpd_req_t *req)
{
    bool started = s_now->startHandshake();
    return send_result(req, started, "Set and save the generator MAC first");
}

// Every unknown URL, including the OS connectivity checks, ends at the portal.
// That is what makes phones and laptops open the sign-in page.
static esp_err_t redirect_to_portal(httpd_req_t *req, httpd_err_code_t)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://" AP_IP_STR "/");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, NULL, 0);
}

HTTP_Server::HTTP_Server(SharedData *data, NVS *nvs, ESP_NOW *now, AP *ap)
{
    s_data = data;
    s_nvs = nvs;
    s_now = now;
    s_ap = ap;
}

bool HTTP_Server::init()
{
    static const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = get_root, .user_ctx = NULL},
        {.uri = "/style.css", .method = HTTP_GET, .handler = get_style, .user_ctx = NULL},
        {.uri = "/script.js", .method = HTTP_GET, .handler = get_script, .user_ctx = NULL},
        {.uri = "/favicon.ico", .method = HTTP_GET, .handler = get_favicon, .user_ctx = NULL},
        {.uri = "/getStats", .method = HTTP_GET, .handler = get_stats, .user_ctx = NULL},
        {.uri = "/getSettings", .method = HTTP_GET, .handler = get_settings, .user_ctx = NULL},
        {.uri = "/setSettings", .method = HTTP_POST, .handler = set_settings, .user_ctx = NULL},
        {.uri = "/connectGenerator", .method = HTTP_POST, .handler = connect_generator, .user_ctx = NULL},
    };

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = sizeof(routes) / sizeof(routes[0]);
    config.stack_size = 6144;
    // Phones open many sockets while probing the portal; recycle the oldest ones.
    // Needs max_open_sockets + 3 (httpd) + 1 (DNS) <= CONFIG_LWIP_MAX_SOCKETS.
    config.max_open_sockets = 12;
    config.lru_purge_enable = true;

    if (httpd_start(&m_server, &config) != ESP_OK)
    {
        return false;
    }
    for (const httpd_uri_t &route : routes)
    {
        httpd_register_uri_handler(m_server, &route);
    }
    httpd_register_err_handler(m_server, HTTPD_404_NOT_FOUND, redirect_to_portal);
    return true;
}

void HTTP_Server::stop()
{
    if (m_server != NULL)
    {
        httpd_stop(m_server);
        m_server = NULL;
    }
}

#include "ESP_NOW.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "../AP/AP.h"

static const char *TAG = "espnow";
static const uint8_t BROADCAST[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static constexpr int64_t ATTEMPT_INTERVAL_US = 1000000;
static constexpr int64_t ACK_TIMEOUT_US = 6000000;
static constexpr int64_t REPLY_TIMEOUT_US = 3000000;
static constexpr uint32_t LINK_LOST_AFTER_FAILS = 3;

ESP_NOW *ESP_NOW::s_instance = nullptr;

static void addPeer(const uint8_t *mac)
{
    esp_now_peer_info_t peer = {};
    peer.channel = WIFI_CHANNEL;
    peer.ifidx = ESPNOW_WIFI_IF;
    peer.encrypt = false;
    memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST)
    {
        ESP_LOGE(TAG, "add peer failed: %s", esp_err_to_name(err));
    }
}

ESP_NOW::ESP_NOW()
{
    s_instance = this;
    m_radio = xSemaphoreCreateMutex();
    xSemaphoreTake(m_radio, portMAX_DELAY);
    activate();
    xSemaphoreGive(m_radio);
    xTaskCreate(taskEntry, "espnow_link", 3072, this, 4, NULL);
}

// Caller holds m_radio.
void ESP_NOW::activate()
{
    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_send_cb(onSent));
    ESP_ERROR_CHECK(esp_now_register_recv_cb(onRecv));
    addPeer(BROADCAST);
    portENTER_CRITICAL(&m_lock);
    bool hasGenerator = m_hasGenerator;
    uint8_t generator[ESP_NOW_ETH_ALEN];
    memcpy(generator, m_generator, sizeof(generator));
    m_active = true;
    portEXIT_CRITICAL(&m_lock);
    if (hasGenerator)
    {
        addPeer(generator);
    }
}

void ESP_NOW::suspend()
{
    xSemaphoreTake(m_radio, portMAX_DELAY);
    portENTER_CRITICAL(&m_lock);
    m_active = false;
    portEXIT_CRITICAL(&m_lock);
    ESP_ERROR_CHECK(esp_now_deinit());
    xSemaphoreGive(m_radio);
}

void ESP_NOW::resume(bool phyFallback)
{
    xSemaphoreTake(m_radio, portMAX_DELAY);
    m_phyFallback = phyFallback;
    activate();
    xSemaphoreGive(m_radio);
}

void ESP_NOW::setGenerator(const uint8_t *mac)
{
    xSemaphoreTake(m_radio, portMAX_DELAY);
    portENTER_CRITICAL(&m_lock);
    bool same = mac ? (m_hasGenerator && memcmp(m_generator, mac, ESP_NOW_ETH_ALEN) == 0) : !m_hasGenerator;
    bool hadGenerator = m_hasGenerator;
    uint8_t previous[ESP_NOW_ETH_ALEN];
    memcpy(previous, m_generator, sizeof(previous));
    if (!same)
    {
        m_hasGenerator = mac != nullptr;
        if (mac)
        {
            memcpy(m_generator, mac, ESP_NOW_ETH_ALEN);
        }
        m_handshake = Handshake::Idle;
        m_attempts = 0;
        m_replyLen = 0;
        m_replyMs = -1;
        m_lastRxUs = 0;
        m_consecutiveFails = 0;
    }
    bool active = m_active;
    portEXIT_CRITICAL(&m_lock);

    if (!same && active)
    {
        if (hadGenerator)
        {
            esp_now_del_peer(previous);
        }
        if (mac)
        {
            addPeer(mac);
        }
    }
    xSemaphoreGive(m_radio);
}

bool ESP_NOW::startHandshake()
{
    bool started = false;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&m_lock);
    if (m_hasGenerator)
    {
        m_handshake = Handshake::Sending;
        m_attempts = 0;
        m_replyLen = 0;
        m_replyMs = -1;
        m_nextAttempt = now;
        m_deadline = now + ACK_TIMEOUT_US;
        started = true;
    }
    portEXIT_CRITICAL(&m_lock);
    return started;
}

void ESP_NOW::sendOverEvent()
{
    uint8_t target[ESP_NOW_ETH_ALEN];
    portENTER_CRITICAL(&m_lock);
    memcpy(target, m_hasGenerator ? m_generator : BROADCAST, sizeof(target));
    portEXIT_CRITICAL(&m_lock);
    send(target, "Over", 5); // includes the NUL: the generator prints the payload with %s
}

void ESP_NOW::send(const uint8_t *addr, const char *payload, uint8_t len)
{
    xSemaphoreTake(m_radio, portMAX_DELAY);
    portENTER_CRITICAL(&m_lock);
    bool active = m_active;
    portEXIT_CRITICAL(&m_lock);
    esp_err_t err = active ? esp_now_send(addr, (const uint8_t *)payload, len) : ESP_ERR_ESPNOW_NOT_INIT;
    xSemaphoreGive(m_radio);

    portENTER_CRITICAL(&m_lock);
    if (err == ESP_OK)
    {
        m_txFrames++;
        m_txBytes += len;
    }
    else if (active)
    {
        m_txFailed++;
    }
    portEXIT_CRITICAL(&m_lock);
    if (err != ESP_OK && active)
    {
        ESP_LOGW(TAG, "send failed: %s", esp_err_to_name(err));
    }
}

// While the AP runs the radio accepts both PHYs, but the generator may be listening on
// only one of them, depending on its own AP/LR cycle. Handshake attempts alternate.
void ESP_NOW::setPeerLongRange(bool longRange)
{
    uint8_t target[ESP_NOW_ETH_ALEN];
    portENTER_CRITICAL(&m_lock);
    memcpy(target, m_generator, sizeof(target));
    bool ready = m_active && m_hasGenerator;
    portEXIT_CRITICAL(&m_lock);
    if (!ready)
    {
        return;
    }
    esp_now_rate_config_t rate = {};
    rate.phymode = longRange ? WIFI_PHY_MODE_LR : WIFI_PHY_MODE_11B;
    rate.rate = longRange ? WIFI_PHY_RATE_LORA_500K : WIFI_PHY_RATE_1M_L;
    xSemaphoreTake(m_radio, portMAX_DELAY);
    esp_err_t err = esp_now_set_peer_rate_config(target, &rate);
    xSemaphoreGive(m_radio);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "set peer rate failed: %s", esp_err_to_name(err));
    }
}

void ESP_NOW::onSent(const esp_now_send_info_t *info, esp_now_send_status_t status)
{
    ESP_NOW *self = s_instance;
    // Broadcast frames have no ACK, so they say nothing about the link.
    if (!self || !info || !info->des_addr || memcmp(info->des_addr, BROADCAST, ESP_NOW_ETH_ALEN) == 0)
    {
        return;
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&self->m_lock);
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        self->m_txAcked++;
        self->m_consecutiveFails = 0;
        if (self->m_handshake == Handshake::Sending)
        {
            self->m_handshake = Handshake::WaitingReply;
            self->m_deadline = now + REPLY_TIMEOUT_US;
        }
    }
    else
    {
        self->m_txFailed++;
        self->m_consecutiveFails++;
    }
    portEXIT_CRITICAL(&self->m_lock);
}

void ESP_NOW::onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    ESP_NOW *self = s_instance;
    if (!self || !info || !data || len <= 0)
    {
        return;
    }
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&self->m_lock);
    if (self->m_hasGenerator && memcmp(info->src_addr, self->m_generator, ESP_NOW_ETH_ALEN) == 0)
    {
        self->m_rxFrames++;
        self->m_rxBytes += len;
        self->m_lastRxUs = now;
        self->m_consecutiveFails = 0;
        if (info->rx_ctrl)
        {
            self->m_rssi = info->rx_ctrl->rssi;
        }
        self->m_replyLen = len > ESPNOW_MAX_REPLY ? ESPNOW_MAX_REPLY : len;
        memcpy(self->m_reply, data, self->m_replyLen);
        if (self->m_handshake == Handshake::Sending || self->m_handshake == Handshake::WaitingReply)
        {
            self->m_replyMs = (now - self->m_attemptSentAt) / 1000;
        }
        self->m_handshake = Handshake::Connected;
    }
    portEXIT_CRITICAL(&self->m_lock);
}

void ESP_NOW::taskEntry(void *arg)
{
    static_cast<ESP_NOW *>(arg)->taskLoop();
}

void ESP_NOW::taskLoop()
{
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        int64_t now = esp_timer_get_time();
        updateRates(now);

        bool attempt = false;
        bool longRange = false;
        uint8_t target[ESP_NOW_ETH_ALEN];
        portENTER_CRITICAL(&m_lock);
        memcpy(target, m_generator, sizeof(target));
        if (m_handshake == Handshake::Sending)
        {
            if (now >= m_deadline)
            {
                m_handshake = Handshake::NoAck;
            }
            else if (now >= m_nextAttempt && m_active)
            {
                attempt = true;
                longRange = m_phyFallback && (m_attempts % 2 == 1);
                m_attempts++;
                m_nextAttempt = now + ATTEMPT_INTERVAL_US;
                m_attemptSentAt = now;
            }
        }
        else if (m_handshake == Handshake::WaitingReply && now >= m_deadline)
        {
            m_handshake = Handshake::NoReply;
        }
        bool fallback = m_phyFallback;
        portEXIT_CRITICAL(&m_lock);

        if (attempt)
        {
            if (fallback)
            {
                setPeerLongRange(longRange);
            }
            send(target, "HH", 3);
        }
    }
}

// Rates are averaged over a sliding window because traffic is sparse.
void ESP_NOW::updateRates(int64_t now)
{
    if (now < m_nextRateUpdate)
    {
        return;
    }
    m_nextRateUpdate = now + 1000000;
    portENTER_CRITICAL(&m_lock);
    int oldest = (m_historyPos + 1) % RATE_WINDOW_S;
    m_txRate = (float)(m_txBytes - m_txHistory[oldest]) / (RATE_WINDOW_S - 1);
    m_rxRate = (float)(m_rxBytes - m_rxHistory[oldest]) / (RATE_WINDOW_S - 1);
    m_historyPos = oldest;
    m_txHistory[m_historyPos] = m_txBytes;
    m_rxHistory[m_historyPos] = m_rxBytes;
    portEXIT_CRITICAL(&m_lock);
}

ESP_NOW::Stats ESP_NOW::getStats()
{
    Stats s = {};
    portENTER_CRITICAL(&m_lock);
    s.handshake = m_handshake;
    s.attempts = m_attempts;
    s.hasGenerator = m_hasGenerator;
    s.linkLost = m_consecutiveFails >= LINK_LOST_AFTER_FAILS;
    s.txFrames = m_txFrames;
    s.txAcked = m_txAcked;
    s.txFailed = m_txFailed;
    s.rxFrames = m_rxFrames;
    s.txBytes = m_txBytes;
    s.rxBytes = m_rxBytes;
    s.txBytesPerSec = m_txRate;
    s.rxBytesPerSec = m_rxRate;
    s.rssi = m_rssi;
    s.lastRxUs = m_lastRxUs;
    s.replyMs = m_replyMs;
    s.replyLen = m_replyLen;
    memcpy(s.reply, m_reply, m_replyLen);
    portEXIT_CRITICAL(&m_lock);
    return s;
}

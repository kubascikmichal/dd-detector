#include "ESP_NOW.h"

#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"

using namespace dd_proto;

static const char *TAG = "espnow";
static const uint8_t BROADCAST[ESP_NOW_ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
static constexpr int64_t ATTEMPT_INTERVAL_US = 1000000;
static constexpr int64_t TEST_TIMEOUT_US = 6000000;
static constexpr int64_t DATA_INTERVAL_US = 500000;
static constexpr uint32_t LINK_LOST_AFTER_FAILS = 3;

static_assert(sizeof(Header) == 8 && sizeof(DataFrame) == 16, "wire format must match dd-generator");

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

ESP_NOW::ESP_NOW(SharedData *data) : m_data(data)
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
    bool hasSession = m_sessionActive;
    uint8_t generator[ESP_NOW_ETH_ALEN];
    uint8_t sessionPeer[ESP_NOW_ETH_ALEN];
    memcpy(generator, m_generator, sizeof(generator));
    memcpy(sessionPeer, m_sessionPeer, sizeof(sessionPeer));
    m_active = true;
    portEXIT_CRITICAL(&m_lock);
    if (hasGenerator)
    {
        addPeer(generator);
    }
    if (hasSession)
    {
        addPeer(sessionPeer);
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

void ESP_NOW::resume()
{
    xSemaphoreTake(m_radio, portMAX_DELAY);
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
        m_test = LinkTest::Idle;
        m_attempts = 0;
        m_lastRxUs = 0;
        m_consecutiveFails = 0;
        m_sessionActive = false;
        m_ackPending = false;
        m_peerPending = false;
        m_ignoredFrames = 0;
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

bool ESP_NOW::startLinkTest()
{
    bool started = false;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&m_lock);
    if (m_hasGenerator)
    {
        m_test = LinkTest::Testing;
        m_attempts = 0;
        m_nextAttempt = now;
        m_deadline = now + TEST_TIMEOUT_US;
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
    send(target, "Over", 5);
}

void ESP_NOW::send(const uint8_t *addr, const void *payload, uint8_t len)
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

void ESP_NOW::sendSessionFrame(const uint8_t *addr, Type type, const SensorStats &sensor)
{
    Settings settings = m_data->getSettings();
    DataFrame frame = {};
    memcpy(frame.header.magic, "DD", sizeof(frame.header.magic));
    frame.header.version = VERSION;
    frame.header.type = type;
    frame.header.seq = ++m_txSeq;
    frame.db_x10 = lround(sensor.current * 10.0f);
    frame.threshold_x10 = settings.hasThreshold ? lround(settings.threshold * 10.0f) : 0;
    frame.total_over = sensor.overTotal;
    send(addr, &frame, sizeof(frame));
    if (type == TYPE_DATA)
    {
        portENTER_CRITICAL(&m_lock);
        m_dataFramesSent++;
        portEXIT_CRITICAL(&m_lock);
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
    portENTER_CRITICAL(&self->m_lock);
    if (status == ESP_NOW_SEND_SUCCESS)
    {
        self->m_txAcked++;
        self->m_consecutiveFails = 0;
        if (self->m_test == LinkTest::Testing)
        {
            self->m_test = LinkTest::Reachable;
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
    // With no generator configured, any generator may connect, matching its own "empty MAC" rule.
    if (self->m_hasGenerator && memcmp(info->src_addr, self->m_generator, ESP_NOW_ETH_ALEN) != 0)
    {
        self->m_ignoredFrames++;
        memcpy(self->m_lastIgnored, info->src_addr, ESP_NOW_ETH_ALEN);
        portEXIT_CRITICAL(&self->m_lock);
        return;
    }

    self->m_rxFrames++;
    self->m_rxBytes += len;
    self->m_lastRxUs = now;
    self->m_consecutiveFails = 0;
    if (info->rx_ctrl)
    {
        self->m_rssi = info->rx_ctrl->rssi;
    }

    if (len >= (int)sizeof(Header))
    {
        Header header;
        memcpy(&header, data, sizeof(header));
        if (memcmp(header.magic, "DD", sizeof(header.magic)) == 0 && header.version == VERSION && header.type == TYPE_HELLO)
        {
            bool samePeer = memcmp(self->m_sessionPeer, info->src_addr, ESP_NOW_ETH_ALEN) == 0;
            if (!self->m_sessionActive || !samePeer)
            {
                memcpy(self->m_sessionPeer, info->src_addr, ESP_NOW_ETH_ALEN);
                self->m_sessionActive = true;
                self->m_ackPending = true;
                self->m_peerPending = true; // esp_now_add_peer() is left to the link task
                self->m_nextDataUs = 0;
            }
            self->m_lastHelloUs = now;
        }
    }
    portEXIT_CRITICAL(&self->m_lock);
}

void ESP_NOW::taskEntry(void *arg)
{
    ESP_NOW *self = static_cast<ESP_NOW *>(arg);
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        self->tick();
    }
}

void ESP_NOW::tick()
{
    int64_t now = esp_timer_get_time();
    updateRates(now);
    SensorStats sensor = m_data->getStats();

    bool attempt = false;
    bool addSessionPeer = false;
    bool sendAck = false;
    bool sendData = false;
    uint8_t generator[ESP_NOW_ETH_ALEN];
    uint8_t sessionPeer[ESP_NOW_ETH_ALEN];

    portENTER_CRITICAL(&m_lock);
    memcpy(generator, m_generator, sizeof(generator));
    memcpy(sessionPeer, m_sessionPeer, sizeof(sessionPeer));
    if (m_test == LinkTest::Testing)
    {
        if (now >= m_deadline)
        {
            m_test = LinkTest::NoAck;
        }
        else if (now >= m_nextAttempt && m_active)
        {
            attempt = true;
            m_attempts++;
            m_nextAttempt = now + ATTEMPT_INTERVAL_US;
        }
    }
    if (m_sessionActive && now - m_lastHelloUs > (int64_t)STREAM_TIMEOUT_MS * 1000)
    {
        m_sessionActive = false;
        m_ackPending = false;
    }
    if (m_peerPending && m_active)
    {
        addSessionPeer = true;
        m_peerPending = false;
    }
    // The protocol cannot mark a value as invalid, so without a real measurement this side
    // stays silent. The generator then reports no connection instead of showing a false level.
    if (m_sessionActive && m_active && sensor.sensorOk)
    {
        if (m_ackPending)
        {
            sendAck = true;
            m_ackPending = false;
        }
        else if (now >= m_nextDataUs)
        {
            sendData = true;
            m_nextDataUs = now + DATA_INTERVAL_US;
        }
    }
    portEXIT_CRITICAL(&m_lock);

    if (addSessionPeer)
    {
        xSemaphoreTake(m_radio, portMAX_DELAY);
        portENTER_CRITICAL(&m_lock);
        bool active = m_active;
        portEXIT_CRITICAL(&m_lock);
        if (active)
        {
            addPeer(sessionPeer);
        }
        xSemaphoreGive(m_radio);
    }
    if (attempt)
    {
        send(generator, "HH", 3);
    }
    if (sendAck || sendData)
    {
        sendSessionFrame(sessionPeer, sendAck ? TYPE_HELLO_ACK : TYPE_DATA, sensor);
    }
}

// Rates are averaged over a sliding window because traffic can be sparse.
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
    s.test = m_test;
    s.attempts = m_attempts;
    s.hasGenerator = m_hasGenerator;
    s.sessionActive = m_sessionActive;
    s.linkLost = m_consecutiveFails >= LINK_LOST_AFTER_FAILS;
    s.txFrames = m_txFrames;
    s.txAcked = m_txAcked;
    s.txFailed = m_txFailed;
    s.rxFrames = m_rxFrames;
    s.dataFramesSent = m_dataFramesSent;
    s.txBytes = m_txBytes;
    s.rxBytes = m_rxBytes;
    s.txBytesPerSec = m_txRate;
    s.rxBytesPerSec = m_rxRate;
    s.rssi = m_rssi;
    s.lastRxUs = m_lastRxUs;
    s.ignoredFrames = m_ignoredFrames;
    memcpy(s.lastIgnored, m_lastIgnored, sizeof(s.lastIgnored));
    portEXIT_CRITICAL(&m_lock);
    return s;
}

#ifndef __ESPNOW_H__
#define __ESPNOW_H__

#include <stdint.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define ESPNOW_WIFI_IF WIFI_IF_AP
#define ESPNOW_MAX_REPLY 64

// Link to the generator. Owns the ESP-NOW callbacks and all traffic counters.
//
// Handshake: the detector sends "HH" to the configured generator until a frame
// gets a MAC-level ACK, then waits for the generator to answer with any frame.
// The current generator firmware starts a tone on *every* frame it receives, so
// never add periodic traffic here, and only retry frames that were not ACKed.
class ESP_NOW
{
public:
    enum class Handshake
    {
        Idle,
        Sending,      // no ACK yet, still retrying
        WaitingReply, // ACKed: the generator is reachable, no data from it yet
        Connected,    // the generator sent data
        NoReply,      // ACKed but the generator never sent data
        NoAck         // no frame was ACKed
    };

    struct Stats
    {
        Handshake handshake;
        uint32_t attempts;
        bool hasGenerator;
        bool linkLost;
        uint32_t txFrames, txAcked, txFailed;
        uint32_t rxFrames;
        uint64_t txBytes, rxBytes;
        float txBytesPerSec, rxBytesPerSec;
        int rssi;
        int64_t lastRxUs; // 0 = nothing received yet
        int32_t replyMs;  // -1 = not measured
        uint8_t reply[ESPNOW_MAX_REPLY];
        uint8_t replyLen;
    };

    ESP_NOW();

    void setGenerator(const uint8_t *mac); // nullptr clears it
    bool startHandshake();
    void sendOverEvent();

    // ESP-NOW has to be shut down while the Wi-Fi driver restarts.
    void suspend();
    void resume(bool phyFallback);

    Stats getStats();

private:
    static void onSent(const esp_now_send_info_t *info, esp_now_send_status_t status);
    static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);
    static void taskEntry(void *arg);
    void taskLoop();

    void activate();
    void send(const uint8_t *addr, const char *payload, uint8_t len);
    void setPeerLongRange(bool longRange);
    void updateRates(int64_t now);

    static ESP_NOW *s_instance;

    SemaphoreHandle_t m_radio; // guards esp_now_* calls against suspend()
    portMUX_TYPE m_lock = portMUX_INITIALIZER_UNLOCKED; // guards everything below, shared with the Wi-Fi task
    bool m_active = false;
    bool m_phyFallback = true;
    bool m_hasGenerator = false;
    uint8_t m_generator[ESP_NOW_ETH_ALEN] = {};

    Handshake m_handshake = Handshake::Idle;
    uint32_t m_attempts = 0;
    bool m_attemptLongRange = false;
    int64_t m_deadline = 0;
    int64_t m_nextAttempt = 0;
    int64_t m_attemptSentAt = 0;
    int32_t m_replyMs = -1;
    uint32_t m_consecutiveFails = 0;

    uint32_t m_txFrames = 0, m_txAcked = 0, m_txFailed = 0, m_rxFrames = 0;
    uint64_t m_txBytes = 0, m_rxBytes = 0;
    int m_rssi = 0;
    int64_t m_lastRxUs = 0;
    uint8_t m_reply[ESPNOW_MAX_REPLY] = {};
    uint8_t m_replyLen = 0;

    static constexpr int RATE_WINDOW_S = 10;
    uint64_t m_txHistory[RATE_WINDOW_S] = {};
    uint64_t m_rxHistory[RATE_WINDOW_S] = {};
    int m_historyPos = 0;
    int64_t m_nextRateUpdate = 0;
    float m_txRate = 0, m_rxRate = 0;
};

#endif

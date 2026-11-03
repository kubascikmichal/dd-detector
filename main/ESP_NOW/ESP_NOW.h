#ifndef __ESPNOW_H__
#define __ESPNOW_H__

#include <stdint.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "Protocol.h"
#include "../Shared_data/SharedData.h"

#define ESPNOW_WIFI_IF WIFI_IF_AP

// Link to the generator. Owns the ESP-NOW callbacks and all traffic counters.
// The wire format and session are defined in Protocol.h, shared with dd-generator.
//
// There are three independent kinds of traffic:
// - Start triggers ("Over"), sent when the level exceeds the threshold. They need no session.
// - The session: the generator starts it with HELLO, this side answers with HELLO_ACK and
//   streams the live level as DATA until HELLOs stop. Only the generator can start it.
// - The reachability test: "HH", repeated until a frame gets a MAC-level ACK. The generator
//   does not answer it, so an ACK is all it can prove.
class ESP_NOW
{
public:
    enum class LinkTest
    {
        Idle,
        Testing,
        Reachable,
        NoAck
    };

    struct Stats
    {
        LinkTest test;
        uint32_t attempts;
        bool hasGenerator;
        bool sessionActive;
        bool linkLost;
        uint32_t txFrames, txAcked, txFailed;
        uint32_t rxFrames, dataFramesSent;
        uint64_t txBytes, rxBytes;
        float txBytesPerSec, rxBytesPerSec;
        int rssi;
        int64_t lastRxUs; // 0 = nothing received yet
        uint32_t ignoredFrames;
        uint8_t lastIgnored[ESP_NOW_ETH_ALEN];
    };

    explicit ESP_NOW(SharedData *data);

    void setGenerator(const uint8_t *mac); // nullptr clears it
    bool startLinkTest();
    void sendOverEvent();

    // ESP-NOW has to be shut down while the Wi-Fi driver restarts.
    void suspend();
    void resume();

    Stats getStats();

private:
    static void onSent(const esp_now_send_info_t *info, esp_now_send_status_t status);
    static void onRecv(const esp_now_recv_info_t *info, const uint8_t *data, int len);
    static void taskEntry(void *arg);

    void tick();
    void activate();
    void send(const uint8_t *addr, const void *payload, uint8_t len);
    void sendSessionFrame(const uint8_t *addr, dd_proto::Type type, const SensorStats &sensor);
    void updateRates(int64_t now);

    static ESP_NOW *s_instance;

    SharedData *m_data;
    SemaphoreHandle_t m_radio; // guards esp_now_* calls against suspend()
    portMUX_TYPE m_lock = portMUX_INITIALIZER_UNLOCKED; // guards everything below, shared with the Wi-Fi task
    bool m_active = false;
    bool m_hasGenerator = false;
    uint8_t m_generator[ESP_NOW_ETH_ALEN] = {};

    LinkTest m_test = LinkTest::Idle;
    uint32_t m_attempts = 0;
    int64_t m_deadline = 0;
    int64_t m_nextAttempt = 0;
    uint32_t m_consecutiveFails = 0;

    bool m_sessionActive = false;
    bool m_ackPending = false;
    bool m_peerPending = false;
    uint8_t m_sessionPeer[ESP_NOW_ETH_ALEN] = {};
    int64_t m_lastHelloUs = 0;
    int64_t m_nextDataUs = 0;
    uint32_t m_txSeq = 0;
    uint32_t m_dataFramesSent = 0;

    uint32_t m_txFrames = 0, m_txAcked = 0, m_txFailed = 0, m_rxFrames = 0;
    uint64_t m_txBytes = 0, m_rxBytes = 0;
    int m_rssi = 0;
    int64_t m_lastRxUs = 0;
    uint32_t m_ignoredFrames = 0;
    uint8_t m_lastIgnored[ESP_NOW_ETH_ALEN] = {};

    static constexpr int RATE_WINDOW_S = 10;
    uint64_t m_txHistory[RATE_WINDOW_S] = {};
    uint64_t m_rxHistory[RATE_WINDOW_S] = {};
    int m_historyPos = 0;
    int64_t m_nextRateUpdate = 0;
    float m_txRate = 0, m_rxRate = 0;
};

#endif

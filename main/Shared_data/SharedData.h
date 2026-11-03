#ifndef __SHARED_DATA_H__
#define __SHARED_DATA_H__

#include <stdint.h>
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

struct Settings
{
    bool hasThreshold = false;
    float threshold = 0;
    bool hasGenerator = false;
    uint8_t generator[6] = {};
};

// Reads the stored JSON form {"treshold":"85","MAC":"aa:bb:.."}. Empty or missing
// fields mean "not set". Returns false if a field is invalid; valid ones are still applied.
bool parseSettings(const cJSON *json, Settings &out);

struct SensorStats
{
    bool hasSample;
    bool sensorOk;
    float current, min, max, avg;
    uint32_t samples, badFrames;
    uint32_t overSinceBoot;
    uint64_t overTotal;
};

class SharedData
{
private:
    static constexpr uint32_t SENSOR_LOST_AFTER_BAD_FRAMES = 5;

    SemaphoreHandle_t m_mutex;
    Settings m_settings;
    SensorStats m_stats = {};
    double m_sum = 0;
    uint32_t m_consecutiveBad = 0;
    int64_t m_portalDeadlineUs = 0;

public:
    SharedData();

    void recordSample(float dB);
    void recordBadFrame();
    void setOverTotal(uint64_t total);
    uint64_t recordOver(); // returns the new all-time total
    SensorStats getStats();

    Settings getSettings();
    void setSettings(const Settings &settings);

    void setPortalDeadline(int64_t us);
    int64_t getPortalDeadline();
};

#endif

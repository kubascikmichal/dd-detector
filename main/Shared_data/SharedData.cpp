#include "SharedData.h"

#include <stdlib.h>
#include "../Resolvers/MACResolving.h"

bool parseSettings(const cJSON *json, Settings &out)
{
    out = Settings();
    if (json == nullptr)
    {
        return true;
    }
    bool valid = true;

    const cJSON *threshold = cJSON_GetObjectItemCaseSensitive(json, "treshold");
    if (cJSON_IsString(threshold) && threshold->valuestring[0] != '\0')
    {
        char *end = nullptr;
        double value = strtod(threshold->valuestring, &end);
        if (*end == '\0' && value >= 0 && value <= 200)
        {
            out.hasThreshold = true;
            out.threshold = value;
        }
        else
        {
            valid = false;
        }
    }
    else if (threshold != nullptr && !cJSON_IsString(threshold))
    {
        valid = false;
    }

    const cJSON *mac = cJSON_GetObjectItemCaseSensitive(json, "MAC");
    if (cJSON_IsString(mac) && mac->valuestring[0] != '\0')
    {
        // The generator must be a single device: a multicast address would never be ACKed.
        out.hasGenerator = parseMAC(mac->valuestring, out.generator) && !(out.generator[0] & 0x01);
        valid = valid && out.hasGenerator;
    }
    else if (mac != nullptr && !cJSON_IsString(mac))
    {
        valid = false;
    }
    return valid;
}

SharedData::SharedData()
{
    m_mutex = xSemaphoreCreateMutex();
}

void SharedData::recordSample(float dB)
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    if (!m_stats.hasSample || dB < m_stats.min)
    {
        m_stats.min = dB;
    }
    if (!m_stats.hasSample || dB > m_stats.max)
    {
        m_stats.max = dB;
    }
    m_stats.hasSample = true;
    m_stats.sensorOk = true;
    m_stats.current = dB;
    m_stats.samples++;
    m_sum += dB;
    m_stats.avg = m_sum / m_stats.samples;
    m_consecutiveBad = 0;
    xSemaphoreGive(m_mutex);
}

void SharedData::recordBadFrame()
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_stats.badFrames++;
    m_consecutiveBad++;
    // Single bad frames are normal: the UART is flushed between reads, so frames get cut.
    if (m_consecutiveBad >= SENSOR_LOST_AFTER_BAD_FRAMES)
    {
        m_stats.sensorOk = false;
    }
    xSemaphoreGive(m_mutex);
}

void SharedData::setOverTotal(uint64_t total)
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_stats.overTotal = total;
    xSemaphoreGive(m_mutex);
}

uint64_t SharedData::recordOver()
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_stats.overSinceBoot++;
    uint64_t total = ++m_stats.overTotal;
    xSemaphoreGive(m_mutex);
    return total;
}

SensorStats SharedData::getStats()
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    SensorStats stats = m_stats;
    xSemaphoreGive(m_mutex);
    return stats;
}

Settings SharedData::getSettings()
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    Settings settings = m_settings;
    xSemaphoreGive(m_mutex);
    return settings;
}

void SharedData::setSettings(const Settings &settings)
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_settings = settings;
    xSemaphoreGive(m_mutex);
}

void SharedData::setPortalDeadline(int64_t us)
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    m_portalDeadlineUs = us;
    xSemaphoreGive(m_mutex);
}

int64_t SharedData::getPortalDeadline()
{
    xSemaphoreTake(m_mutex, portMAX_DELAY);
    int64_t deadline = m_portalDeadlineUs;
    xSemaphoreGive(m_mutex);
    return deadline;
}

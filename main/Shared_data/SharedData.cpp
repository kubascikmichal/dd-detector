#include "SharedData.h"

SharedData::SharedData()
{
    this->mSemaphoredB = xSemaphoreCreateMutex();
    this->mSemaphoreStats = xSemaphoreCreateMutex();
    this->afterRestartAbove = 0;
}

SharedData::~SharedData()
{
}

float SharedData::get_dB()
{
    float tmp = 0;
    if (this->mSemaphoredB != NULL)
    {
        if (xSemaphoreTake((this->mSemaphoredB), 100) == pdPASS)
        {
            tmp = this->actual_dB;
            xSemaphoreGive((this->mSemaphoredB));
        }
    }
    return tmp;
}

bool SharedData::set_dB(float dB)
{
    if (xSemaphoreTake((this->mSemaphoredB), 100) == pdPASS)
    {
        this->actual_dB = dB;
        xSemaphoreGive((this->mSemaphoredB));
    }
    return true;
}

uint64_t SharedData::getAfterAbove()
{
    uint64_t after = 0;
    if (xSemaphoreTake((this->mSemaphoreStats), 300) == pdPASS)
    {
        after = this->afterRestartAbove;
        xSemaphoreGive((this->mSemaphoreStats));
    }
    return after;
}

bool SharedData::increment()
{
    if (xSemaphoreTake((this->mSemaphoreStats), 300) == pdPASS)
    {
        this->afterRestartAbove += 1;
        xSemaphoreGive((this->mSemaphoreStats));
        return true;
    }
    return false;
}
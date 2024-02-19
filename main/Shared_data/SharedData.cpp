#include "SharedData.h"

SharedData::SharedData()
{
    this->mSemaphoredB = xSemaphoreCreateMutex();
    this->mSemaphoreStats = xSemaphoreCreateMutex();
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
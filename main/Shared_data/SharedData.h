#ifndef __SHARED_DATA_H__
#define __SHARED_DATA_H__
#include <stdlib.h>
#include <stdio.h>
#include "string.h"
#include "string"
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

using namespace std;

class SharedData
{
private:
    SemaphoreHandle_t mSemaphoredB;
    SemaphoreHandle_t mSemaphoreStats;
    float actual_dB = 0;
    uint32_t totalyAbove = 0;
    uint32_t afterRestartAbove = 0;
public:
    SharedData();
    ~SharedData();
    float get_dB();
    bool set_dB(float dB);
};

#endif
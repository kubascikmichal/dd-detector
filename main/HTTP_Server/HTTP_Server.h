#ifndef __HTTP_SERVER__
#define __HTTP_SERVER__

#include <esp_http_server.h>
#include "../AP/AP.h"
#include "../ESP_NOW/ESP_NOW.h"
#include "../NVS/NVS.h"
#include "../Shared_data/SharedData.h"

class HTTP_Server
{
public:
    HTTP_Server(SharedData *data, NVS *nvs, ESP_NOW *now, AP *ap);
    bool init();
    void stop();

private:
    httpd_handle_t m_server = NULL;
};

#endif

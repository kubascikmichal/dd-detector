#include "NVS.h"

NVS::NVS(/* args */)
{
    this->xSemaphore = xSemaphoreCreateMutex();
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    this->init();
}

NVS::~NVS()
{
}

bool NVS::saveConfig(cJSON *config)
{
    char *json = cJSON_PrintUnformatted(config);
    if (json == NULL)
    {
        return false;
    }
    bool saved = false;
    if (xSemaphoreTake(this->xSemaphore, 300))
    {
        esp_err_t err = nvs_set_str(this->storage_handle, NVS_CONFIG_LABEL, json);
        if (err == ESP_OK)
        {
            err = nvs_commit(this->storage_handle);
        }
        saved = (err == ESP_OK);
        xSemaphoreGive(this->xSemaphore);
    }
    cJSON_free(json);
    return saved;
}

// Returns NULL when nothing is stored yet (first boot). The caller owns the result.
cJSON *NVS::getConfig()
{
    cJSON *ret = NULL;
    if (xSemaphoreTake(this->xSemaphore, 300))
    {
        size_t size = 0;
        esp_err_t err = nvs_get_str(this->storage_handle, NVS_CONFIG_LABEL, NULL, &size);
        if (err == ESP_OK && size > 0)
        {
            char *json = (char *)malloc(size);
            if (json != NULL)
            {
                if (nvs_get_str(this->storage_handle, NVS_CONFIG_LABEL, json, &size) == ESP_OK)
                {
                    ret = cJSON_Parse(json);
                }
                free(json);
            }
        }
        xSemaphoreGive(this->xSemaphore);
    }
    return ret;
}

uint64_t NVS::getTotalyOver()
{
    uint64_t total = 0;
    if (xSemaphoreTake(this->xSemaphore, 300))
    {
        esp_err_t err = nvs_get_u64(this->storage_handle, NVS_TOTAL_TRESHOLDS_LABEL, &total);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND)
        {
            ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        }
        xSemaphoreGive(this->xSemaphore);
    }
    return total;
}

bool NVS::saveTotalyOver(uint64_t p_total)
{
    bool saved = false;
    if (xSemaphoreTake(this->xSemaphore, 300))
    {
        esp_err_t err = nvs_set_u64(this->storage_handle, NVS_TOTAL_TRESHOLDS_LABEL, p_total);
        if (err == ESP_OK)
        {
            err = nvs_commit(this->storage_handle);
        }
        ESP_ERROR_CHECK_WITHOUT_ABORT(err);
        saved = (err == ESP_OK);
        xSemaphoreGive(this->xSemaphore);
    }
    return saved;
}

void NVS::init()
{
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &this->storage_handle);
    if (err != ESP_OK)
    {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    }
}

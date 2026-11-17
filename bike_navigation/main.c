#include "esp_err.h"
#include "nvs_flash.h"

#include "bluetooth.h"
#include "gps.h"
#include "navigation.h"

void app_main(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    ESP_ERROR_CHECK(err);

    bluetooth_init();

    gps_init();

    // BLE runs in its own task.
    // GPS continuously receives UART data here.
    gps_run();
}
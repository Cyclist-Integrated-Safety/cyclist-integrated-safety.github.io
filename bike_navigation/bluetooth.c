#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"

#include "bluetooth.h"
#include "navigation.h"

static uint8_t address_type;

// uuid for bike
static const ble_uuid128_t bike_service_uuid =
    BLE_UUID128_INIT(
        0x10, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xAB, 0xCD
    );

// route service uuid
static const ble_uuid128_t route_uuid =
    BLE_UUID128_INIT(
        0x11, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0xAB, 0xCD
    );

// route data from phone
static int write_route(uint16_t connection, uint16_t attribute, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    char route[101];

    // check for valid route
    int length = OS_MBUF_PKTLEN(ctxt->om);

    if (length == 0 || length > 100) {
        ESP_LOGW("BIKE_BLE", "Invalid route length");
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }

    // copy data into route string, check if read or not
    int status = os_mbuf_copydata(ctxt->om, 0, length, route);

    if (status != 0) {
        ESP_LOGE("BIKE_BLE", "Failed to read route data");
        return BLE_ATT_ERR_UNLIKELY;
    }

    // string end to print out
    route[length] = '\0';
    ESP_LOGI("BIKE_BLE", "Received route: %s", route);

    // start receiving a new route
    if (strcmp(route, "START") == 0) {
        nav_start();
        return 0;
    }

    // finished receiving route
    if (strcmp(route, "END") == 0) {
        if (nav_finish()) {
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    // extract latitude, longitude, and turn
    double latitude;
    double longitude;
    char turn;
    char extra;

    int values = sscanf(
        route,
        "%lf,%lf,%c%c",
        &latitude,
        &longitude,
        &turn,
        &extra
    );

    // check for three values
    if (values != 3) {
        ESP_LOGW("BIKE_BLE", "Invalid route point");
        return BLE_ATT_ERR_UNLIKELY;
    }

    // add route point to navigation
    if (!nav_add_point(latitude, longitude, turn)) {
        ESP_LOGW("BIKE_BLE", "Failed to add route point");
        return BLE_ATT_ERR_UNLIKELY;
    }

    return 0;
}

// define bt service and characteristic
static const struct ble_gatt_svc_def bike_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &bike_service_uuid.u,

        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &route_uuid.u,
                .access_cb = write_route,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            {0}
        },
    },
    {0}
};

static void ble_broadcast(void);

// event handler, rebroadcast after disconnect
static int ble_event(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        if (event->connect.status == 0) {
            ESP_LOGI("BIKE_BLE", "Phone connected");
        }
        else {
            ESP_LOGW("BIKE_BLE", "Connection failed");
            ble_broadcast();
        }
    }
    else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        ESP_LOGI("BIKE_BLE", "Phone disconnected");
        ble_broadcast();
    }

    return 0;
}

// Make ESP32 visible to other devices
static void ble_broadcast(void) {
    // 1. what information to broadcast
    struct ble_hs_adv_fields fields = {0};

    // tell other devices: allow general discovery, no bt classic
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    // get name, name length, and check if its complete for NimBLE
    fields.name = (uint8_t *)"BikeNav";
    fields.name_len = strlen("BikeNav");
    fields.name_is_complete = 1;

    int status = ble_gap_adv_set_fields(&fields);

    if (status != 0) {
        ESP_LOGE("BIKE_BLE", "Broadcast data failed");
        return;
    }

    // 2. how broadcast
    struct ble_gap_adv_params params = {0};

    // undirected connectable, general discoverability
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    // start broadcasting
    int broadcast_status = ble_gap_adv_start(
        address_type, NULL, BLE_HS_FOREVER,
        &params, ble_event, NULL
    );

    if (broadcast_status == 0) {
        ESP_LOGI("BIKE_BLE", "Broadcasting as BikeNav");
    }
    else {
        ESP_LOGE("BIKE_BLE", "Broadcast failed");
    }
}

// get address type, start broadcasting device
static void ble_ready(void) {
    int address_status = ble_hs_id_infer_auto(0, &address_type);

    if (address_status != 0) {
        ESP_LOGE("BIKE_BLE", "Address setup failed");
        return;
    }

    ble_broadcast();
}

// helper so nimble_port_run() doesnt block os
static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

// initialize bluetooth
void bluetooth_init(void) {
    // NVS is initialized in main.c

    // initialize bt
    ESP_ERROR_CHECK(nimble_port_init());

    // set bluetooth name for gap and gatt services
    // different from broadcast name
    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set("BikeNav");

    // resources for GATT, register service w/ NimBLE
    int status = ble_gatts_count_cfg(bike_services);

    if (status != 0) {
        ESP_LOGE("BIKE_BLE", "Service configuration failed");
        return;
    }

    status = ble_gatts_add_svcs(bike_services);

    if (status != 0) {
        ESP_LOGE("BIKE_BLE", "Service registration failed");
        return;
    }

    // run function when bluetooth ready
    ble_hs_cfg.sync_cb = ble_ready;

    // start task to run bluetooth separate from application
    nimble_port_freertos_init(ble_host_task);
}
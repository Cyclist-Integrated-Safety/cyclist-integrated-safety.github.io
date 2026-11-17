#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"

#include "gps.h"
#include "navigation.h"

#define GPS_UART UART_NUM_1
#define GPS_TX_PIN 2
#define GPS_RX_PIN 1

// NMEA coordinates to decimal degrees
static double convert_coordinate(double coordinate)
{
    int degrees = (int)(coordinate / 100);
    double minutes = coordinate - degrees * 100;

    return degrees + minutes / 60.0;
}

// Extract latitude, longitude, and speed
static void parse_gps(const char *sentence)
{
    // Only read RMC messages
    if (strncmp(sentence, "$GPRMC,", 7) != 0) {
        return;
    }

    char time[20];
    char status;

    double latitude;
    double longitude;
    double speed_knots;

    char north_south;
    char east_west;

    int result = sscanf(
        sentence,
        "$GPRMC,%19[^,],%c,%lf,%c,%lf,%c,%lf",
        time,
        &status,
        &latitude,
        &north_south,
        &longitude,
        &east_west,
        &speed_knots
    );

    if (result != 7 || status != 'A') {
        printf("Waiting for GPS fix...\n");
        return;
    }

    if ((north_south != 'N' && north_south != 'S') ||
        (east_west != 'E' && east_west != 'W')) {
        return;
    }

    // Convert coordinates
    latitude = convert_coordinate(latitude);
    longitude = convert_coordinate(longitude);

    // South and west are negative
    if (north_south == 'S') {
        latitude = -latitude;
    }

    if (east_west == 'W') {
        longitude = -longitude;
    }

    // Convert knots to mph
    // double speed_mph = speed_knots * 1.15078;

    // Send current location to navigation
    navigation_update(latitude, longitude);
}

// Configure GPS UART
void gps_init(void)
{
    uart_config_t gps_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    ESP_ERROR_CHECK(
        uart_driver_install(GPS_UART, 1024, 0, 0, NULL, 0)
    );

    ESP_ERROR_CHECK(
        uart_param_config(GPS_UART, &gps_config)
    );

    // ESP GPIO2 = TX, GPIO1 = RX
    ESP_ERROR_CHECK(
        uart_set_pin(
            GPS_UART,
            GPS_TX_PIN,
            GPS_RX_PIN,
            UART_PIN_NO_CHANGE,
            UART_PIN_NO_CHANGE
        )
    );

    printf("GPS UART initialized\n");
}

// Continuously receive GPS messages
void gps_run(void)
{
    uint8_t data[256];

    char sentence[128];
    int index = 0;
    int collecting = 0;

    while (1) {
        int length = uart_read_bytes(
            GPS_UART,
            data,
            sizeof(data),
            pdMS_TO_TICKS(1000)
        );

        for (int i = 0; i < length; i++) {
            char character = (char)data[i];

            // Start of NMEA sentence
            if (character == '$') {
                index = 0;
                collecting = 1;
            }

            if (!collecting) {
                continue;
            }

            // End of sentence
            if (character == '\n') {
                sentence[index] = '\0';

                parse_gps(sentence);

                index = 0;
                collecting = 0;
            }

            else if (character != '\r') {
                if (index < (int)sizeof(sentence) - 1) {
                    sentence[index++] = character;
                }
                else {
                    index = 0;
                    collecting = 0;
                }
            }
        }
    }
}
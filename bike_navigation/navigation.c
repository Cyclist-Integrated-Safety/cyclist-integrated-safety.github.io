#include <stdio.h>
#include <math.h>
#include <stdbool.h>

#include "navigation.h"

typedef struct {
    double lat;
    double lon;
    char turn;
} route_point;

static route_point route[10];
static int point_index = 0;
static int next_point_index = 0;
static bool loading = false;
static bool route_ready = false;

// start receiving route from bluetooth
void nav_start(void) {
    point_index = 0;
    next_point_index = 0;
    loading = true;
    route_ready = false;

    printf("Starting new route\n");
}

// add route point from bluetooth
bool nav_add_point(double lat, double lon, char turn) {
    if (!loading || point_index >= 10) {
        return false;
    }

    route[point_index].lat = lat;
    route[point_index].lon = lon;
    route[point_index].turn = turn;

    point_index++;

    printf("Route point added: %d\n", point_index);

    return true;
}

// finished receiving route
bool nav_finish(void) {
    if (!loading || point_index == 0) {
        return false;
    }

    loading = false;
    route_ready = true;

    printf("Route ready: %d points\n", point_index);

    return true;
}

// compare gps position to next turn
void navigation_update(double lat, double lon) {
    if (!route_ready || next_point_index >= point_index) {
        return;
    }

    route_point target = route[next_point_index];

    // approximate distance in meters
    double dx = (lon - target.lon) * 111320.0
                * cos(lat * 3.14159265 / 180.0);

    double dy = (lat - target.lat) * 111320.0;

    double distance = sqrt(dx * dx + dy * dy);

    printf("Distance: %.1f m\n", distance);
    printf("Next turn: %c\n", target.turn);

    if (distance < 15.0) {
        printf("Reached turn!\n");
        next_point_index++;

        if (next_point_index >= point_index) {
            printf("Route complete!\n");
            route_ready = false;
        }
    }
}
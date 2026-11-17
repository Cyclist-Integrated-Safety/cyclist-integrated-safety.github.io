#ifndef NAV_H
#define NAV_H

#include <stdbool.h>

void nav_start(void);
bool nav_add_point(double lat, double lon, char turn);
bool nav_finish(void);
void navigation_update(double lat, double lon);

#endif
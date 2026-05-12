#ifndef UPDATE_HANDLING_PART2_H
#define UPDATE_HANDLING_PART2_H

#include <Arduino.h>
#include "updateHandling.h"

void updateHandling_initWebserverEndpoints_Part2();
bool updateHandling_performUpdatePart2(update_info_t& updateInfo, String component, int componentInstanceIndex);

#endif
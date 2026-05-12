#ifndef UPDATE_HANDLING_PART1_H
#define UPDATE_HANDLING_PART1_H

#include <Arduino.h>
#include "updateHandling.h"

bool updateHandling_performUpdatePart1(update_info_t& updateInfo, String component, int componentInstanceIndex);

#endif
#ifndef UPDATE_HANDLING_PART2_H
#define UPDATE_HANDLING_PART2_H

#include <Arduino.h>

void updateHandling_Part2_initWebserverEndpoints();
bool updateHandling_Part2_enqueueUpdateTasks(int componentInstanceIndex);

#endif
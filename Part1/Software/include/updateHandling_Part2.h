#ifndef UPDATE_HANDLING_PART2_H
#define UPDATE_HANDLING_PART2_H

#include <Arduino.h>

extern update_info_t updateInfo_Part2;

void updateHandling_Part2_initWebserverEndpoints();
bool updateHandling_Part2_enqueueUpdateTasks(int componentInstanceIndex);
size_t updateHandling_Part2_getInstanceCount();
char* updateHandling_Part2_queryVersion(int componentInstanceIndex);

#endif
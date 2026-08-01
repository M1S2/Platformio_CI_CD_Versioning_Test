#ifndef UPDATE_HANDLING_PART1_H
#define UPDATE_HANDLING_PART1_H

#include <Arduino.h>

extern update_info_t updateInfo_Part1;

void updateHandling_Part1_initWebserverEndpoints(AsyncWebServer* p_server);
bool updateHandling_Part1_enqueueUpdateTasks(int componentInstanceIndex);
size_t updateHandling_Part1_getInstanceCount();
char* updateHandling_Part1_queryVersion(int componentInstanceIndex);

#endif
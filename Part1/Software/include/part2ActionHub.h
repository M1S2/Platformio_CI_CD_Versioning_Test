#ifndef PART2ACTIONHUB_H
#define PART2ACTIONHUB_H

#include <Arduino.h>

enum Part2ActionHubAction
{
    PART2ACTIONHUB_ACTION_NONE,
    PART2ACTIONHUB_ACTION_UPDATE
};

extern bool part2ActionHub_isAPOpen;
extern String part2ActionHub_ApSsid;
extern Part2ActionHubAction part2ActionHub_currentAction;

void part2ActionHub_initWebserverEndpoints();
bool part2ActionHub_startAP(Part2ActionHubAction currentAction);
bool part2ActionHub_stopAP();
bool part2ActionHub_handleAPTimeout();

#endif
#ifndef PART2ACTIONHUB_H
#define PART2ACTIONHUB_H

#include <Arduino.h>

extern bool part2ActionHub_isAPOpen;
extern String part2ActionHub_ApSsid;
extern uint32_t part2ActionHub_currentToken;

bool part2ActionHub_startAP();
bool part2ActionHub_stopAP();
bool part2ActionHub_handleAPTimeout();

#endif
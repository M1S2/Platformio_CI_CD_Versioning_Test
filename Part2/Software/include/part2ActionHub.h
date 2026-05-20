#ifndef PART2ACTIONHUB_H
#define PART2ACTIONHUB_H

#include <Arduino.h>

enum Part2ActionHubAction
{
    PART2ACTIONHUB_ACTION_NONE,
    PART2ACTIONHUB_ACTION_UPDATE
};

#define PART2ACTIONHUB_CURRENT_ACTION_ENDPOINT     "/actionHub/current_action"

bool part2ActionHub_runAction();

#endif
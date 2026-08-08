#ifndef REMOTEACTIONHUB_H
#define REMOTEACTIONHUB_H

#include <Arduino.h>

enum RemoteActionHubAction
{
    REMOTEACTIONHUB_ACTION_NONE,
    REMOTEACTIONHUB_ACTION_UPDATE
};

#define REMOTEACTIONHUB_CURRENT_ACTION_ENDPOINT     "/actionHub/current_action"

bool remoteActionHub_runAction();

#endif
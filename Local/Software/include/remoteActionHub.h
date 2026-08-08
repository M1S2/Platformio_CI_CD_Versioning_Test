#ifndef REMOTEACTIONHUB_H
#define REMOTEACTIONHUB_H

#include <Arduino.h>

enum RemoteActionHubAction
{
    REMOTEACTIONHUB_ACTION_NONE,
    REMOTEACTIONHUB_ACTION_UPDATE
};

extern bool remoteActionHub_isAPOpen;
extern String remoteActionHub_ApSsid;
extern RemoteActionHubAction remoteActionHub_currentAction;

void remoteActionHub_initWebserverEndpoints();
bool remoteActionHub_startAP(RemoteActionHubAction currentAction);
bool remoteActionHub_stopAP();
bool remoteActionHub_handleAPTimeout();

#endif
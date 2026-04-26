#ifndef UPDATE_HANDLING_H
#define UPDATE_HANDLING_H

#include <Arduino.h>
#include "config.h"

enum UpdateChannel
{
    UPDATE_CHANNEL_STABLE,
    UPDATE_CHANNEL_DEV
};

typedef struct update_info
{
    bool valid;
    String version;
    String url_fw;  // Firmware URL
    String url_fs;  // Filesystem URL (if applicable, otherwise empty)
} update_info_t;

extern UpdateChannel currentUpdateChannel;
extern update_info_t updateInfo;

void updateHandling_initWebserverEndpoints();
void updateHandling_loop();
void updateHandling_startFetchingNewestVersionInfos();
//bool otaPerformUpdate();

#endif
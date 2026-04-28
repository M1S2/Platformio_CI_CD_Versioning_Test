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
    bool valid;             // Whether the update info is valid (i.e. whether a valid manifest was fetched and parsed)
    String version;         // Firmware version (e.g. "1.0.0" or "dev-SW_v2.0.0-p2-856538c")
    String url_fw;          // Firmware URL
    String url_fs;          // Filesystem URL (if applicable, otherwise empty)
    String url_fw_sha256;   // SHA256 hash of the firmware binary for integrity check
    String url_fs_sha256;   // SHA256 hash of the filesystem binary for integrity check (if applicable, otherwise empty)
} update_info_t;

extern UpdateChannel currentUpdateChannel;
extern update_info_t updateInfo;

void updateHandling_initWebserverEndpoints();
void updateHandling_loop();
void updateHandling_startFetchingNewestVersionInfos();
//bool otaPerformUpdate();

#endif
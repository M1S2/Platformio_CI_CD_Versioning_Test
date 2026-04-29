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
    String componentName;   // Name of the component (e.g. "part1")
    bool valid;             // Whether the update info is valid (i.e. whether a valid manifest was fetched and parsed)
    String version;         // Firmware version (e.g. "1.0.0" or "dev-SW_v2.0.0-p2-856538c")
    bool has_fs_update;     // Whether the update includes a filesystem update (i.e. whether the manifest contains a valid URL for the filesystem binary)
    String url_fw;          // Firmware URL
    String url_fs;          // Filesystem URL (if applicable, otherwise empty)
    String fw_md5;          // MD5 hash of the firmware binary for integrity check
    String fs_md5;          // MD5 hash of the filesystem binary for integrity check (if applicable, otherwise empty)
    float updateProgress_fw; // Progress of the firmware update (0.0 to 100.0)
    float updateProgress_fs; // Progress of the filesystem update (0.0 to 100.0)
} update_info_t;

extern UpdateChannel currentUpdateChannel;
extern update_info_t updateInfo_Part1;

void updateHandling_initWebserverEndpoints();
void updateHandling_loop();
void updateHandling_startFetchingNewestVersionInfos();
void updateHandling_startUpdate();

#endif
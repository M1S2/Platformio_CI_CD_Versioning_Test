#ifndef UPDATE_HANDLING_H
#define UPDATE_HANDLING_H

#include <Arduino.h>
#include "config.h"

enum UpdateChannel
{
    UPDATE_CHANNEL_STABLE,
    UPDATE_CHANNEL_DEV
};

enum UpdateStep
{
    UPDATE_STEP_FW,
    UPDATE_STEP_FS
};

#define UPDATE_COMPONENT_PART1 "part1"
#define UPDATE_COMPONENT_PART2 "part2"

typedef struct update_status
{
    UpdateChannel currentUpdateChannel = UPDATE_CHANNEL_DEV;     // Current update channel (stable or dev)
    bool isFetchingNewestVersionInfos = false;  // This flag is true, when the device is currently fetching the newest version infos from the update server
    bool isUpdating = false;                    // This flag is true, when the device is currently performing an update
    UpdateStep updateStep = UPDATE_STEP_FW;     // Current step of the update process (firmware update or filesystem update)
    float updateProgress = 0.0f;                // Progress of the current or last firmware and filesystem update (0.0 to 100.0)
} update_status_t;

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
} update_info_t;

extern update_status_t updateStatus;
extern update_info_t updateInfo_Part1;
extern update_info_t updateInfo_Part2;

void updateHandling_initWebserverEndpoints();
void updateHandling_loop();
void updateHandling_startFetchingNewestVersionInfos();
void updateHandling_startUpdate();

#endif
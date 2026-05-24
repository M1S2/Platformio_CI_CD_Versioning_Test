#ifndef UPDATE_HANDLING_H
#define UPDATE_HANDLING_H

#include <Arduino.h>
#include "config.h"

enum UpdateChannel
{
    UPDATE_CHANNEL_STABLE,
    UPDATE_CHANNEL_DEV
};

enum UpdateState
{
    UPDATE_STATE_IDLE,
    UPDATE_STATE_CHECKING,
    UPDATE_STATE_UPDATING,
    UPDATE_STATE_RESTARTING,
    UPDATE_STATE_ERROR
};

enum UpdateStep
{
    UPDATE_STEP_NONE,
    UPDATE_STEP_PREPARE,
    UPDATE_STEP_WAIT,
    UPDATE_STEP_FW,
    UPDATE_STEP_FS,
    UPDATE_STEP_FINISHED
};

#define UPDATE_COMPONENT_NAME_PART1 "part1"
#define UPDATE_COMPONENT_NAME_PART2 "part2"

typedef struct update_status
{
    UpdateChannel currentUpdateChannel = UPDATE_CHANNEL_DEV;     // Current update channel (stable or dev)
    UpdateState state = UPDATE_STATE_IDLE;      // Current state of the update handling
    String currentComponent = "";               // Name of the component currently being updated (e.g. "part1" or "part2")
    int currentComponentInstanceIndex = -1;     // Index of the component instance currently being updated
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

void updateHandling_initWebserverEndpoints();
void updateHandling_loop();
void updateHandling_startFetchingNewestVersionInfos();
void updateHandling_startUpdate(String component, int componentInstanceIndex);

/**********************************************************************/

#define UPDATE_QUEUE_SIZE 10

typedef struct update_task
{
    String component = "";               // Name of the component targeted by the update (e.g. "part1" or "part2")
    int componentInstanceIndex = -1;     // Index of the component instance targeted by the update
} update_task_t;

class UpdateQueue
{
public:
    bool push(const update_task_t &task);
    bool pop(update_task_t &task);
    bool isEmpty() const;
    size_t size() const;
    void clear();

private:
    update_task_t queue[UPDATE_QUEUE_SIZE];
    size_t head = 0;
    size_t tail = 0;
    size_t count = 0;
};

#endif
#ifndef UPDATE_HANDLING_H
#define UPDATE_HANDLING_H

#include <Arduino.h>
#include "config.h"
#include "genericQueue.h"

enum UpdateChannels
{
    UPDATE_CHANNEL_STABLE,
    UPDATE_CHANNEL_DEV
};

enum UpdateComponents
{
    UPDATE_COMPONENT_NONE,
    UPDATE_COMPONENT_PART1,
    UPDATE_COMPONENT_PART2
};

enum UpdateStates
{
    UPDATE_STATE_IDLE,
    UPDATE_STATE_CHECKING,
    UPDATE_STATE_UPDATING,
    UPDATE_STATE_RESTARTING,
    UPDATE_STATE_ERROR
};

enum UpdateSteps
{
    UPDATE_STEP_NONE,
    UPDATE_STEP_PREPARE,
    UPDATE_STEP_WAIT,
    UPDATE_STEP_FW,
    UPDATE_STEP_FS,
    UPDATE_STEP_RESTART,
    UPDATE_STEP_FINISHED
};

#define UPDATE_COMPONENT_NAME_PART1 "part1"
#define UPDATE_COMPONENT_NAME_PART2 "part2"

typedef struct update_status
{
    UpdateChannels currentUpdateChannel = UPDATE_CHANNEL_DEV;       // Current update channel (stable or dev)
    UpdateStates state = UPDATE_STATE_IDLE;                         // Current state of the update handling
    UpdateComponents currentComponent = UPDATE_COMPONENT_NONE;      // Component currently being updated
    int currentComponentInstanceIndex = -1;                         // Index of the component instance currently being updated
    UpdateSteps updateStep = UPDATE_STEP_FW;                        // Current step of the update process (firmware update or filesystem update)
    float updateProgress = 0.0f;                                    // Progress of the current or last firmware and filesystem update (0.0 to 100.0)
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

/**********************************************************************/

// Forward declaration of update task struct and typedef (otherwise we would have a circular dependency between the update task struct and the update step handler function pointer typedef)
struct update_task;
typedef struct update_task update_task_t;

typedef bool (*update_step_handler_t)(update_task_t &task);

struct update_task
{
    UpdateComponents component = UPDATE_COMPONENT_NONE;     // Component targeted by the update
    int componentInstanceIndex = -1;                        // Index of the component instance targeted by the update
    UpdateSteps step = UPDATE_STEP_NONE;                    // Step of the update process
    update_step_handler_t handler = nullptr;                // Handler function to perform the update step
};

typedef bool (*update_enqueue_handler_t)(int componentInstanceIndex);
typedef struct
{
    UpdateComponents component;
    String componentName;
    update_info_t* updateInfo;
    update_enqueue_handler_t enqueueHandler;
} update_component_definition_t;

/**********************************************************************/

#define UPDATE_QUEUE_SIZE 15
extern GenericQueue<update_task_t, UPDATE_QUEUE_SIZE> updateTaskQueue;

/**********************************************************************/

void updateHandling_initWebserverEndpoints();
void updateHandling_loop();
bool updateHandling_startFetchingNewestVersionInfos();
void updateHandling_enqueueUpdateTasks(UpdateComponents component, int componentInstanceIndex);
bool updateHandling_enqueueSingleUpdateTask(UpdateComponents component, int instanceIndex, UpdateSteps step, update_step_handler_t handler);

#endif
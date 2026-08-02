#ifndef UPDATE_HANDLING_PART1_H
#define UPDATE_HANDLING_PART1_H

#include <Arduino.h>
#include "updateHandling.h"

class UpdateHandlingPart1 : public UpdateHandlingComponentBase
{
public:
    UpdateHandlingPart1(unsigned long backupRestoreTimeoutMs = 60000, const char** filesForBackup = nullptr, size_t filesForBackupCount = 0);

    bool enqueueUpdateTasks(int componentInstanceIndex = -1) override;
    size_t getInstanceCount() override;
    char* queryVersion(int componentInstanceIndex = -1) override;
    void initWebserverEndpoints(AsyncWebServer* p_server) override;

    const char** filesForBackup = nullptr;
    size_t filesForBackupCount = 0;
    
    // Flags used to synchronize with the web UI for backup/restore operations
    volatile bool fsBackupConfirmed = false;
    volatile bool fsRestoreConfirmed = false;

private:
    bool filenameMatchesPattern(const char* filename, const char* pattern);
    static bool performUpdateTask_FS(update_task_t& updateTask);
    static bool performUpdateTask_FW(update_task_t& updateTask);
    static bool performUpdateTask_BACKUP(update_task_t& updateTask);
    static bool performUpdateTask_RESTORE(update_task_t& updateTask);
    static bool performUpdateTask_RESTART(update_task_t& updateTask);

    static unsigned long backupRestoreTimeoutMs; // Timeout in ms for the backup/restore process. Use 0 to disable the timeout and keep the process active until it is manually stopped.
};

#endif
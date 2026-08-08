#ifndef UPDATE_HANDLING_REMOTE_H
#define UPDATE_HANDLING_REMOTE_H

#include <Arduino.h>
#include "updateHandling.h"

#define UPDATE_COMPONENTNAME_REMOTE "remote"  ///< The name of the update component for Remote part.

class UpdateHandlingRemote : public UpdateHandlingComponentBase
{
public:
    UpdateHandlingRemote();

    bool enqueueUpdateTasks(int componentInstanceIndex = -1) override;
    size_t getInstanceCount() override;
    char* queryVersion(int componentInstanceIndex = -1) override;
    void initWebserverEndpoints(AsyncWebServer* p_server) override;

    bool fwUpdateFinished = false;
    bool connectionEstablished = false;

private:
    static String calculateFileMD5(const String &filePath, UpdateHandling* p_updateHandling);
    static bool downloadFileToLittleFS(const String &url, const String &filePath, const String &expectedMd5, update_task_t& updateTask);

    static bool performUpdateTask_PREPARE(update_task_t& updateTask);
    static bool performUpdateTask_WAIT(update_task_t& updateTask);
    static bool performUpdateTask_FW(update_task_t& updateTask);
};

#endif
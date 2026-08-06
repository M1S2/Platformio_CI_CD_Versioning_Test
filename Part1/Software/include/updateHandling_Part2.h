#ifndef UPDATE_HANDLING_PART2_H
#define UPDATE_HANDLING_PART2_H

#include <Arduino.h>
#include "updateHandling.h"

#define UPDATE_COMPONENTNAME_PART2 "part2"  ///< The name of the update component for Part2. This macro defines the string identifier used to represent the Part2 update component in the update handling system.

class UpdateHandlingPart2 : public UpdateHandlingComponentBase
{
public:
    UpdateHandlingPart2();

    bool enqueueUpdateTasks(int componentInstanceIndex = -1) override;
    size_t getInstanceCount() override;
    char* queryVersion(int componentInstanceIndex = -1) override;
    void initWebserverEndpoints(AsyncWebServer* p_server) override;

    bool fwUpdateFinished = false;
    bool connectionEstablished = false;

private:
    static bool downloadFileToLittleFS(const String &url, const String &filePath, const String &expectedMd5, update_task_t& updateTask);

    static bool performUpdateTask_PREPARE(update_task_t& updateTask);
    static bool performUpdateTask_WAIT(update_task_t& updateTask);
    static bool performUpdateTask_FW(update_task_t& updateTask);
};

#endif
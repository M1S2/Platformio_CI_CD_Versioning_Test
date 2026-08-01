#ifndef UPDATE_HANDLING_CONFIG_H
#define UPDATE_HANDLING_CONFIG_H

#include "updateHandling.h"
#include "updateHandling_Part1.h"
#include "updateHandling_Part2.h"

/**********************************************************************/
/* UPDATE HANDLING */

#define UPDATE_STABLEBASEURL                    "https://github.com/M1S2/Platformio_CI_CD_Versioning_Test/releases/latest/download/"
#define UPDATE_DEVBASEURL                       "https://M1S2.github.io/Platformio_CI_CD_Versioning_Test/firmware/dev/"
#define UPDATE_MANIFESTFILENAME                 "manifest.json"
#define UPDATE_PART1BACKUPRESTORE_TIMEOUT_MS    60000                               // Timeout in ms for the part1 backup/restore process. Use 0 to disable the timeout and keep the process active until it is manually stopped.
#define UPDATE_BACKUP_FILES_ARRAY               { "testfile.txt", "data*.bin" }     // List of files/patterns to include in the backup. Supports both fixed paths ("/config.bin") and wildcard patterns ("/dataSensor*.bin").

/**********************************************************************/

inline const update_component_definition_t updateComponentDefinitions[] =
{
    {
        .component = UPDATE_COMPONENT_PART1,
        .componentName = "part1",
        .updateInfo = &updateInfo_Part1,
        .enqueueHandler = updateHandling_Part1_enqueueUpdateTasks,
        .getInstanceCountHandler = updateHandling_Part1_getInstanceCount,
        .queryVersionHandler = updateHandling_Part1_queryVersion,
        .initWebserverEndpointsHandler = updateHandling_Part1_initWebserverEndpoints
    },
    {
        .component = UPDATE_COMPONENT_PART2,
        .componentName = "part2",
        .updateInfo = &updateInfo_Part2,
        .enqueueHandler = updateHandling_Part2_enqueueUpdateTasks,
        .getInstanceCountHandler = updateHandling_Part2_getInstanceCount,
        .queryVersionHandler = updateHandling_Part2_queryVersion,
        .initWebserverEndpointsHandler = updateHandling_Part2_initWebserverEndpoints
    }
};

inline const size_t updateComponentDefinitionCount = sizeof(updateComponentDefinitions) / sizeof(updateComponentDefinitions[0]);

#endif

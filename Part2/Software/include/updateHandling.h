#ifndef UPDATE_HANDLING_H
#define UPDATE_HANDLING_H

#include <Arduino.h>

#define UPDATE_MD5_ENDPOINT                 "/update/part2_fw_md5"
#define UPDATE_BIN_ENDPOINT                 "/update/part2_fw.bin"
#define UPDATE_REPORT_PROGRESS_ENDPOINT     "/update/part2_report"

#define UPDATE_PROGRESS_INTERVALL_DURING_UPDATE_MS 200

bool updateHandling_performUpdate();

#endif
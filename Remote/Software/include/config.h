#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define REMOTEACTIONHUB_AP_NAME_BASE            "TestActionHub-"         // Base name for the remote action hub access point (should be the same as in the part1 software to find the correct AP)
#define REMOTEACTIONHUB_AP_PW                   "Act1onHub#PW"           // Password for the remote action hub access point (should be the same as in the part1 software)
#define REMOTEACTIONHUB_CONNECT_TIMEOUT_MS      15000                    // Timeout for connecting to the remote action hub access point in milliseconds

#define DEBUG_OUTPUT                                      // enable this define to print debugging output on the serial. If this is disabled, no serial output is used at all (to save power)

#endif
#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define CONNECTION_TIMEOUT_MS   10000               // Timeout in ms for connection to router
#define WIFI_HOSTNAME           "Test ESP"          // Name that is displayed for this device by the router
#define CONFIGURATION_AP_NAME   "Test ESP AP"       // Name for the configuration access point
#define CONFIGURATION_AP_PW     ""                  // Password for the configuration access point

#define DEBUG_OUTPUT                                // enable this define to print debugging output on the serial. If this is disabled, no serial output is used at all (to save power)

#endif
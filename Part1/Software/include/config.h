#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define CONNECTION_TIMEOUT_MS   10000               // Timeout in ms for connection to router
#define WIFI_HOSTNAME           "Test_ESP"          // Name that is displayed for this device by the router
#define CONFIGURATION_AP_NAME   "Test_ESP_AP"       // Name for the configuration access point
#define CONFIGURATION_AP_PW     ""                  // Password for the configuration access point
#define CONFIGURATION_PORTAL_TIMEOUT_MS 60000       // Timeout for the configuration portal (if this time is exceeded, the portal is closed and a new connection attempt to the router is made)

#define PART2ACTIONHUB_AP_NAME_BASE    "TestActionHub-"     // Base name for the part2 action hub access point (it is appended by the chip id to make it unique)
#define PART2ACTIONHUB_AP_PW           "Act1onHub#PW"       // Password for the part2 action hub access point
#define PART2ACTIONHUB_AP_TIMEOUT_MS   120000               // Timeout in ms for the part2 action hub access point. Use 0 to disable the timeout and keep the AP active until it is manually stopped.

#define DEBUG_OUTPUT                                // enable this define to print debugging output on the serial. If this is disabled, no serial output is used at all (to save power)

#endif
#ifndef TIME_HANDLING_H
#define TIME_HANDLING_H

#include <Arduino.h>
#include <time.h>

// Configuration of NTP
// https://werner.rothschopf.net/201802_arduino_esp8266_ntp.htm
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
#define TIME_NTP_SERVER "de.pool.ntp.org"
#define TIME_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

extern bool isTimeValid;       // This flag is set to true the first time the the NTP server is accessed

/**
 * Initalize the NTP time servers.
 */
void timeHandling_init();

/**
 * Print the given time on the serial output in a readable way.
 * @param time Time to print on the serial output.
 */
void timeHandling_printSerial(time_t time);

/**
 * Print the current time on the serial output in a readable way.
 */
void timeHandling_printNowSerial();

#endif
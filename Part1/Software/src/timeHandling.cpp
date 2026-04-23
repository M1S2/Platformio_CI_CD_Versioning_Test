#include "timeHandling.h"

bool isTimeValid = false;

// Callback that is called, when the NTP server was reached
// https://www.weigu.lu/microcontroller/tips_tricks/esp_NTP_tips_tricks/index.html
void time_is_set(bool from_sntp) 
{  
    isTimeValid = true;
}

// Adjust the update interval of the NTP server
uint32_t sntp_update_delay_MS_rfc_not_less_than_15000 ()
{
    return 1 * 60 * 60 * 1000UL; // 1 hour
}

void timeHandling_init()
{
    configTime(TIME_TZ, TIME_NTP_SERVER);
    settimeofday_cb(time_is_set); // ! optional  callback function to check
}

void timeHandling_printSerial(time_t time)
{
    #ifdef DEBUG_OUTPUT
        tm tm;                            // the structure tm holds time information in a more convenient way
        localtime_r(&time, &tm);          // update the structure tm with the current time
        Serial.print("year:");
        Serial.print(tm.tm_year + 1900);  // years since 1900
        Serial.print("\tmonth:");
        Serial.print(tm.tm_mon + 1);      // January = 0 (!)
        Serial.print("\tday:");
        Serial.print(tm.tm_mday);         // day of month
        Serial.print("\thour:");
        Serial.print(tm.tm_hour);         // hours since midnight  0-23
        Serial.print("\tmin:");
        Serial.print(tm.tm_min);          // minutes after the hour  0-59
        Serial.print("\tsec:");
        Serial.print(tm.tm_sec);          // seconds after the minute  0-61*
        Serial.print("\twday");
        Serial.print(tm.tm_wday);         // days since Sunday 0-6
        if (tm.tm_isdst == 1)             // Daylight Saving Time flag
        {
            Serial.print("\tDST");
        }
        else
        {
            Serial.print("\tstandard");
        }
        Serial.println();
    #endif
}

void timeHandling_printNowSerial() 
{
    #ifdef DEBUG_OUTPUT
        if(!isTimeValid)
        {
            Serial.println("Time wasn't synchronised yet.");
        }
        else
        {
            time_t now;                       // this are the seconds since Epoch (1970) - UTC
            time(&now);                       // read the current time
            timeHandling_printSerial(now);
        }
    #endif
}
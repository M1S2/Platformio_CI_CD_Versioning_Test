#include <Arduino.h>
#include "version.h"
#include "config.h"

bool isLedOn;
void setLedState(bool on)
{
    isLedOn = on;
    digitalWrite(LED_BUILTIN, isLedOn ? LOW : HIGH);
}
void toggleLedState()
{
    isLedOn = !isLedOn;
    setLedState(isLedOn);
}

/**********************************************************************/

void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    setLedState(true);    // LED on

    #ifdef DEBUG_OUTPUT
        Serial.begin(115200);
        Serial.println();
        Serial.print("[Main] FW Version: ");
        Serial.println(FW_VERSION);
    #endif
}

void loop()
{
    toggleLedState();
    delay(1000);
}
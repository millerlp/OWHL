// Get date and time using a DS3231, DS1307, DS1337 or DS1340 RTC connected via I2C


#include <Wire.h>
#include <SPI.h>  // not used here, but needed to prevent a RTClib compile error
#include "RTClib.h"

// Create the RTC object. 
// You may use RTC_DS1307 with the DS3231, DS1337, DS1340, Chronodot
// because those clock chips all use the same basic addresses. 
RTC_DS1307 rtc;    

void setup () {
    Serial.begin(57600); // Set serial port speed
    Wire.begin(); // Start the I2C
    rtc.begin(); // Init RTC
}

void loop () {
    DateTime now = rtc.now(); // Read the time and date from the clock chip
    
    Serial.print(now.year(), DEC);
    Serial.print('-');
    Serial.print(now.month(), DEC);
    Serial.print('-');
    Serial.print(now.day(), DEC);
    Serial.print(' ');
    Serial.print(now.hour(), DEC);
    Serial.print(':');
	if (now.minute() < 10) {
		Serial.print("0");
	}
    Serial.print(now.minute(), DEC);
    Serial.print(':');
	if (now.second() < 10) {
		Serial.print("0");
	}
    Serial.print(now.second(), DEC);
    Serial.println();
    
    delay(1000);
}
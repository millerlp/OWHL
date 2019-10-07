// Set date and time using a DS1307 RTC connected via I2C

// On an Arduino DUE, connect the clock chip the SDA1 and SCL1 lines
// known as "Wire1". 


#include <Wire.h>
#include <SPI.h>  // not used here, but needed to prevent a RTClib compile error
#include "RTClib.h"

RTC_DS1307 rtc;     // Setup an instance of DS1307 naming it RTC

void setup () {
    Serial.begin(57600); // Set serial port speed
    // Wire.begin(); // Start the I2C
    rtc.begin();  // Init RTC, includes a call to Wire.begin()
    rtc.adjust(DateTime(__DATE__, __TIME__));  // Time and date is expanded to date and time on your computer at compiletime
    Serial.print('Time and date set');
}

void loop () {
    DateTime now = rtc.now();
    
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
    
    delay(3000);
}

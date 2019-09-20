// Date, Time and Alarm functions using a Chronodot (DS3231) RTC connected via I2C and Wire lib

#include <Wire.h>
#include <SPI.h>  // not used here, but needed to prevent a RTClib compile error
#include <RTClib.h>

Chronodot rtc;

void setup () {
    Serial.begin(57600);
    // Wire.begin();
    rtc.begin();	// Includes a call to Wire.begin()
    
  rtc.adjust(DateTime(__DATE__, __TIME__));
  if (! rtc.isrunning()) {
    Serial.println("rtc is NOT running!");
    // following line sets the rtc to the date & time this sketch was compiled
    rtc.adjust(DateTime(__DATE__, __TIME__));
  }
  DateTime now = rtc.now();
  rtc.setAlarm1Simple(21, 58);
  rtc.turnOnAlarm(1);
  if (rtc.checkAlarmEnabled(1)) {
    Serial.println("Alarm Enabled");
  }
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

    if (rtc.checkIfAlarm(1)) {
      Serial.println("Alarm Triggered");
    }
    Serial.println();
    delay(3000);
}
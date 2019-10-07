// Date, Time and Alarm functions using a DS3231 RTC connected via I2C and Wire lib

#include <Wire.h>
#include <SPI.h>   // not used here, but needed to prevent a RTClib compile error
#include <avr/sleep.h>
#include <RTClib.h>


RTC_DS3231 rtc;
int INTERRUPT_PIN = 2;
volatile int state = LOW;

void setup () {
  pinMode(INTERRUPT_PIN, INPUT);
  //pull up the interrupt pin
  digitalWrite(INTERRUPT_PIN, HIGH);
  
  Serial.begin(57600);
  // Wire.begin();
  rtc.begin();	// Includes a call to Wire.begin()
    
  rtc.adjust(DateTime(__DATE__, __TIME__));
  DateTime now = rtc.now();
  rtc.setAlarm1Simple(22, 11);
  rtc.setAlarm2Simple(11, 10);
  rtc.turnOnAlarm(1);
  rtc.turnOnAlarm(2);
  if (rtc.checkAlarmEnabled(1) && rtc.checkAlarmEnabled(2)) {
    Serial.println("Alarms Enabled");
  }
  attachInterrupt(0, logData, LOW);
}

void loop () {
  DateTime now = rtc.now();
  if (rtc.checkIfAlarm(1) || rtc.checkIfAlarm(2)) {
    Serial.println("Alarm Triggered");
  }
  
  Serial.print(now.hour(), DEC);
  Serial.print(':');
  Serial.print(now.minute(), DEC);
  Serial.print(':');
  Serial.println(now.second(), DEC);
  Serial.println("Going to Sleep");
  delay(600);
  sleepNow();
  Serial.println("AWAKE");
}

void sleepNow() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  attachInterrupt(0,logData, LOW);
  sleep_mode();
  //HERE AFTER WAKING UP
  sleep_disable();
  detachInterrupt(0);
}

void logData() {
  //do something quick, flip a flag, and handle in loop();
}
/*
	Code contributed by Robert Lozyniak
	
	This sketch sets the date and time of a DS1307 or DS3231 real
	time clock chip.
	
	Note that this sketch doesn't use the RTClib library functions
	
	Usage: 
	1. Enter a year, month, day, hour, minute, and seconds values in the
	fields of "timeset" below. Enter the hour value using 24-hr 
	hour values. Make your time a minute or two in the future from
	the time you will upload the sketch to the Arduino.
	2. Once the sketch is uploaded, leave the Arduino plugged in
	to the computer. As the time that you set approaches on your 
	watch, hit the RESET button on the Arduino. You may need to hit 
	the button 1-2 seconds early to allow the bootloader time to run 
	before the sketch actually begins and sets the Real Time Clock value.
	If you time it right, the real time clock's value should exactly
	match your watch/computer time.
	3. After hitting RESET to set the clock, upload a different sketch 
	to the Arduino. The goal is to prevent this sketch 
	(settime_exact.ino) from running a 2nd time and 
	resetting the clock again. For example, try uploading the sketch 
	gettime.ino to check that the time was set correctly.


*/

#include "Wire.h"

// AVR-based Arduinos (Uno, MEGA etc) use the Wire bus for I2C
// with the pins located near the Aref pin. 
// SAM-based Arduinos (Due) call the bus attached to those
// same pins near Aref "Wire1", so this little block of code
// automagically swaps Wire or Wire1 in as necessary in the 
// main code below. 
#ifdef __AVR__
	#define WIRE Wire	// For AVR-based Arduinos
#elif defined(ARDUINO_SAM_DUE)
	#define WIRE Wire1	// for Arduino DUE, Wire1 I2C bus
#endif


void setup() {
	//****************************************************
	// Enter the date and time values on the line below
  int timeset[] = {2015,	6,	13,	15,	16,	0};
		//   year,  	mo,  	day,  hr,  min,  sec
	//****************************************************
  WIRE.begin();
  Serial.begin(57600);
  // program to precisely set Chronodot
  WIRE.beginTransmission(0x68); // address of DS3231 or DS1307
  WIRE.write(0x00); // select register
  
  WIRE.write(toBCD(timeset[5])); // seconds
  WIRE.write(toBCD(timeset[4])); // minutes
  WIRE.write(toBCD(timeset[3])); // hours
  WIRE.write(weekday(timeset[0],timeset[1],timeset[2])); // day of week
  WIRE.write(toBCD(timeset[2])); // day of month
  WIRE.write(toBCD(timeset[1])); // month
  WIRE.write(toBCD(timeset[0])); // year
  
  WIRE.endTransmission();
}

void loop() {
  delay(500);  
}


byte toBCD(int n) {
  int tens = (n/10)%10;
  int ones = n%10;
  return (byte)(tens*16+ones);
}


byte weekday(int y, int m, int d) {
  if ((y<1583) || (y>9999)) return 0;
  if (d<1) return 0;
  int l = ((y%(((y%100)==0)?400:4)==0)?1:0);
  int n = y + (y/4) - (y/100) + (y/400);
  switch (m) {
    case 1:  if (d>31) return 0;  n+=(1-l); break;
    case 2: if (d>28+l) return 0; n+=(4-l); break;
    case 3:  if (d>31) return 0;  n+= 4;    break;
    case 4:  if (d>30) return 0;  break;
    case 5:  if (d>31) return 0;  n+= 2; break;
    case 6:  if (d>30) return 0;  n+= 5; break;
    case 7:  if (d>31) return 0;  break;
    case 8:  if (d>31) return 0;  n+= 3; break;
    case 9:  if (d>30) return 0;  n+= 6; break;
    case 10: if (d>31) return 0;  n+= 1; break;
    case 11: if (d>30) return 0;  n+= 4; break;
    case 12: if (d>31) return 0;  n+= 6; break;
    default: return 0;
  }
  n += d;
  n = ((n+4)%7)+1;
  return n;  // 1 for Mon, 2 for Tue, ..., 7 for Sun
}



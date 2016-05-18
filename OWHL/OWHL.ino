/* OWHL.ino
 * 
	
	OWHL - Open Wave Height Logger
	Copyright (C) 2014  Luke Miller
	
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program.  If not, see http://www.gnu.org/licenses/
	
	Written under Arduino 1.0.5-r2
	Updated to run under Arduino 1.6.4
	https://github.com/millerlp/OWHL 
  -------------------------------------------------------------------
  The sketch:
  During setup, we enable the DS3231 RTC's 32.768kHz output hooked to 
  XTAL1 to provide a timed interrupt on TIMER2. 
  The DS3231 32kHz pin should be connected to XTAL1, with a 10k ohm 
  pull-up resistor between XTAL1 and +Vcc (3.3V). 

  We set the prescaler on TIMER2 so that it only interrupts
  every 0.25 second (or change the value of SAMPLES_PER_SECOND in 
  the preamble to sample 4, 2, or 1 times per second). 
  In the main loop, whenever TIMER2 rolls over and causes an
  interrupt (see ISR at the bottom), the statements in the
  main loop's if() statement should execute, including reading the
  time, reading the MS5803 sensor, printing those values to the
  microSD card, and then going to sleep via the goToSleep function.
  
  Change the STARTMINUTE and DATADURATION values in the preamble
  below to set when the unit should wake up and start taking 
  data, and for how long during each hour it should take data.
  For example, STARTMINUTE = 0 and DATADURATION = 30 would 
  take data every hour starting at minute 00, and record 30 
  minutes worth of data. For continuous recording, set 
  STARTMINUTE = 0 and DATADURATION = 60. 
	
  The character string stored in "missionInfo" will be written
  to the 1st header row of every output file. You may change it
  here or reset it using a settings.txt file on the SD card.
  
  The startMinute, dataDuration, and missionInfo may optionally
  be changed by including a settings.txt file on the root 
  directory of the SD card. The file should always be named
  settings.txt. It should consist of one line with three
  values: startMinute, dataDuration, missionInfo, each separated
  by commas.
  startMinute should be a numeric value 0 to 59.
  dataDuration should be a numeric value 1 to 60. 
  missionInfo is a character/numeric entry that you can use 
  to describe this mission. Do not use commas inside the
  missionInfo entry. 
  An example settings.txt file would look like this:
  0,60,JFK Pier 1
  
  That makes the logger start logging at minute 0, and 
  log for 60 minutes (i.e. continuous logging all day), 
  and the "JFK Pier 1" will be inserted into the 1st header
  row of every output file produced. 
  
  When not recording, the sketch goes into a lower power 
  sleep (lowPowerSleep) mode using the watchdog timer set 
  to 8 second timeouts to conserve power further.
  
  If a buzzer is present on AVR pin PD7 and a (reed) switch
  is present on pin PD3, the sketch will beep the buzzer 10 
  times and flash the LED to indicate that the logger is alive
  while running. If the logger is in a lowPowerSleep state with
  8 second timeouts, the buzzer will only beep every 8 seconds
  after the reed switch is activated.
  
  Status indicators, in order of appearance:
  1. Real time clock not set: 
		Error LED on, Status LED blinks, slow beep
  2. SD card not found: 
		Error LED + Status LED blink, fast beep
  3a. settings.txt not found, using defaults:
		No indication
  3b. settings.txt read successfully: 
		Status LED flashes 10 times
  3c. settings.txt read, but invalid entries: 
		Error LED flashes 5 times
  4. Data collection is beginning:
		Buzzer plays a little tune,
		followed by 10 beeps indicating data collection
 */
#include "SdFat.h" // https://github.com/greiman/SdFat or https://github.com/millerlp/SdFat
#include <SPI.h> // stock Arduino library
#include <Wire.h> // stock Arduino library
#include "RTClib.h" // https://github.com/millerlp/RTClib
#include "MS5803_14.h" // https://github.com/millerlp/MS5803_14

#include <EEPROM.h>
// The following libraries should come with the normal Arduino 
// distribution. 
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/atomic.h>
#include <wiring_private.h>
#include <avr/wdt.h>

#define ECHO_TO_SERIAL // Echo data to serial port, comment out this line to turn off
// Turning off ECHO_TO_SERIAL will save battery power, always turn it off 
// during real deployments. 

#define REEDSW 3 // Arduino pin D3, AVR pin PD3, aka INT1
#define ERRLED 5 // Arduino pin D5, AVR pin PD5
#define LED 6 	  // Arduino pin D6, AVR pin PD6
#define BUZZER 7 // Arduino pin D7, AVR pin PD7

#define STARTMINUTE 0 // minute of hour to start taking data, 0 to 59
#define DATADURATION 60 // # of minutes to collect data, 1 to 60

#define SAMPLES_PER_SECOND 4// number of samples taken per second (4, 2, or 1)


const byte chipSelect = 10; // define the Chip Select pin for SD card
// Declare initial name for output files written to SD card
// The newer versions of SdFat library support long filenames
char filename[] = "YYYYMMDD_HHMM_00.CSV";
// Define name of settings file that may appear on SD card
char setfilename[] = "settings.txt";  
// Define an array to hold the 17-digit serial number that may be in eeprom
char serialnumber[17];


// Create variables to hold settings from SD card settings.txt file
#define arraylen 50 // maximum number of characters in missionInfo array
uint8_t startMinute; // changeable value for startMinute from settings.txt
uint8_t dataDuration; // changeable value for dataDuration from settings.txt
// Define character array to hold missionInfo from settings.txt
char missionInfo[arraylen] = "Default mission information for csv file header"; 
// Define a flag to show whether the serialnumber value is value or just zeros
bool serialValid = 0;

// Declare data arrays
uint32_t unixtimeArray[SAMPLES_PER_SECOND]; // store unixtime values temporarily
byte fracSecArray[SAMPLES_PER_SECOND]; // store fracSec values temporarily
float pressureArray[SAMPLES_PER_SECOND]; // store pressure readings temporarily
float tempCArray[SAMPLES_PER_SECOND]; // store temperature readings temporarily
byte loopCount = 0; // counter to keep track of data sampling loops

byte heartBeatFlag = 0; // flag to keep track of heartbeat interrupt status
byte heartBeatCount = 0; // counter to keep track of heartbeat loops

//-------------------------------------------------
// Buzzer frequency (Hz) and duration (milliseconds). 
unsigned int frequency = 8000;
unsigned long duration = 12;
// Additional global variables used by the buzzer function
volatile long timer1_toggle_count;
volatile uint8_t *timer1_pin_port;
volatile uint8_t timer1_pin_mask;
//------------------------------------------------

byte precision = 2; // decimal precision for stored pressure data values
char pressBuffer [10]; // character buffer to hold string-converted pressure
char tempBuffer [7]; // character buffer to hold string-converted temperature

volatile int f_wdt = 1; // TIMER2 flag (watchdog or TIMER2 counter)
volatile byte fracSec = 0; // fractional second count for timestamp (0,25,50,75)

// endMinute is the minute value after which the program should stop taking
// data for the remainder of the hour
uint8_t endMinute = STARTMINUTE + DATADURATION - 1;
// wakeMinute is the minute value when the program should start preparing to
// wake up and take data again. This will be set during the setup loop.
uint8_t wakeMinute = 59;

DateTime oldtime; // used to track time in main loop
DateTime newtime; // used to track time
uint8_t oldday; // used to track when a new day starts

// Create RTC object
RTC_DS3231 RTC; 
// Create MS_5803 object
MS_5803 sensor = MS_5803(512); // the argument to MS_5803 sets the precision (256,512,1024,4096)
// Create sd objects
SdFat sd;
SdFile logfile;  // for sd card, this is the file object to be written to
SdFile setfile; // for sd card, this is the settings file to read from

 
//---------------------------------------------------------
// SETUP loop
void setup() {
	
	Serial.begin(57600);
	pinMode(chipSelect, OUTPUT);  // set chip select pin for SD card to output
	// Set indicator LEDs as output
	pinMode(LED, OUTPUT);
	pinMode(ERRLED, OUTPUT);
	// Set interrupt 0 (Arduino pin D2) as input, use internal pullup resistor
	// interrupt 0 is used to put the unit into permanent sleep, stopping sampling
	pinMode(2, INPUT_PULLUP);
	
	// Set interrupt 1 (Arduino pin D3) as input, use internal pullup resistor
	// Interrupt 1 is used to activate the heartbeat function that notifies the 
	// user if the unit is taking data.
	pinMode(3, INPUT_PULLUP);
	
	//--------RTC SETUP ------------
	// In this section the AVR will check to see if the real time clock
	// is running. The clock should normally have been previously set with a 
	// separate sketch. If the clock is not running, the error LED
	// will turn on permanently, the status led will blink slowly, and the
	// buzzer will play a low-pitched beep. The goal here is to force
	// the user to have explicitly set the clock in the 
	// desired time zone, and then not allow this sketch to alter that
	// value unexpectedly. If you need to check the time, you can start
	// the OWHL running, then stop it and read the time stamps in the
	// microSD data file. 
	// If you need to set your DS3231 real time clock for the first time,
	// make sure you have the RTClib library installed (a copy can be found at
	// https://github.com/millerlp/RTClib ). In the examples folder of 
	// RTClib you will find the sketch settime_exact.ino, for setting the 
	// clock (follow the instructions in the comments of that sketch) and
	// the sketch gettime.ino for reading out the clock time via serial 
	// monitor. 
	Wire.begin();
	RTC.begin(); // Start DS3231 real time clock
	// Check to see if DS3231 RTC is running
	if (! RTC.isrunning()) {
		digitalWrite(ERRLED, HIGH);
		frequency = 2000;
		while(1){ // infinite loop due to RTC initialization error
				digitalWrite(LED, HIGH);
				delay(400);
				digitalWrite(LED, LOW);
				delay(400);
				beepbuzzer();
		}
	} 
	DateTime starttime = RTC.now(); // get initial time
	// Perform a second simple check to see if the running clock
	// is out of date. A running clock that has not been properly 
	// set will default to a date of 2000-01-01. A clock that has
	// lost its battery backup and then had power reapplied will
	// often come up with a 2000-01-01 date.
	if (starttime.year() < 2015){
		digitalWrite(ERRLED, HIGH);
		frequency = 3000;
		while(1){ // infinite loop due to RTC initialization error
				digitalWrite(LED, HIGH);
				delay(400);
				digitalWrite(LED, LOW);
				delay(400);
				beepbuzzer();
		}
	}

	RTC.enable32kHz(false); // Stop 32.768kHz output from DS3231 for now
	
	// The DS3231 can also put out several different square waves
	// on its SQW pin (1024, 4096, 8192 Hz), though I don't use them
	// in this sketch. The code below disables the SQW output to make
	// sure it's not using any extra power.
	RTC.enableOscillator(true, false, 0);

#ifdef ECHO_TO_SERIAL	
	// Print time to serial monitor. 
	printTime(starttime);
	Serial.println();
#endif	
	//-------End RTC SETUP-----------

	// Initialize the SD card object
	// Try SPI_FULL_SPEED, or SPI_HALF_SPEED if full speed produces
	// errors on a breadboard setup. 
	if (!sd.begin(chipSelect, SPI_FULL_SPEED)) {
	// If the above statement returns FALSE after trying to 
	// initialize the card, enter into this section and
	// hold in an infinite loop.
		while(1){ // infinite loop due to SD card initialization error
                        digitalWrite(ERRLED, HIGH);
                        delay(100);
                        digitalWrite(ERRLED, LOW);
                        digitalWrite(LED, HIGH);
                        delay(100);
                        digitalWrite(LED, LOW);
						beepbuzzer();
		}
	}
	
	// Read the startMinute, dataDuration, and missionInfo from the
	// settings.txt file on the SD card, if present. 
	getSettings();
	
	//--------------------------------------------------------------
	// Calculate what minute of the hour the program should turn the 
	// accurate timer back on to start taking data again.
	if (startMinute == 0) {
		wakeMinute = 59;
	} else {
		wakeMinute = startMinute - 1;
	}
	// If the getSettings() function returned new values for
	// startMinute and dataDuration, recalculate endMinute value
	endMinute = startMinute + dataDuration - 1;
	// if endMinute value is above 60, just set it to 59
	if (endMinute >= 60) endMinute = 59;
	
	// Start MS5803 pressure sensor
	sensor.initializeMS_5803();
	
	//-------------------------------------------------
	// Play a little ditty on successful startup
	// Set Buzzer pin as output
	pinMode(BUZZER, OUTPUT);
	duration = 200;
	frequency = 1000;
	// Play a little ditty on successful startup
	for (int i = 1000; i <=6000; i = i+500){
		beepbuzzer();
		delay(250);
		frequency = i;
	}
	duration = 25; // reset buzzer beep duration
	frequency = 4000; // reset buzzer frequency
	//-------------------------------------------------------
	// Set the heartBeatFlag to 1, which will cause it to
	// initially beep during the first few seconds of data
	// collection. 
	heartBeatFlag = 1; // Immediately beep on start up.
	
	//--------------------------------------------------------
	// Check current time, branch the rest of the setup loop
	// depending on whether it's time to take data or 
	// go to sleep for a while
	newtime = RTC.now();

	if (newtime.minute() >= startMinute && newtime.minute() <= endMinute){
		// Create new file on SD card using initFileName() function
		// This creates a filename based on the year, month, day,
		// hour, and minute, 
		// with a number indicating which number file this is on
		// the given day. The format of the filename is
		// YYYYMMDD_HHMM_XX.CSV where XX is the counter (00-99). The
		// function also writes the column headers to the new file.
		initFileName(newtime);
#ifdef ECHO_TO_SERIAL		
		Serial.print("Writing to ");
		Serial.println(filename);
#endif
		// Start 32.768kHz clock signal on TIMER2. 
		// Supply the current time value as the argument.
		newtime = startTIMER2(newtime);
		// Initialize oldtime and oldday values
		oldtime = newtime;
		oldday = oldtime.day();
		// set f_wdt flag to 1 so we start taking data in the main loop
		f_wdt = 1;
		
	} else if ( (newtime.minute() < startMinute) | (newtime.minute() > endMinute) ){
		// The current minute is earlier or later in the hour than the user has 
		// specified for taking data.
		oldtime = newtime;
		oldday = oldtime.day();

		cbi(TIMSK2,TOIE2); // disable the TIMER2 interrupt on overflow
		// Initialize oldtime and oldday values
		oldtime = newtime;
		oldday = oldtime.day();
		//  Enter the low power sleep cycle in the main loop
		// by setting f_wdt = 2.
		f_wdt = 2;	  
		
	}



}

//--------------------------------------------------------------
// Main loop
// We will enter the if() statement immediately when we get to the main loop for
// the first time, because f_wdt was set = 1 or 2 during the setup loop.
void loop() {
	//1111111111111111111111111111111111111111111111111111111111111111111111111
	//1111111111111111111111111111111111111111111111111111111111111111111111111
	if (f_wdt == 1) {
	
		f_wdt = 0; // reset interrupt flag
		// bitSet(PINC, 1); // used to visualize timing with LED or oscilloscope

		// Get a new time reading from the real time clock
		newtime = RTC.now();
		
		//*********************************************************************
		//*********************************************************************
		// If the current minute is >= than startMinute and <= endMinute, 
		// then take data
		if (newtime.minute() >= startMinute && newtime.minute() <= endMinute) {
			// Check to see if the current seconds value
			// is equal to oldtime.second(). If so, we
			// are still in the same second. If not,
			// the fracSec value should be reset to 0
			// and oldtime updated to equal newtime.
			if (oldtime.second() != newtime.second()) {
				fracSec = 0; // reset fracSec
				oldtime = newtime; // update oldtime
				loopCount = 0; // reset loopCount				
			}
			
			// Save current time to unixtimeArray
			unixtimeArray[loopCount] = newtime.unixtime();
			fracSecArray[loopCount] = fracSec;

			// Use readSensor() function to get pressure and temperature reading
			sensor.readSensor();
			pressureArray[loopCount] = sensor.pressure();
			tempCArray[loopCount] = sensor.temperature();

			if (fracSec == 0) {
				if (heartBeatFlag) {
					heartBeat(); // call the heartBeat function
					delay(duration); // keep the AVR awake long enough to play the sound
				}
			}

			// Now if loopCount is equal to the value in SAMPLES_PER_SECOND
			// (minus 1 for zero-based counting), then write out the contents
			// of the sample data arrays to the SD card. This should write data
			// every second
			if (loopCount >= (SAMPLES_PER_SECOND - 1)) {
				// Check to see if a new day has started. If so, open a new file
				// with the initFileName() function
				if (oldtime.day() != oldday) {
					// Close existing file
					logfile.close();
					// Generate a new output filename based on the new date
					initFileName(oldtime);
					// Update oldday value to match the new day
					oldday = oldtime.day();
				}
				// Call the writeToSD function to output the data array contents
				// to the SD card
				writeToSD();
#ifdef ECHO_TO_SERIAL
				printTime(newtime);
				Serial.print("\t");
				Serial.println(sensor.pressure());
				delay(5);
#endif				
			} // end of if (loopCount >= (SAMPLES_PER_SECOND - 1))

			// increment loopCount after writing all the sample data to
			// the arrays
			loopCount++; 
			
			// Increment the fractional seconds count
#if SAMPLES_PER_SECOND == 4
			fracSec = fracSec + 25;
#endif

#if SAMPLES_PER_SECOND == 2
			fracSec = fracSec + 50;
#endif
			goToSleep(); // call the goToSleep function (below)

		//***************************************************************
		//***************************************************************	
		} else if (newtime.minute() < startMinute){
			// If it is less than startMinute time, check to see if 
			// it is wakeMinute time. If so, use goToSleep, else if not use
			// lowPowerSleep
			//================================================================
			//================================================================
			if (newtime.minute() == wakeMinute){
				if (heartBeatFlag) {
					heartBeat(); // call the heartBeat function
					delay(duration); // delay long enough to play the sound
				}
				// Since it is the wakeMinute, we'll idle in the 
				// goToSleep cycle (returning f_wdt = 1 on interrupt from
				// TIMER2) until the minute rolls over to start recording
				// data.
				goToSleep();
			//================================================================
			//================================================================
			} else if (newtime.minute() != wakeMinute){
				if (heartBeatFlag) {
					heartBeat(); // call the heartBeat function
					delay(duration); // delay long enough to play the sound
				}
				TIMSK2 = 0; // stop TIMER2 interrupts
				// Turn off the RTC's 32.768kHz clock signal
				RTC.enable32kHz(false);
				// Go into low power sleep mode with watchdog timer
				lowPowerSleep();
				// From here, f_wdt will be set to 2 on interrupt from the
				// watchdog timer, and the code in the (f_wdt == 2) statement
				// below will be executed
			}
		//***************************************************************
		//***************************************************************
		} else if (newtime.minute() > endMinute && newtime.minute() != wakeMinute){
			// If the newtime minute is later than endMinute, and it's not a wakeMinute,
			// then shut down the 32kHz oscillator and go into lowPowerSleep mode with
			// the watchdog timer.
			if (heartBeatFlag) {
				heartBeat(); // call the heartBeat function
				delay(duration); // delay long enough to play the sound
			}
			TIMSK2 = 0; // stop TIMER2 interrupts
			// If we are past endMinute, enter lowPowerSleep (shuts off TIMER2)
			// Turn off the RTC's 32.768kHz clock signal
			RTC.enable32kHz(false);
			// Go into low power sleep mode with watchdog timer
			// The watchdog interrupt will return f_wdt = 2 after this point
			lowPowerSleep();
		//******************************************************************
		//******************************************************************	
		} else if (newtime.minute() == wakeMinute && wakeMinute == 59  && endMinute != wakeMinute){
			// Handle the special case when logging should start on minute 0,
			// but wakeMinute will be 59, and therefore isn't less than
			// startMinute. Reenter goToSleep mode. The third test, with
			// endMinute != wakeMinute, takes care of the additional special case
			// where the user wants to record continuously from 0 to 59 minutes,
			// so the endMinute would be equal to wakeMinute (that case should be
			// dealt with by the first statement in the block above where data 
			// collection happens). 
				if (heartBeatFlag) {
					heartBeat(); // call the heartBeat function
					delay(duration); // delay long enough to play sound
				}			
			goToSleep();
			
		} // end of if(f_wdt == 1) statement
//22222222222222222222222222222222222222222222222222222222222222222222222222222
//22222222222222222222222222222222222222222222222222222222222222222222222222222		
// Below here we are coming back from lowPowerSleep mode, when f_wdt == 2	
// This simply checks to see if it's a wakeMinute or not, and then either
// enters lowPowerSleep (not wakeMinute) or restarts TIMER2 and goes to 
// the goToSleep mode (is wakeMinute).		
	} else if (f_wdt == 2) {
		// When f_wdt == 2, we are coming back from low-power sleep mode
		f_wdt = 0; // always reset f_wdt flag value
		
		// Get a new time reading from the real time clock
		newtime = RTC.now();
		//===================================================================
		//===================================================================
		if (newtime.minute() != wakeMinute){
			// Flash LED to indicate that we've come back from low
			// power sleep mode (and are about to go back into it).
			digitalWrite(LED, HIGH);
			delay(3);
			digitalWrite(LED, LOW);
			
			if (heartBeatFlag) {
				heartBeat(); // call the heartBeat function
				delay(duration); // delay long enough to play sound
			}			
			// If it is not yet the wakeMinute, just go back to 
			// low power sleep mode
			lowPowerSleep();
		//===================================================================
		//===================================================================
		} else if (newtime.minute() == wakeMinute){
			if (heartBeatFlag) {
				heartBeat(); // call the heartBeat function
				delay(duration); //delay long enough to play sound
			}
			
			// If it is the wakeMinute, restart TIMER2
			newtime = startTIMER2(newtime);
			
			// Go back to sleep with TIMER2 interrupts activated
			goToSleep();
			// From here, f_wdt will be set to 1 on each interrupt from TIMER2
			// and the data-taking code above will be used
		}

	}
	// Set interrupt on pin D2, called when D2 goes low. Causes program to
	// stop completely (see endRun function below).
	attachInterrupt (0, endRun, LOW);

}
// END OF MAIN LOOP
//--------------------------------------------------------------


//-----------------------------------------------------------------------------
// FUNCTIONS

//-----------------------------------------------------------------------------
// This Interrupt Service Routine (ISR) is called every time the
// TIMER2_OVF_vect goes high (==1), which happens when TIMER2
// overflows. The ISR doesn't care if the AVR is awake or
// in SLEEP_MODE_PWR_SAVE, it will still roll over and run this
// routine. If the AVR is in SLEEP_MODE_PWR_SAVE, the TIMER2
// interrupt will also reawaken it. This is used for the goToSleep() function
ISR(TIMER2_OVF_vect) {
	if (f_wdt == 0) { // if flag is 0 when interrupt is called
		f_wdt = 1; // set the flag to 1
	} 
}

//-----------------------------------------------------------------------------
// goToSleep function. When called, this puts the AVR to
// sleep until it is awakened by an interrupt (TIMER2 in this case)
// This is a higher power sleep mode than the lowPowerSleep function uses.
void goToSleep()
{
	// Create three variables to hold the current status register contents
	byte adcsra, mcucr1, mcucr2;
	// Cannot re-enter sleep mode within one TOSC cycle. 
	// This provides the needed delay.
	OCR2A = 0; // write to OCR2A, we're not using it, but no matter
	while (ASSR & _BV(OCR2AUB)) {} // wait for OCR2A to be updated
	// Set the sleep mode to PWR_SAVE, which allows TIMER2 to wake the AVR
	set_sleep_mode(SLEEP_MODE_PWR_SAVE);
	adcsra = ADCSRA; // save the ADC Control and Status Register A
	ADCSRA = 0; // disable ADC
	sleep_enable();
	// Do not interrupt before we go to sleep, or the
	// ISR will detach interrupts and we won't wake.
	noInterrupts ();
	
	wdt_disable(); // turn off the watchdog timer
	
	//ATOMIC_FORCEON ensures interrupts are enabled so we can wake up again
	ATOMIC_BLOCK(ATOMIC_FORCEON) { 
		// Turn off the brown-out detector
		mcucr1 = MCUCR | _BV(BODS) | _BV(BODSE); 
		mcucr2 = mcucr1 & ~_BV(BODSE);
		MCUCR = mcucr1; //timed sequence
		// BODS stays active for 3 cycles, sleep instruction must be executed 
		// while it's active
		MCUCR = mcucr2; 
	}
	// We are guaranteed that the sleep_cpu call will be done
	// as the processor executes the next instruction after
	// interrupts are turned on.
	interrupts ();  // one cycle, re-enables interrupts
	sleep_cpu(); //go to sleep
	//wake up here
	sleep_disable(); // upon wakeup (due to interrupt), AVR resumes here

}

//-----------------------------------------------------------------------------
// lowPowerSleep function
// This sleep version uses the watchdog timer to sleep for 8 seconds at a time
// Because the watchdog timer isn't super accurate, the main program will leave
// this sleep mode once you get within 1 minute of the start time for a new 
// sampling period, and start using the regular goToSleep() function that 
// uses the more accurate TIMER2 clock source. 
void lowPowerSleep(void){

	/* It seems to be necessary to zero out the Asynchronous clock status 
	 * register (ASSR) before enabling the watchdog timer interrupts in this
	 * process. 
	 */
	ASSR = 0;  
	TIMSK2 = 0; // stop timer 2 interrupts
	// Cannot re-enter sleep mode within one TOSC cycle. 
	// This provides the needed delay.
	OCR2A = 0; // write to OCR2A, we're not using it, but no matter
	while (ASSR & _BV(OCR2AUB)) {} // wait for OCR2A to be updated

	ADCSRA = 0;   // disable ADC
	set_sleep_mode (SLEEP_MODE_PWR_DOWN);  // specify sleep mode
	sleep_enable();
	// Do not interrupt before we go to sleep, or the
	// ISR will detach interrupts and we won't wake.
	noInterrupts ();
	//--------------------------------------------------------------------------
	// Set up Watchdog timer for long term sleep

	// Clear the reset flag first
	MCUSR &= ~(1 << WDRF);

	// In order to change WDE or the prescaler, we need to
	// set WDCE (This will allow updates for 4 clock cycles).
	WDTCSR |= (1 << WDCE) | (1 << WDE);
	// Enable watchdog interrupt (WDIE), and set 8 second delay
	WDTCSR = bit(WDIE) | bit(WDP3) | bit(WDP0); 
	wdt_reset();

	// Turn off brown-out enable in software
	// BODS must be set to one and BODSE must be set to zero within four clock 
	// cycles, see section 10.11.2 of 328P datasheet
	MCUCR = bit (BODS) | bit (BODSE);
	// The BODS bit is automatically cleared after three clock cycles
	MCUCR = bit (BODS);
	// We are guaranteed that the sleep_cpu call will be done
	// as the processor executes the next instruction after
	// interrupts are turned on.
	interrupts ();  // one cycle, re-enables interrupts
	sleep_cpu ();   // one cycle, going to sleep now, wake on interrupt
	// The AVR is now asleep. In SLEEP_MODE_PWR_DOWN it will only wake
	// when the watchdog timer counter rolls over and creates an interrupt
	//-------------------------------------------------------------------
	// disable sleep as a precaution after waking
	sleep_disable();
}

//--------------------------------------------------------------------------
// Interrupt service routine for when the watchdog timer counter rolls over
// This is used after the user has entered the lowPowerSleep function.
ISR(WDT_vect) {
	if (f_wdt == 0) { // if flag is 0 when interrupt is called
		f_wdt = 2; // set the flag to 2
	} 
}

//--------------------------------------------------------------
// startTIMER2 function
// Starts the 32.768kHz clock signal being fed into XTAL1 to drive the
// quarter-second interrupts used during data-collecting periods. 
// Supply a current DateTime time value. 
// This function returns a DateTime value that can be used to show the 
// current time when returning from this function. 
DateTime startTIMER2(DateTime currTime){
	TIMSK2 = 0; // stop timer 2 interrupts

	RTC.enable32kHz(true);
	ASSR = _BV(EXCLK); // Set EXCLK external clock bit in ASSR register
	// The EXCLK bit should only be set if you're trying to feed the
	// 32.768 clock signal from the Chronodot into XTAL1. 

	ASSR = ASSR | _BV(AS2); // Set the AS2 bit, using | (OR) to avoid
	// clobbering the EXCLK bit that might already be set. This tells 
	// TIMER2 to take its clock signal from XTAL1/2
	TCCR2A = 0; //override arduino settings, ensure WGM mode 0 (normal mode)
	
	// Set up TCCR2B register (Timer Counter Control Register 2 B) to use the 
	// desired prescaler on the external 32.768kHz clock signal. Depending on 
	// which bits you set high among CS22, CS21, and CS20, different 
	// prescalers will be used. See Table 18-9 on page 158 of the AVR 328P 
	// datasheet.
	//  TCCR2B = 0;  // No clock source (Timer/Counter2 stopped)
	// no prescaler -- TCNT2 will overflow once every 0.007813 seconds (128Hz)
	//  TCCR2B = _BV(CS20) ; 
	// prescaler clk/8 -- TCNT2 will overflow once every 0.0625 seconds (16Hz)
	//  TCCR2B = _BV(CS21) ; 
#if SAMPLES_PER_SECOND == 4
	// prescaler clk/32 -- TCNT2 will overflow once every 0.25 seconds
	TCCR2B = _BV(CS21) | _BV(CS20); 
#endif

#if SAMPLES_PER_SECOND == 2
	TCCR2B = _BV(CS22) ; // prescaler clk/64 -- TCNT2 will overflow once every 0.5 seconds
#endif

#if SAMPLES_PER_SECOND == 1
    TCCR2B = _BV(CS22) | _BV(CS20); // prescaler clk/128 -- TCNT2 will overflow once every 1 seconds
#endif

	// Pause briefly to let the RTC roll over a new second
	DateTime starttime = currTime;
	// Cycle in a while loop until the RTC's seconds value updates
	while (starttime.second() == currTime.second()) {
		delay(1);
		currTime = RTC.now(); // check time again
	}

	TCNT2 = 0; // start the timer at zero
	// wait for the registers to be updated
	while (ASSR & (_BV(TCN2UB) | _BV(TCR2AUB) | _BV(TCR2BUB))) {} 
	TIFR2 = _BV(OCF2B) | _BV(OCF2A) | _BV(TOV2); // clear the interrupt flags
	TIMSK2 = _BV(TOIE2); // enable the TIMER2 interrupt on overflow
	// TIMER2 will now create an interrupt every time it rolls over,
	// which should be every 0.25, 0.5 or 1 seconds (depending on value 
	// of SAMPLES_PER_SECOND) regardless of whether the AVR is awake or asleep.
	f_wdt = 0;
	return currTime;
}

//--------------------------------------------------------------
// writeToSD function. This formats the available data in the
// data arrays and writes them to the SD card file in a
// comma-separated value format.
void writeToSD (void) {
	// bitSet(PINC, 1); // Toggle LED for monitoring
//	bitSet(PIND, 7); // Toggle Arduino pin 7 for oscilloscope monitoring
	
	// Reopen logfile. If opening fails, notify the user
	if (!logfile.isOpen()) {
		if (!logfile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
			digitalWrite(ERRLED, HIGH); // turn on error LED
		}
	}
	// Step through each element of the sample data arrays
	// and write them to the SD card
	for (int i = 0; i < SAMPLES_PER_SECOND; i++) {
		// Write the unixtime
		logfile.print(unixtimeArray[i], DEC);
		logfile.print(F(","));
		// Convert the stored unixtime back into a DateTime
		// variable so we can write out the human-readable
		// version of the time stamp. There is a function
		// DateTime() that takes a uint32_t value and converts
		// to a DateTime object.
		DateTime tempTime = DateTime(unixtimeArray[i]);
		logfile.print(tempTime.year(), DEC);
		logfile.print(F("-"));
		logfile.print(tempTime.month(), DEC);
		logfile.print(F("-"));
		logfile.print(tempTime.day(), DEC);
		logfile.print(F(" "));
		logfile.print(tempTime.hour(), DEC);
		logfile.print(F(":"));
		logfile.print(tempTime.minute(), DEC);
		logfile.print(F(":"));
		logfile.print(tempTime.second(), DEC);
		logfile.print(F(","));
		logfile.print(fracSecArray[i], DEC);
		logfile.print(F(","));
		
		// Write out pressure in millibar
		// Begin by converting the floating point value of pressure to
		// a string, truncating at 2 digits of precision
		dtostrf(pressureArray[i], precision+3, precision, pressBuffer);
		// Then print the value to the logfile. 
		logfile.print(pressBuffer);
		logfile.print(F(","));
		
		// Write out temperature in Celsius
		// Begin by converting the floating point value of temperature to
		// a string, truncating at 2 digits of precision
		dtostrf(tempCArray[i], precision+3, precision, tempBuffer);
		logfile.println(tempBuffer);
	}
	  DateTime t1 = DateTime(unixtimeArray[0]);
	  // If the seconds value is 30, update the file modified timestamp
	  if (t1.second() % 30 == 0){
	    logfile.timestamp(T_WRITE, t1.year(),t1.month(),t1.day(),t1.hour(),t1.minute(),t1.second());
	  }
}

//------------------------------------------------------------------------------
// initFileName - a function to create a filename for the SD card based
// on the 4-digit year, month, day, hour, minutes and a 2-digit counter. 
// The character array 'filename' was defined as a 20-character global array 
// at the top of the sketch.
void initFileName(DateTime time1) {
	char buf[5];
	// integer to ascii function itoa(), supplied with numeric year value,
	// a buffer to hold output, and the base for the conversion (base 10 here)
	itoa(time1.year(), buf, 10);
	// copy the ascii year into the filename array
	for (byte i = 0; i <= 4; i++){
		filename[i] = buf[i];
	}
	// Insert the month value
	if (time1.month() < 10) {
		filename[4] = '0';
		filename[5] = time1.month() + '0';
	} else if (time1.month() >= 10) {
		filename[4] = (time1.month() / 10) + '0';
		filename[5] = (time1.month() % 10) + '0';
	}
	// Insert the day value
	if (time1.day() < 10) {
		filename[6] = '0';
		filename[7] = time1.day() + '0';
	} else if (time1.day() >= 10) {
		filename[6] = (time1.day() / 10) + '0';
		filename[7] = (time1.day() % 10) + '0';
	}
	// Insert an underscore between date and time
	filename[8] = '_';
	// Insert the hour
	if (time1.hour() < 10) {
		filename[9] = '0';
		filename[10] = time1.hour() + '0';
	} else if (time1.hour() >= 10) {
		filename[9] = (time1.hour() / 10) + '0';
		filename[10] = (time1.hour() % 10) + '0';
	}
	// Insert minutes
		if (time1.minute() < 10) {
		filename[11] = '0';
		filename[12] = time1.minute() + '0';
	} else if (time1.minute() >= 10) {
		filename[11] = (time1.minute() / 10) + '0';
		filename[12] = (time1.minute() % 10) + '0';
	}
	// Insert another underscore after time
	filename[13] = '_';
		
	// Next change the counter on the end of the filename
	// (digits 14+15) to increment count for files generated on
	// the same day. This shouldn't come into play
	// during a normal data run, but can be useful when 
	// troubleshooting.
	for (uint8_t i = 0; i < 100; i++) {
		filename[14] = i / 10 + '0';
		filename[15] = i % 10 + '0';
		filename[16] = '.';
		filename[17] = 'c';
		filename[18] = 's';
		filename[19] = 'v';
		
		
		
		if (!sd.exists(filename)) {
			// when sd.exists() returns false, this block
			// of code will be executed to open the file
			if (!logfile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
				// If there is an error opening the file, notify the
				// user. Otherwise, the file is open and ready for writing
				// Turn both indicator LEDs on to indicate a failure
				// to create the log file
				bitSet(PINB, 0); // Toggle error led on PINB0 (D8 Arduino)
				bitSet(PINB, 1); // Toggle indicator led on PINB1 (D9 Arduino)
				delay(5);
			}
			break; // Break out of the for loop when the
			// statement if(!logfile.exists())
			// is finally false (i.e. you found a new file name to use).
		} // end of if(!sd.exists())
	} // end of file-naming for loop
	
	// Serial.println(filename);
	
	// Write 1st header line to SD file based on mission info
	if (serialValid) {
		logfile.print(serialnumber);
		logfile.print(" ");
	}
	logfile.print(missionInfo);
	logfile.print(F(","));
	logfile.print(F("startMinute"));
	logfile.print(F(","));
	logfile.print(startMinute);
	logfile.print(F(","));
	logfile.print(F("minutes per hour"));
	logfile.print(F(","));
	logfile.println(dataDuration);
	// write a 2nd header line to the SD file
	logfile.println(F("POSIXt,DateTime,frac.seconds,Pressure.mbar,TempC"));
	// Update the file's creation date, modify date, and access date.
	logfile.timestamp(T_CREATE, time1.year(), time1.month(), time1.day(), 
			time1.hour(), time1.minute(), time1.second());
	logfile.timestamp(T_WRITE, time1.year(), time1.month(), time1.day(), 
			time1.hour(), time1.minute(), time1.second());
	logfile.timestamp(T_ACCESS, time1.year(), time1.month(), time1.day(), 
			time1.hour(), time1.minute(), time1.second());
	logfile.close();

} // end of initFileName function


//------------------------------------------------------------------------------
// endRun function.
// If user presses button to trigger interrupt 0 (AVR pin PD2, Arduino D2)
// then stop taking data and just flash the LED once in a while while sleeping
void endRun ()
{
	// cancel sleep as a precaution
	sleep_disable();
	// must do this as the pin will probably stay low for a while
	detachInterrupt(0);
	interrupts(); // reenable global interrupts (including TIMER0 for millis)

	// Turn off the RTC's 32.768kHz clock signal if it's not already off
	RTC.enable32kHz(false);
	TIMSK2 = 0; // stop timer 2 interrupts

	// Create a final time stamp for the file's modify date
	DateTime time1 = DateTime(unixtimeArray[0]);
	logfile.timestamp(T_WRITE, time1.year(), time1.month(), time1.day(), 
			time1.hour(), time1.minute(), time1.second());
	logfile.timestamp(T_ACCESS, time1.year(), time1.month(), time1.day(), 
			time1.hour(), time1.minute(), time1.second());

	logfile.close(); // close file again just to be sure
	digitalWrite(LED, LOW); // Turn off LED if it was on

	//--------------------------------------------------------------------------
	// At this point we'll enter into an endless loop
	// where the LED will flash briefly, then go back
	// to sleep over and over again. Only a full reset
	// will restart the logging process.
	while (1) {
		digitalWrite(LED, HIGH);
		delay(3);
		digitalWrite(LED, LOW);
		lowPowerSleep();  // go back to sleep
		f_wdt = 0; // reset flag each time through
	}
}  // end of endRun


//-----------------------------------------------------------------------------
// Interrupt Service Routine for INT1 (should be a reed switch)
// This function sets the global heartBeatFlag true, which will trigger actions
// in the main loop to notify the user that the datalogging is still happening.
void heartBeatInterrupt() {
	// If the interrupt is triggered, set the flag to 1 (true)
	heartBeatFlag = 1;
	// Immediately detach the interrupt on INT1 so that it doesn't 
	// trigger repeatedly. 
	detachInterrupt(1);
}

//-------------------------------------------------------------------
// heartBeat function. This function plays a tone on the buzzer attached
// to AVR pin PD7 and flashes the LED to notify the user that datalogging
// is still happening, or will happen again on schedule. 

void heartBeat(void){
				if (heartBeatCount < 10){
					digitalWrite(LED, HIGH); // also flash LED
					delay(5);
					digitalWrite(LED, LOW); // turn off LED
					beepbuzzer(); // Play tone on Arduino pin 7 (PD7)
					heartBeatCount++; // increment counter
				} else {
					// If the heartbeat has executed 10 times, shut if off,
					// reactivate the heartbeat interrupt, and reset the counter
					heartBeatFlag = 0;
					// Register the heartBeatInterrupt service routine on INT1, 
					// triggered whenever INT1 is pulled low.
					attachInterrupt(1, heartBeatInterrupt, LOW);
					heartBeatCount = 0;
				}
				heartBeatCount; // return value
}

//-------------------------------------------------------------------------
// Function beepbuzzer()
// This function uses TIMER1 to toggle the BUZZER pin to drive a piezo 
// buzzer. The frequency and duration of the noise are defined as global
// variables at the top of the program. This function exists in place of
// the normal Arduino tone() function because tone() uses TIMER2, which 
// interferes with the 32.768kHz timer used to clock the data logging. 
void beepbuzzer(void){
	uint8_t prescalarbits = 0b001;
	long toggle_count = 0;
	uint32_t ocr = 0;
	int8_t _pin = BUZZER;
	
	// Reset the 16 bit TIMER1 Timer/Counter Control Register A
    TCCR1A = 0;
	// Reset the 16 bit TIMER1 Timer/Counter Control Register B
    TCCR1B = 0;
	// Enable Clear Timer on Compare match mode by setting the bit WGM12 to 
	// 1 in TCCR1B
    bitWrite(TCCR1B, WGM12, 1);
	// Set the Clock Select bit 10 to 1, which sets no prescaling
    bitWrite(TCCR1B, CS10, 1);
	// Establish which pin will be used to run the buzzer so that we
	// can do direct port manipulation (which is fastest). 
    timer1_pin_port = portOutputRegister(digitalPinToPort(_pin));
    timer1_pin_mask = digitalPinToBitMask(_pin);
	
    // two choices for the 16 bit timers: ck/1 or ck/64
    ocr = F_CPU / frequency / 2 - 1;
	
	prescalarbits = 0b001; // 0b001 equals no prescalar 
      if (ocr > 0xffff)
      {
        ocr = F_CPU / frequency / 2 / 64 - 1;
        prescalarbits = 0b011; // 0b011 equal ck/64 prescalar
      }

	// For TCCR1B, zero out any of the upper 5 bits using AND that aren't already 
	// set to 1, and then do an OR with the prescalarbits to set any of the
	// lower three bits to 1 wherever prescalarbits holds a 1 (0b001).
    TCCR1B = (TCCR1B & 0b11111000) | prescalarbits;

	// Calculate the toggle count
    if (duration > 0)
    {
      toggle_count = 2 * frequency * duration / 1000;
    }
	
	// Set the OCR for the given timer,
    // set the toggle count,
    // then turn on the interrupts
    OCR1A = ocr; // Store the match value (ocr) that will trigger a TIMER1 interrupt
    timer1_toggle_count = toggle_count;
    bitWrite(TIMSK1, OCIE1A, 1); // Set OCEIE1A bit in TIMSK1 to 1.
	// At this point, TIMER1 should now be actively counting clock pulses, and
	// throwing an interrupt every time the count equals the value of ocr stored
	// in OCR1A above. The actual toggling of the pin to make noise happens in the 
	// TIMER1_COMPA_vect interrupt service routine. 
}

//-----------------------------------------------------
// Interrupt service routine for TIMER1 counter compare match
// This should be toggling the buzzer pin to make a beep sound
ISR(TIMER1_COMPA_vect)
{
  if (timer1_toggle_count != 0)
  {
    // toggle the pin
    *timer1_pin_port ^= timer1_pin_mask;

    if (timer1_toggle_count > 0)
      timer1_toggle_count--;
  }
  else
  {
	// Set Output Compare A Match Interrupt Enable (OCIE1A) bit to zero
	// in the TIMSK1 (Timer/Counter1 Interrupt Mask Register) to disable
	// the interrupt on compare match. 
    bitWrite(TIMSK1, OCIE1A, 0); 
    *timer1_pin_port &= ~(timer1_pin_mask);  // keep pin low after stop
  }
}

//----------------------------------------------------------------------------
// Function getSettings()
// This function retrieves mission parameters from a settings.txt file on 
// the micro SD card, if the file is present. The file may have lines 
// starting with slashes (/) for comments, so this routine skips those 
// lines and looks for the first line with a format like: 
// 0,60,my mission info
// The first entry is assumed to be the startMinute value
// The 2nd entry is assumed to be the dataDuration value
// The 3rd entry is assumed to be the missionInfo
void getSettings()
{
	// Begin by retrieving the 17-digit serial number that may 
	// or may not be burned into the eeprom memory of the ATmega chip
	EEPROM.get(0, serialnumber);
	if (serialnumber[0] == 'S') {
		serialValid = true;
	}

  char character;
  char temporary[3];
  boolean valid;
  // Check if the setting.txt file exists in the root directory of the SD card
  if (sd.exists(setfilename)){
	// Open the settings file for reading:
	setfile.open(setfilename, O_READ);
	while (setfile.available()){
		character = setfile.read(); // read first character in file
		while(character == '/') {
			if (character == '/') { // if first character is a comment character
				while(character != '\n'){
					// skip all characters until you reach the newline \n character
					character=setfile.read();
				}
			}
			character = setfile.read();
		}
		// We should now be at the first value in the file
		// Step through the next few characters and add them to the
		// temporary array
		for (int i = 0; i < 3; i++){
			if (character != ',') {
				temporary[i] = character;
				character=setfile.read(); // read next value
			} else {
				temporary[i] = '\0'; // terminate array
				break;
			}
		}
		startMinute = atoi(temporary); // convert array to integer
		// Test to see if startMinute was interpreted as a usable value
		if (startMinute >= 0 && startMinute <= 59) {
			valid = true;
		}
		// We should now be at the 2nd value in the file, after a comma
		char temporary[3]; // reset temporary array
		character = setfile.read(); // read the next character after the comma
		for (int i = 0; i < 3; i++){
			if (character != ',') {
				temporary[i] = character;
				character = setfile.read(); // read next value
			} else {
				temporary[i] = '\0'; // terminate array
				break;
			}
		}
		dataDuration = atoi(temporary); // convert array to integer
		// Check to see if dataDuration was interpreted as a usable value
		if (valid) {
			if (dataDuration >= 1 && dataDuration <= 60) {
				valid = true;
			} else {
				valid = false;
			}
		}
		
		// If startMinute or dataDuration were not valid, break out of 
		// the loop, otherwise continue reading in the missionInfo
		if (!valid) {
			break;
		}
		// We should now be at the 3rd entry, the mission info, which can be
		// up to the length that arraylen is defined as up above. It will 
		// stop reading at any newline character or commas though. 
		int i = 0;
		character = setfile.read(); // read the next character after the comma
		while (character >= 0 && character != '\n' && character != '\r' && character != ','  && i < arraylen) {
			// The tests in this while() statement will stop reading the line as
			// soon as it finds an End of File marker (value < 0), a newline
			// character ('\n'), a carriage return character ('\r'), 
			// another comma, or once it's read more characters than will fit
			// in the arraylen limit. 			
			missionInfo[i] = character; // write next value to missionInfo
			i++; // increment i
			character = setfile.read(); // read next value
		}
		missionInfo[i] = '\0'; // terminate character string
		break; // stop the while(setfile.available()) loop
		}
    
	} else { 
		// The settings.txt file wasn't found in the root
		// directory of the sd card, so just set the parameters
		// to their defaults.
		startMinute = STARTMINUTE;
		dataDuration = DATADURATION;
#ifdef ECHO_TO_SERIAL
		Serial.println(F("Could not find settings.txt"));
		Serial.print(F("Using defaults: "));
		Serial.print(startMinute);
		Serial.print(F("\t"));
		Serial.print(dataDuration);
		Serial.print(F("\t"));
		Serial.println(missionInfo);
		if (serialValid){
			Serial.print(F("Serial #: "));
			Serial.println(serialnumber);
		}
#endif
		setfile.close();
		return; // quit out of the function immediately
		// The leds will not be flashed in this case, so if the user
		// was expecting files to be read off the settings.txt file
		// the lack of success or failure led flashes will signify
		// that the file wasn't found. 
	}
	
	setfile.close();
	
	if (valid) {
		// Flash green LED 10 times to notify user that 
		// settings were read from the SD card. 
		for (int flsh = 0; flsh < 10; flsh++){
				digitalWrite(LED, HIGH);
				delay(100);
				digitalWrite(LED, LOW);
				delay(100);
		}
#ifdef ECHO_TO_SERIAL
		Serial.println(F("Read settings.txt: "));
		Serial.print(startMinute);
		Serial.print(F("\t"));
		Serial.print(dataDuration);
		Serial.print(F("\t"));
		Serial.println(missionInfo);
		if (serialValid){
			Serial.print(F("Serial #: "));
			Serial.println(serialnumber);
		}
#endif
	} else if (!valid){
			// If one of the values wasn't valid, reset both
			// to the default values.
			startMinute = STARTMINUTE;
			dataDuration = DATADURATION;
#ifdef ECHO_TO_SERIAL
		Serial.println(F("Invalid settings.txt entries."));
		Serial.print(F("Using defaults: "));
		Serial.print(startMinute);
		Serial.print(F("\t"));
		Serial.print(dataDuration);
		Serial.print(F("\t"));
		Serial.println(missionInfo);
		if (serialValid){
			Serial.print(F("Serial #: "));
			Serial.println(serialnumber);
		}
#endif			
		// Flash the red error led 5 times to notify the user that
		// the settings.txt file was read, but the values
		// were not valid and will not be used. 
		for (int flsh = 0; flsh < 5; flsh++){
		digitalWrite(ERRLED, HIGH);
		delay(100);
		digitalWrite(ERRLED, LOW);
		delay(100);
		}
	}
}

//------------------------------------------------
// printTime function takes a DateTime object from
// the real time clock and prints the date and time 
// to the serial monitor. 
void printTime(DateTime now){
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
	// You may want to print a newline character
	// after calling this function i.e. Serial.println();
}


//---------------------------------------------------------------------------
// freeRam function. Used to report back on the current available RAM while
// the sketch is running. A 328P AVR has only 2048 bytes of RAM available
// so this should return a value between 0 and 2048. If RAM drops to
// zero you will start seeing weird behavior as bits of memory are accidentally
// overwritten, destroying normal functioning in the process.
// int freeRam () {
	// extern int __heap_start, *__brkval;
	// int v;
	// return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
// }





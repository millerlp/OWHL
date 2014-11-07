/* OWHL.ino
 * 

	OWHL - Open Wave Height Logger
	
  -------------------------------------------------------------------
  The sketch:
  During setup, we enable the DS3231 RTC's 32.768kHz output hooked to 
  XTAL1 to provide a timed interrupt on TIMER2. 
  The DS3231 32kHz pin should be connected to XTAL1, with a 10kOhm 
  pull-up resistor between XTAL1 and +Vcc (3.3V). 

  We set the prescaler on TIMER2 so that it only interrupts
  every 0.25 second (or change the value of SAMPLES_PER_SECOND in 
  the preamble to sample 4, 2, or 1 times per second). 
  In the main loop, whenever TIMER2 rolls over and causes an
  interrupt (see ISR at the bottom), the statements in the
  main loop's if() statement should execute, including reading the
  time, reading the MS5803 sensor, printing those values to the
  serial port, and then going to sleep via the goToSleep function.
  
  Change the STARTMINUTE and DATADURATION values in the preamble
  below to set when the unit should wake up and start taking 
  data, and for how long during each hour it should take data.
  For example, STARTMINUTE = 0 and DATADURATION = 30 would 
  take data every hour starting at minute 00, and record 30 
  minutes worth of data. For continuous recording, set 
  STARTMINUTE = 0 and DATADURATION = 60. 
  
  If you are using an external 32.768kHz crystal instead of
  the built-in signal from the DS3231 clock, set the
  variable useClockCrystal = false. Otherwise, leave the
  value set = true.
  
  When not recording, the sketch goes into a lower power 
  sleep (lowPowerSleep) mode using the watchdog timer set 
  to 8 second timeouts to conserve power further.
  
  If a buzzer is present on AVR pin PD7 and a (reed) switch
  is present on pin PD3, the sketch will beep the buzzer 10 
  times and flash the LED to indicate that the logger is alive
  while running. 

 */
#include <SPI.h>
#include <Wire.h>
#include <RTClib.h>
#include <RTC_DS3231.h>
#include <MS5803_14.h>
#include <SdFat.h>


#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/atomic.h>
#include <wiring_private.h>
#include <avr/wdt.h>



#define REEDSW 3 // Arduino pin D3, AVR pin PD3, aka INT1
#define ERRLED 5 // Arduino pin D5, AVR pin PD5
#define LED 6 	  // Arduino pin D6, AVR pin PD6
#define BUZZER 7 // Arduino pin D7, AVR pin PD7


#define ECHO_TO_SERIAL 0 // echo data to serial port, set to 0 to turn off

#define STARTMINUTE 0 // minute of hour to start taking data, 0 to 59
#define DATADURATION 30 // # of minutes to collect data, 1 to 60

#define SAMPLES_PER_SECOND 4// number of samples taken per second (4, 2, or 1)

// Define a variable to use either the DS3231 32.768kHz signal (true) or 
// an external 32.768kHz crystal (false). 
boolean useClockCrystal = true;  


const byte chipSelect = 10; // define the Chip Select pin for SD card
char filename[] = "LOGGER00.CSV";

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
// wake up and take data again. This will be set during the setup loop
uint8_t wakeMinute = 59;

DateTime oldtime; // used to track time in main loop
DateTime newtime; // used to track time
uint8_t oldday; // used to track when a new day starts

RTC_DS3231 RTC;
MS_5803 sensor = MS_5803(512); // the argument to MS_5803 sets the precision (256,512,1024,4096)
SdFat sd;
SdFile logfile;  // for sd card, this is the file object to be written to


 
//---------------------------------------------------------
// SETUP loop
void setup() {

	pinMode(chipSelect, OUTPUT);  // set chip select pin for SD card to output
	// Set indicator LED as output
	pinMode(LED, OUTPUT);
	pinMode(ERRLED, OUTPUT);
	// Set interrupt 0 (Arduino pin D2) as input, use internal pullup resistor
	// interrupt 0 is used to put the unit into permanent sleep, stopping sampling
	pinMode(2, INPUT_PULLUP);
	
	// Set interrupt 1 (Arduino pin D3) as input, use internal pullup resistor
	// interrupt 1 is used to activate the heartbeat function that notifies the 
	// user if the unit is taking data.
	pinMode(3, INPUT_PULLUP);
	
	// Set Buzzer pin as output
	pinMode(BUZZER, OUTPUT);
	duration = 200; // lengthen duration of buzzer beep
	frequency = 4000; // change pitch of buzzer (Hz)
	beepbuzzer();
	duration = 25; // reset buzzer beep duration
	frequency = 4000; // reset buzzer frequency
	

#if ECHO_TO_SERIAL
	Serial.begin(57600);
	Serial.println();
	delay(500);
	Serial.println(F("Starting up..."));
#endif
	// Calculate what minute of the hour the program should turn the 
	// accurate timer back on to start taking data again
	if (STARTMINUTE == 0) {
		wakeMinute = 59;
	} else {
		wakeMinute = STARTMINUTE - 1;
	}
	// if endMinute value is above 60, just set it to 59
	if (endMinute >= 60) endMinute = 59;
	


	//--------RTC SETUP ------------
	Wire.begin();
	RTC.begin(); // Start Chronodot
        // Check to see if DS3231 RTC is running
        if (! RTC.isrunning()) {
          //Serial.println("RTC is NOT running!");
          // The following line sets the RTC to the date & time this sketch was compiled
          RTC.adjust(DateTime(__DATE__, __TIME__));
          // -----------------------
          // Notify the user via the LEDs. Error LED should go on,
          // while 2nd LED flashes 5 times
          digitalWrite(ERRLED, HIGH);
          for (int flsh = 0; flsh < 5; flsh++){
            digitalWrite(LED, HIGH);
            delay(100);
            digitalWrite(LED, LOW);
            delay(100);
          }
          digitalWrite(ERRLED, LOW); // turn error led back off
          //------------------------
        }
        // Assuming the DS3231 RTC is running, 
		// now check to see if DS3231 RTC time is behind
        DateTime now = RTC.now();
        DateTime compiled = DateTime(__DATE__, __TIME__);
        if (now.unixtime() < compiled.unixtime()) {
          //Serial.println("RTC is older than compile time!  Updating");
          RTC.adjust(DateTime(__DATE__, __TIME__));
                    // -----------------------
          // Notify the user via the LEDs. Error LED should go on,
          // while 2nd LED flashes 10 times
          digitalWrite(ERRLED, HIGH);
          for (int flsh = 0; flsh < 10; flsh++){
            digitalWrite(LED, HIGH);
            delay(100);
            digitalWrite(LED, LOW);
            delay(100);
          }
          digitalWrite(ERRLED, LOW); // turn error led back off
          //------------------------
        }

	RTC.enable32kHz(false); // Stop 32.768kHz output from Chronodot
	
	// The Chronodot can also put out several different square waves
	// on its SQW pin (1024, 4096, 8192 Hz), though I don't use them
	// in this sketch. The code below disables the SQW output to make
	// sure it's not using any extra power
	RTC.SQWEnable(false); // Stop the SQW output pin
	DateTime starttime = RTC.now(); // get initial time
	//-------End RTC SETUP-----------

	// Initialize the SD card object
	// Try SPI_FULL_SPEED, or SPI_HALF_SPEED if full speed produces
	// errors on a breadboard setup. 
	if (!sd.begin(chipSelect, SPI_FULL_SPEED)) {
	// If the above statement returns FALSE after trying to 
	// initialize the card, enter into this section and
	// hold in an infinite loop.
#if ECHO_TO_SERIAL
		Serial.println(F("SD initialization failed"));
#endif
		
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
#if ECHO_TO_SERIAL
	Serial.println(F("SD initialization done."));
#endif

#if ECHO_TO_SERIAL
	Serial.print(F("Start minute: "));
	Serial.print(STARTMINUTE);
	Serial.print(F(", endMinute: "));
	Serial.print(endMinute);
	Serial.print(F(", wakeMinute: "));
	Serial.println(wakeMinute);
	delay(40);
#endif
	
	// Start MS5803 pressure sensor
	sensor.initializeMS_5803();
	
	//-------------------------------------------------------
	// Create interrupt for INT1 (should be reed switch)
	// The heartBeat interrupt service routine is defined below
	// the main loop
	// attachInterrupt(1, heartBeatInterrupt, LOW);
	heartBeatFlag = 1; // Immediately beep on start up.
	
	//--------------------------------------------------------
	// Check current time, branch the rest of the setup loop
	// depending on whether it's time to take data or 
	// go to sleep for a while
	newtime = RTC.now();

	if (newtime.minute() >= STARTMINUTE && newtime.minute() <= endMinute){
		// Create new file on SD card using initFileName() function
		// This creates a filename based on the year + month + day
		// with a number indicating which number file this is on
		// the given day. The format of the filename is
		// YYMMDDXX.CSV where XX is the counter (00-99). The
		// function also writes the column headers to the new file.
		initFileName(starttime);

		// Start 32.768kHz clock signal on TIMER2. Set argument to true
		// if using a 32.768 signal from the DS3231 Chronodot on XTAL1, 
		// or false if using a crystal on XTAL1/XTAL2. The 2nd argument
		// should be the current time
		newtime = startTIMER2(useClockCrystal, newtime);
		// Initialize oldtime and oldday values
		oldtime = newtime;
		oldday = oldtime.day();
		// set f_wdt flag to 1 so we start taking data in the main loop
		f_wdt = 1;
	} else if (newtime.minute() < STARTMINUTE | newtime.minute() > endMinute){
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
		// If the current minute is >= than STARTMINUTE and <= endMinute, 
		// then take data
		if (newtime.minute() >= STARTMINUTE && newtime.minute() <= endMinute) {
	
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
				//      bitSet(PINB,1); // used to visualize timing with LED or oscilloscope

				// Call the writeToSD function to output the data array contents
				// to the SD card
				writeToSD();
				
#if ECHO_TO_SERIAL
				if (newtime.second() % 10 == 0){
					printTimeSerial(newtime, fracSec);
					// Show pressure
					Serial.print(F("Pressure = "));
					Serial.print(sensor.pressure());
					Serial.print(F(" mbar, "));
					Serial.print(sensor.temperature());
					Serial.println(F(" C"));
					Serial.print(F("Free RAM: ")); Serial.println(freeRam());
					delay(40);
				}
#endif
				//      bitSet(PINB,1); // used to visualize timing with LED or oscilloscope
			} // end of if (loopCount >= (SAMPLES_PER_SECOND - 1))

			// increment loopCount after writing all the sample data to
			// the arrays
			loopCount = loopCount++;

			// Increment the fractional seconds count
#if SAMPLES_PER_SECOND == 4
			fracSec = fracSec + 25;
#endif

#if SAMPLES_PER_SECOND == 2
			fracSec = fracSec + 50;
#endif
			
			// bitSet(PINC, 1); // used to visualize timing with LED or oscilloscope
			goToSleep(); // call the goToSleep function (below)
		
		//***************************************************************
		//***************************************************************	
		} else if (newtime.minute() < STARTMINUTE){
			// If it is less than STARTMINUTE time, check to see if 
			// it is wakeMinute time. If so, use goToSleep, else if not use
			// lowPowerSleep
			//================================================================
			//================================================================
			if (newtime.minute() == wakeMinute){
				
#if ECHO_TO_SERIAL
				if (newtime.second() % 10 == 0){
					printTimeSerial(newtime, fracSec);
					Serial.println(F("Wake minute, goToSleep"));
					delay(40);
				}
#endif 
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
				
#if ECHO_TO_SERIAL
				printTimeSerial(newtime, fracSec);
				Serial.println(F("Going to deep sleep"));
				delay(40);
#endif 
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
			
#if ECHO_TO_SERIAL
			printTimeSerial(newtime, fracSec);
			Serial.println(F("Going to deep sleep, outer f_wdt = 1"));
			delay(40);
#endif
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
			// STARTMINUTE. Reenter goToSleep mode. The third test, with
			// endMinute != wakeMinute, takes care of the additional special case
			// where the user wants to record continuously from 0 to 59 minutes,
			// so the endMinute would be equal to wakeMinute (that case should be
			// dealt with by the first statement in the block above where data 
			// collection happens). 
#if ECHO_TO_SERIAL
				if (newtime.second() % 10 == 0){
					printTimeSerial(newtime, fracSec);
					Serial.println(F("Wake minute, goToSleep"));
					delay(40);
				}
#endif
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
			
#if ECHO_TO_SERIAL
			printTimeSerial(newtime, fracSec);
			Serial.println(F("Still in deep sleep"));
			delay(40);
#endif
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
			
#if ECHO_TO_SERIAL
			printTimeSerial(newtime, fracSec);
			Serial.println(F("Restarting TIMER2, return to goToSleep mode"));
			delay(40);
#endif
			if (heartBeatFlag) {
				heartBeat(); // call the heartBeat function
				delay(duration); //delay long enough to play sound
			}
			
			// If it is the wakeMinute, restart TIMER2
			newtime = startTIMER2(useClockCrystal,newtime);
			// Start a new file 
			//initFileName(newtime); // LPM commented out so new files only start on new days
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


//--------------------------------------------------------------
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
	bitSet(PINC,1);
}



//-----------------------------------------------------------------------------
// goToSleep function. When called, this puts the AVR to
// sleep until it is awakened by an interrupt (TIMER2 in this case)
// This is a higher power sleep mode than the lowPowerSleep function uses.
void goToSleep()
{
	byte adcsra, mcucr1, mcucr2;
	// bitSet(PINB,1); // flip pin PB1 (digital pin 9) to indicate start of sleep cycle

	// Cannot re-enter sleep mode within one TOSC cycle. 
	// This provides the needed delay.
	OCR2A = 0; // write to OCR2A, we're not using it, but no matter
	while (ASSR & _BV(OCR2AUB)) {} // wait for OCR2A to be updated

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

	//    bitSet(PINB,1); // flip Pin PB1 (digital pin 9) to indicate wakeup
	//    ADCSRA = adcsra; //restore ADCSRA
}

//-----------------------------------------------------------------------------
// lowPowerSleep function
// This sleep version uses the watchdog timer to sleep for 8 seconds at a time
// Because the watchdog timer isn't super accurate, the main program will leave
// this sleep mode once you get within 1 minute of the start time for a new 
// sampling period, and start using the regular goToSleep() function that 
// uses the more accurate TIMER2 clock source. 
void lowPowerSleep(void){
	
#if ECHO_TO_SERIAL
	Serial.print("lowPowerSleep ");
	Serial.println(millis());
	delay(40);
#endif
	
//	bitSet(PIND,6); // flip Pin PD6 (digital pin 6) to indicate sleep
	
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
//	bitSet(PIND,6); // flip Pin PD6 (digital pin 6) to indicate wakeup
}

//--------------------------------------------------------------------------
// Interrupt service routine for when the watchdog timer counter rolls over
// This is used after the user has entered the lowPowerSleep function.
ISR(WDT_vect) {
	if (f_wdt == 0) { // if flag is 0 when interrupt is called
		f_wdt = 2; // set the flag to 2
	} else {
#if ECHO_TO_SERIAL
		Serial.print(F("WDT fired, but f_wdt = "));
		Serial.println(f_wdt);
		delay(40);
#endif
	}
}

//--------------------------------------------------------------
// startTIMER2 function
// Starts the 32.768kHz clock signal being fed into XTAL1/2 to drive the
// quarter-second interrupts used during data-collecting periods. 
// Set the 1st argument true if using the 32kHz signal from a DS3231 Chronodot
// real time clock, otherwise set the argument false if using a 32.768kHz 
// clock crystal on XTAL1/XTAL2. Also supply a current DateTime time value. 
// This function returns a DateTime value that can be used to show the 
// current time when returning from this function. 
DateTime startTIMER2(bool start32k, DateTime currTime){
	TIMSK2 = 0; // stop timer 2 interrupts
	if (start32k){
		RTC.enable32kHz(true);
		ASSR = _BV(EXCLK); // Set EXCLK external clock bit in ASSR register
		// The EXCLK bit should only be set if you're trying to feed the
		// 32.768 clock signal from the Chronodot into XTAL1. 
	}
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
	// bitSet(PIND, 6); // Toggle LED for monitoring
//	bitSet(PIND, 7); // Toggle Arduino pin 7 for oscilloscope monitoring
	
	// Reopen logfile. If opening fails, notify the user
	if (!logfile.isOpen()) {
		if (!logfile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
			digitalWrite(ERRLED, HIGH); // turn on error LED
#if ECHO_TO_SERIAL
			sd.errorHalt("opening file for write failed");
#endif
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
		// logfile.sync(); // flush all data to SD card (uses lots of power)
	}
	  DateTime t1 = DateTime(unixtimeArray[0]);
	  // If the seconds value is 30, update the file modified timestamp
	  if (t1.second() % 30 == 0){
	    logfile.timestamp(T_WRITE, t1.year(),t1.month(),t1.day(),t1.hour(),t1.minute(),t1.second());
	  }
	// logfile.close(); // close file again. This forces a full sync (which uses lots of power)
//	bitSet(PIND, 7); // Toggle Arduino pin 7 for oscilloscope monitoring
	// bitSet(PIND, 6); // Toggle LED for monitoring
}

//------------------------------------------------------------------------------
// initFileName - a function to create a filename for the SD card based
// on the 2-digit year, month, and day. The character array 'filename'
// was defined as a global variable at the top of the sketch
void initFileName(DateTime time1) {
	// Insert 2-digit year, month, and date into filename[] array
	// decade (2014-2000) = 14/10 = 1
	filename[0] = ((time1.year() - 2000) / 10) + '0'; 
	// year  (2014-2000) = 14%10 = 4
	filename[1] = ((time1.year() - 2000) % 10) + '0'; 
	if (time1.month() < 10) {
		filename[2] = '0';
		filename[3] = time1.month() + '0';
	} else if (time1.month() >= 10) {
		filename[2] = (time1.month() / 10) + '0';
		filename[3] = (time1.month() % 10) + '0';
	}
	if (time1.day() < 10) {
		filename[4] = '0';
		filename[5] = time1.day() + '0';
	} else if (time1.day() >= 10) {
		filename[4] = (time1.day() / 10) + '0';
		filename[5] = (time1.day() % 10) + '0';
	}
	// Next change the counter on the end of the filename
	// (digits 6+7) to increment count for files generated on
	// the same day. This shouldn't come into play
	// during a normal data run, but can be useful when 
	// troubleshooting.
	for (uint8_t i = 0; i < 100; i++) {
		filename[6] = i / 10 + '0';
		filename[7] = i % 10 + '0';
		if (!sd.exists(filename)) {
			// when sd.exists() returns false, this block
			// of code will be executed to open the file
			if (!logfile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
				// If there is an error opening the file, notify the
				// user. Otherwise, the file is open and ready for writing
#if ECHO_TO_SERIAL
				sd.errorHalt("opening file for write failed");
#endif
				// Turn both indicator LEDs on to indicate a failure
				// to create the log file
				bitSet(PINB, 0); // Toggle error led on PINB0 (D8 Arduino)
				bitSet(PINB, 1); // Toggle indicator led on PINB1 (D9 Arduino)
			}
			break; // Break out of the for loop when the
			// statement if(!logfile.exists())
			// is finally false (i.e. you found a new file name to use).
		} // end of if(!sd.exists())
	} // end of file-naming for loop
#if ECHO_TO_SERIAL
	Serial.print(F("Logging to: "));
	Serial.println(filename);
#endif
	// write a header line to the SD file
	logfile.println(F("POSIXt,DateTime,frac.seconds,Pressure.mbar,TempC"));
	// Sync and close the logfile for now.
	logfile.sync();
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
#if ECHO_TO_SERIAL
	printTimeSerial(newtime, fracSec);
	Serial.println(F("Ending program"));
#endif
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
					heartBeatCount = heartBeatCount++; // increment counter
				} else {
					// If the heartbeat has executed 10 times, shut if off,
					// reactivate the heartbeat interrupt, and reset the counter
					heartBeatFlag = 0;
					attachInterrupt(1, heartBeatInterrupt, LOW);
					heartBeatCount = 0;
				}
				heartBeatCount; // return value
}


//------------------------------------------------------------------------------
// printTimeSerial function
// Just a function to print a formatted date and time to the serial monitor
void printTimeSerial(DateTime newtime, byte fracSec){
	Serial.print(newtime.year(), DEC);
	Serial.print(F("/"));
	Serial.print(newtime.month(), DEC);
	Serial.print(F("/"));
	Serial.print(newtime.day(), DEC);
	Serial.print(F(" "));
	Serial.print(newtime.hour(), DEC);
	Serial.print(F(":"));
	if (newtime.minute() < 10) {
		Serial.print(F("0"));
	}
	Serial.print(newtime.minute(), DEC);
	Serial.print(F(":"));
	if (newtime.second() < 10) {
		Serial.print(F("0"));
	}
	Serial.print(newtime.second(), DEC);
	Serial.print(F("."));
	Serial.println(fracSec);
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





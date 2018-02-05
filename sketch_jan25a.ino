
/*
 * The "It has ben XX days since .. Application.
 * 
 * Two displays. Three buttons. To control the value displayed.
 * 
 * Where we drive the cathodes with one 74595 and the anodes (through a transistor driver) with another 74595.
 * Using the extra pins on the 2nd 74595 to drive up to 6 switch inputs.
 * 
 * Trying to get this onto ATtiny85
 * 
 * PB2 (7) - RCLK (12)
 * PB1 (6) - SER (14)
 * PB0 (5) - SRCLK (11)
 * PB3 (2) - switch input
 * 
 * Where the 74595(1) is connected to the 7 segment display as
 * Q0 - Ca
 * Q1 - Cb
 * Q2 - Cc
 * Q3 - Cd
 * Q4 - Ce
 * Q5 - Cf
 * Q6 - Cg
 * Q7 - Cdp
 * 
 * And the 74595(2) is connected to the transistor drivers.
 * Q0 - 1's digit
 * Q1 - 10's digit
 * Q2 - S0 - reset
 * Q3 - S1 - increment
 */

// these are bit positions for shifting, instead of using digital write
// so we can do PORTB |= (1<<pin) to set
// and PORTB &= ~(1<<pin) to clear
// Since this saves us 4 microseconds per operation.

#define sbi(port,bit) \
  (port) |= (1 << (bit)) 

#define cbi(port,bit) \
  (port) &= ~(1 << (bit)) 
  
#include <avr/interrupt.h>

const int clockPin = 0; // PB0 connected to SRCLK on 74595
const int dataPin = 1; // PB1 connected to SER on 74595
const int latchPin = 2; // conected to RCLK on 74595
const int inputPin = 3; // connected to pushbutton switches after 2nd 74595
// 4
// 5 - requires a high voltage programmer to reprogram device is we remove reset function?

// display 0,1,2,3,4,5,6,7,8,9, MSB shifted first (LSB is a segment)
int datArray[10] = { 0xC0, 0xF9, 0xA4, 0xB0, 0x99, 0x92, 0x82, 0xF8, 0x80, 0x90 };
// LSB shifted first
//int datArray[10]= { 0x03, 0x9F, 0x25, 0x0D, 0x99, 0x49, 0x41, 0x1F, 0x01, 0x09 };

const int maxDigits = 2;

// the digit to display
int digitMap[maxDigits] = { 0x02, 0x01 }; // pnp transistors, so these are active low now.

// we basically use unpacked BCD
// [0] - one's digit
// [1] - ten's digit
int displayValue[maxDigits];


// causes one digit to be written to the 7 segment display
// value: the index to the digitArray to display.
// digit: a value from 0-7
void writeDigit(int value, int digit) {
  
    PORTB &= ~(1<<latchPin);// ground latch pin and hold low as long as we are transmitting data.

    // the switches are "after" the display driver, so the need to be shifted first.
    // practically we combine the switch bitmap with the active display digit map, writing a 1 to the enabled switches
    // which is currently the lower 6 bits. 0x2F
    int  control = digitMap[digit]|0xFC;
    // because we have the anode and transistor driver wired after the cathode driver, we have to shift it out first.
    shiftOut(dataPin, clockPin, MSBFIRST, control);

    
    // we used to need to drive out LSB first, because we have Q0 connected to Ca, which is last
    // but reversed our bitmap abover to support MSB first. Either that or we could rewire the display.
    shiftOut(dataPin, clockPin, MSBFIRST, datArray[value]);  
    
    //digitalWrite(latchPin, HIGH);
    PORTB |= (1<<latchPin);
}

void resetDigits() {
  for (int i = 0; i < maxDigits; i++) {
    displayValue[i] = 0;
  }
}

volatile boolean checking_input = false;

void writeDigits() {
  if (checking_input) {
    // do not try to update display if we are trying to read an input.
    return;
  }
  for (int n = 0; n < maxDigits; n++) {
  
    writeDigit(displayValue[n], n);
   // delay(4);
  }
}

void increment() {
  for (int i = 0; i < maxDigits; i++) {
    displayValue[i] ++;
    if (displayValue[i] > 9) {
      displayValue[i] = 0;
      if (i + 1 < maxDigits) {
        continue;
      }
    }
    else {
      break;
    }
  } // for
}

void decrement() {
  int borrow = false;
  // do the ten's compliment of each digit
  for (int i = 0; i < maxDigits; i++) {
    int compliment=0;
    if (borrow) {
      10-displayValue[i];
    }
    else {
      compliment=10-1;
    }
    int result = (displayValue[i] + compliment);
    if (result >=10) {
      borrow = true;
    }
    displayValue[i] = result % 10;
  } // for
}

void setup() {     
  
  // put your setup code here, to run once:
  pinMode(latchPin, OUTPUT);
  pinMode(clockPin, OUTPUT);
  pinMode(dataPin, OUTPUT);
  pinMode(inputPin, INPUT);
  
  resetDigits();

  cli();
  GIMSK |= (1 << PCIE); // enable pin change interrupts
  //sbi(PCMSK, PCINT3);
  PCMSK |= (1 << PCINT3);
  sei();

}

// inspired by Arduino inputs from 74595 http://www.kevindarrah.com/download/arduino_code/ShiftRegisterInput2_piano.ino
//void pin_read(){
ISR(PCINT0_vect) {

  if (GIFR & (1 << PCIF) == 0) {
    return; // not a pin change interrupt
  }
    // switch debouncing logic
    for(int j=0; j<50; j++) {
      delayMicroseconds(1000);
    }
    
    if (digitalRead(inputPin) == LOW) {
      return;
    }

  checking_input = true;
   
    // determine which button(s) are presesed
    // shift out the bitmap that represents the 1st button
    PORTB &= ~(1<<latchPin);
    shiftOut(dataPin, clockPin, MSBFIRST, 0x07); // 0x04 for the switch, + 0x03 to cause displays to blank.
    shiftOut(dataPin, clockPin, MSBFIRST, 0x00);
    PORTB |= (1<<latchPin);
    if (digitalRead(inputPin) == HIGH) {
      resetDigits();
      goto done;
    }

    // shift out the bitmap that represents the 2nd button
    PORTB &= ~(1<<latchPin);
    shiftOut(dataPin, clockPin, MSBFIRST, 0x0B); // 0x08 for the switch, + 0x03 to cause displays to blank.
    shiftOut(dataPin, clockPin, MSBFIRST, 0x00);
    PORTB |= (1<<latchPin);
    if (digitalRead(inputPin) == HIGH) {
      decrement();
      goto done;
    }

    // shift out the bitmap that represents the 3rd button
    PORTB &= ~(1<<latchPin);
    shiftOut(dataPin, clockPin, MSBFIRST, 0x13); // 0x10 for the switch, + 0x03 to cause displays to blank.
    shiftOut(dataPin, clockPin, MSBFIRST, 0x00);
    PORTB |= (1<<latchPin);
    if (digitalRead(inputPin) == HIGH) {
      increment();
      goto done;
    }


done:
    // keep us here until button is released.
    while(digitalRead(inputPin) == HIGH){ 
      delayMicroseconds(1000); 
    }

  checking_input = false;
}//pin_read


void loop() {
  writeDigits();
}

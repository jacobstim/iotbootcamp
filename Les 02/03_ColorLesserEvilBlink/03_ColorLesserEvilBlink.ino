/*
  Blink

  Turns an LED on for one second, then off for one second, repeatedly.

  Most Arduinos have an on-board LED you can control. On the UNO, MEGA and ZERO
  it is attached to digital pin 13, on MKR1000 on pin 6. LED_BUILTIN is set to
  the correct LED pin independent of which board is used.
  If you want to know what pin the on-board LED is connected to on your Arduino
  model, check the Technical Specs of your board at:
  https://www.arduino.cc/en/Main/Products

  modified 8 May 2014
  by Scott Fitzgerald
  modified 2 Sep 2016
  by Arturo Guadalupi
  modified 8 Sep 2016
  by Colby Newman

  This example code is in the public domain.

  http://www.arduino.cc/en/Tutorial/Blink
*/

#include <Adafruit_NeoPixel.h>

#define LED_COUNT 1

// Declare our NeoPixel object:
Adafruit_NeoPixel rgbled(LED_COUNT, PIN_NEOPIXEL, NEO_GRBW + NEO_KHZ800);

// the setup function runs once when you press reset or power the board
void setup() {
  // initialize digital pin LED_BUILTIN as an output.
  pinMode(LED_BUILTIN, OUTPUT);

  // Initialize RGBLED
  rgbled.begin();
  rgbled.setBrightness(64);
  rgbled.show(); // Initialize pixel to 'off'
}

// the loop function runs over and over again forever

int counter = 0;
uint32_t lastBlinkTime = 0;

void loop() {
  uint32_t currentTime = millis();
  if(currentTime > lastBlinkTime + 40) {
    // We are 40ms further - time for some action!
    counter++;
    if((counter % 25) == 0) {
      // This code runs every second
      Serial.print("Current counter value: ");
      Serial.println(counter);
      if( counter % 2) {
        digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
      } else {
        digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
      }      
    }

    // Update color LED
    uint32_t rgbcolor = rgbled.ColorHSV(counter*200);      // See: https://learn.adafruit.com/adafruit-neopixel-uberguide/arduino-library-use
    rgbled.setPixelColor(0, rgbcolor);
    rgbled.show();
    
    // Don't forget to reset our timer
    lastBlinkTime = currentTime;
  }
}

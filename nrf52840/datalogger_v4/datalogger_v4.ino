/* Datalogger for customized gestures
*  Using Xiao BLE Sense (nrf52840), Touch MPR121, IMU LSM6DS3, Flex sensor (Spectra Symbol)
*
*  Creative Interactions Lab @Carleton University
*  Designed/Programmed by RC  2025/05/20
*
*  Useful instructions for UF2 at https://mithundotdas.medium.com/xiao-firmware-update-uf2-e93a94fd499f
*  BLE functions from https://github.com/adafruit/Adafruit_nRF52_Arduino/blob/master/libraries/Bluefruit52Lib/examples/Peripheral/blehid_keyboard/blehid_keyboard.ino
*  MSC functions from https://elm-chan.org/fsw/ff/ and https://github.com/adafruit/Adafruit_TinyUSB_Arduino
*  QSPI flash management from https://forum.seeedstudio.com/t/xiao-sense-nrf52840-littlefs-on-qspi-flash-with-freertos/274221/3
*  Flash formatting from https://github.com/adafruit/Adafruit_SPIFlash/blob/master/examples/SdFat_format/SdFat_format.ino
*  MPR121 configuration and filtering from https://www.nxp.com/docs/en/application-note/AN4600.pdf#page=7.08 
*  IMU library and reading from https://wiki.seeedstudio.com/XIAO-BLE-Sense-IMU-Usage/ 
*/


//#define TOUCH
#define GYRO
#define BLE

#include <Wire.h>
#include <stdint.h>  // For standard integer types
#include <stdlib.h>  // For abs()

#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_SPIFlash.h>
#include "Adafruit_TinyUSB.h"
#include "LSM6DS3.h"


//--------------------------------------------------------------------+
// MACROS & Globals
//--------------------------------------------------------------------+
const int FLEX_sensorPin = A0;
const int RED_ledPin = 11;
const int BLUE_ledPin = 12;
const int GREEN_ledPin = 13;

int touchDX, touchDY;
float accX, accY, accZ;
int flexValue;
static unsigned long last_interval_ms = 0;

// #define FREQUENCY_HZ 50
#define INTERVAL_MS 20  //(1000 / (FREQUENCY_HZ + 1))

#ifdef GYRO
LSM6DS3 myIMU(I2C_MODE, 0x6A);  //I2C device address 0x6A
#define CONVERT_G_TO_MS2 9.80665f
#endif



#include "mpr.h"
#include "data.h"
#include "ble.h"


void setup() {

  //pins I'm using for the ADDR and IRQ of the MPR
  pinMode(7, OUTPUT);
  pinMode(8, OUTPUT);
  digitalWrite(8, HIGH);
  digitalWrite(7, HIGH);

  //LED initial msg
  pinMode(BLUE_ledPin, OUTPUT);
  pinMode(RED_ledPin, OUTPUT);
  pinMode(GREEN_ledPin, OUTPUT);
  digitalWrite(BLUE_ledPin, HIGH);
  digitalWrite(RED_ledPin, HIGH);
  digitalWrite(GREEN_ledPin, HIGH);


  Serial.begin(115200);
  unsigned long startTime = millis();
  while (!Serial && (millis() - startTime < 3000)) {  // Wait for serial, with 5-sec timeout
    digitalWrite(RED_ledPin, HIGH);
    delay(10);
    digitalWrite(RED_ledPin, LOW);
    delay(10);
  }

  digitalWrite(BLUE_ledPin, LOW);
  Serial.println("--------Firmware v4------------");

  Wire.begin();  // Initialize I2C (use Wire.begin(SDA_PIN, SCL_PIN) if non-default)
  DATA_init();

  startTime = millis();
  while (millis() - startTime < 3000) {  // Wait for serial, with 5-sec timeout
    digitalWrite(RED_ledPin, HIGH);
    delay(100);
    digitalWrite(RED_ledPin, LOW);
    delay(100);
  }

  Serial.println("-----Data initialized--------");


#ifdef TOUCH
  MPR121_init_arduino();
  PreTouchStatus.Touched = false;  // Initialize previous states
#endif

#ifdef GYRO
  if (myIMU.begin() != 0) {
    Serial.println("IMU Device error");
  } else {
    Serial.println("IMU initialized");
  }
#endif

  startTime = millis();
  while (millis() - startTime < 3000) {  // Wait for serial, with 5-sec timeout
    digitalWrite(GREEN_ledPin, HIGH);
    delay(100);
    digitalWrite(GREEN_ledPin, LOW);
    delay(100);
  }
  Serial.println("----Sensors initialized------");

#ifdef BLE
  Bluefruit.begin();                        // HID Device can have a min connection interval of 9*1.25 = 11.25 ms
  Bluefruit.Periph.setConnInterval(9, 16);  // min = 9*1.25=11.25 ms, max = 16*1.25=20ms
  Bluefruit.setTxPower(4);                  // Check bluefruit.h for supported values

  // Configure and Start Device Information Service
  bledis.setManufacturer("Creative Interactions Lab");
  bledis.setModel("Xiao Sense - AdaptiveStylus");
  bledis.begin();
  blehid.begin();  // BLE HID

  // Set callback for set LED from central
  // blehid.setKeyboardLedCallback(set_keyboard_led);

  // Set up and start advertising
  startAdv();
  Serial.println("------BLE initialized------");
  Serial.println("------------------------------------");
  Serial.println("Press S to save to buffer");
  Serial.println("Press X to format memory");
  Serial.println("Press B to send command over BLE");
  Serial.println();
#endif
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK  // For Adafruit nRF52 core, TinyUSBDevice.task() is often handled by SoftDevice events or a scheduler.
  TinyUSBDevice.task();           // Calling it here might be redundant but generally harmless.
#endif

  if (millis() >= last_interval_ms + INTERVAL_MS) {
    last_interval_ms = millis();

    if (last_interval_ms % 5000 <= 50) {
      if (fs_mounted) digitalWrite(GREEN_ledPin, LOW);                  //could be used FS disconnected?
      if (Bluefruit.connected()) digitalWrite(BLUE_ledPin, LOW);      //BLE connected?
      if (false) digitalWrite(RED_ledPin, LOW);                      //could be used LOW battery?
    } else {
      digitalWrite(GREEN_ledPin, HIGH);
      digitalWrite(BLUE_ledPin, HIGH);
      digitalWrite(RED_ledPin, HIGH);
    }


    flexValue = analogRead(FLEX_sensorPin);
#ifdef TOUCH
    Read_MPR121_ele_register_arduino();  // Reads all relevant registers into readingArray
    Get_ele_data_arduino();              // Calculates ele_delta from readingArray
    Get_touch_status_arduino();          // Updates CurrTouchStatus from readingArray

    if (CurrTouchStatus.Touched) {
      Intp5x7_arduino();  // Calculates SampSumX/Y, SampSX/Y from ele_delta
    } else {
      // If not touched, clear sample sums for next touch to avoid stale data influencing first new touch
      SampSumX = 0;
      SampSX = 0;
      SampSumY = 0;
      SampSY = 0;
    }

    Pol_mouse_dat_arduino();  // Processes data, calculates DX, DY
#endif

#ifdef GYRO
    accX = myIMU.readFloatAccelX() * CONVERT_G_TO_MS2;
    accY = myIMU.readFloatAccelY() * CONVERT_G_TO_MS2;
    accZ = myIMU.readFloatAccelZ() * CONVERT_G_TO_MS2;
#endif

    logDataToCircularBuffer();

    Serial.print(last_interval_ms);
    Serial.print(',');
    Serial.print(accX, 1);  // Print with 4 decimal places
    Serial.print(',');
    Serial.print(accY, 1);
    Serial.print(',');
    Serial.print(accZ, 1);
    Serial.print(',');
    Serial.print(touchDX);
    Serial.print(',');
    Serial.print(touchDY);
    Serial.print(',');
    Serial.print(flexValue);
    Serial.println();

    Serial.flush();  //needed?
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'X') {
      Serial.setTimeout(30000);  // Increase timeout to print message less frequently.
      do {
        Serial.println(F("RECEIVED X !!!!!!!!!!!!!!!!!!!!!!!!@@@@@!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"));
        Serial.println(F("This sketch will ERASE ALL DATA on the flash chip and format it with a new filesystem!"));
        Serial.println(F("Type OK (all caps) and press enter to continue."));
        Serial.println(F("(Meanwhile, the whole device will not work. Unplug/plug to abort format)"));
        Serial.println(F("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"));
      } while (!Serial.find((char *)"OK"));
      // Call fatfs begin and passed flash object to initialize file system
      Serial.println(F("Creating and formatting FAT filesystem (this takes ~ seconds)..."));

      format_fat12();
      check_fat12();

      Serial.println(F("Flash chip successfully formatted with new empty filesystem!"));

    } else if (c == 'S') {
      Serial.println(F("Forcing to save buffer to flash chip"));
      saveBufferToFlash();
    } else if (c == 'B') {
      Serial.println(F("Sending CTRL+Z over BLE command"));
#ifdef BLE
      delay(100);
      blehid.keyPress(26);  // this stands for CTRL+Z
      delay(5);
      blehid.keyRelease();
      delay(5);
      // blehid.mouseMove(0, MOVE_STEP);
      // Serial.println("Sending an N");
      blehid.keyPress('n');
      delay(5);
      blehid.keyRelease();
#endif
    } else {
      Serial.println(F("Command not recognized"));
      Serial.flush();
    }
  }
}

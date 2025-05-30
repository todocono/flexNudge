
//#define TOUCH
//#define GYRO

#include <Wire.h>
#include <stdint.h>  // For standard integer types
#include <stdlib.h>  // For abs()

#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_SPIFlash.h>
#include "Adafruit_TinyUSB.h"


#include "mpr.h"
#include "data.h"
#include "LSM6DS3.h"

LSM6DS3 myIMU(I2C_MODE, 0x6A);  //I2C device address 0x6A


//--------------------------------------------------------------------+
// MACROS & Globals
//--------------------------------------------------------------------+

#define BLINK_MSC_NOT_MOUNTED (500)
#define BLINK_MSC_MOUNTED (2500)

const int RED_ledPin = 11;
const int BLUE_ledPin = 12;
const int GREEN_ledPin = 13;

#define CONVERT_G_TO_MS2 9.80665f
#define FREQUENCY_HZ 50
#define INTERVAL_MS (1000 / (FREQUENCY_HZ + 1))

static unsigned long last_interval_ms = 0;


void led_blinky_cb(void) {
  static uint32_t last_blink_ms = 0;
  static bool led_state = false;
  uint32_t blink_interval_ms = fs_mounted ? BLINK_MSC_MOUNTED : BLINK_MSC_NOT_MOUNTED;

  if (millis() - last_blink_ms < blink_interval_ms) return;
  last_blink_ms = millis();

  digitalWrite(LED_BUILTIN, led_state);
  led_state = !led_state;
}



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
  delay(100);
  digitalWrite(BLUE_ledPin, LOW);
  delay(300);
  digitalWrite(RED_ledPin, LOW);
  delay(300);
  delay(300);
  digitalWrite(RED_ledPin, LOW);


  Serial.begin(115200);
  unsigned long startTime = millis();
  while (!Serial && (millis() - startTime < 3000)) {  // Wait for serial, with 5-sec timeout
    delay(10);
  }

  Wire.begin();  // Initialize I2C (use Wire.begin(SDA_PIN, SCL_PIN) if non-default)

#ifdef TOUCH
  MPR121_init_arduino();
  PreTouchStatus.Touched = false;  // Initialize previous states
  PrevSumX = 0;
  PrevSX = 0;
  PrevSumY = 0;
  PrevSY = 0;
  PrevPosX = 0;
  PrevPosY = 0;
  PrevDX = 0;
  PrevDY = 0;
#endif

#ifdef GYRO
#warning "Including LM6 gyro"
  if (myIMU.begin() != 0) {
    Serial.println("IMU Device error");
  } else {
    Serial.println("IMU initialized");
  }
#endif
  DATA_init();
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK  // For Adafruit nRF52 core, TinyUSBDevice.task() is often handled by SoftDevice events or a scheduler.
  TinyUSBDevice.task();           // Calling it here might be redundant but generally harmless.
#endif


  //led_blinky_cb();

  if (millis() > last_interval_ms + INTERVAL_MS) {
    last_interval_ms = millis();
    Serial.print(last_interval_ms);
    Serial.print(',');

    // Open the datalogging file for writing.  The FILE_WRITE mode will open
    // the file for appending, i.e. it will add new data to the end of the file.
    File32 dataFile = fatfs.open(FILE_NAME, FILE_WRITE);
    // Check that the file opened successfully and write a line to it.
    if (dataFile) {

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
      Serial.print(DX);
      Serial.print(',');
      Serial.println(DY);

#ifdef GYRO

      Serial.print(myIMU.readFloatAccelX() * CONVERT_G_TO_MS2, 4);
      Serial.print(',');
      Serial.print(myIMU.readFloatAccelY() * CONVERT_G_TO_MS2, 4);
      Serial.print(',');
      Serial.println(myIMU.readFloatAccelZ() * CONVERT_G_TO_MS2, 4);

#endif
      // int reading = random(0, 100);
      // Write a line to the file.  You can use all the same print functions
      // as if you're writing to the serial monitor.  For example to write
      // two CSV (commas separated) values:
      dataFile.print(last_interval_ms, DEC);    //timestamp (needed?)
      dataFile.print(",");
      dataFile.print(DX, DEC);
      dataFile.print(",");
      dataFile.print(DY, DEC);
      dataFile.print(",");
      dataFile.print(random(0, 100), DEC);  //flex
      dataFile.print(",");
      dataFile.print(random(0, 100), DEC);  //Ax
      dataFile.print(",");
      dataFile.print(random(0, 100), DEC);  //Ay
      dataFile.print(",");
      dataFile.print(random(0, 100), DEC);  //Az
      dataFile.println();
      // Finally close the file when done writing.  This is smart to do to make
      // sure all the data is written to the file.
      dataFile.close();
      //Serial.print("Wrote new measurement to data file:");
      //Serial.println(reading, DEC);
    } else {
      Serial.println("Failed to open data file for writing!");
    }

    Serial.flush();
    // delay(20);
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

    } else {
      Serial.println(F("Send X (cap) to format whole flash memory"));
    }
  }
}

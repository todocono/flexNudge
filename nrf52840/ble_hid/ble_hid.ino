
// #include <LSM6DS3.h>
// #include <Wire.h>
/* Constant defines -------------------------------------------------------- */
#define CONVERT_G_TO_MS2 9.80665f
#define MAX_ACCEPTED_RANGE 2.0f  // starting 03/2022, models are generated setting range to +-2, but this example use Arudino library which set range to +-4g. If you are using an older model, ignore this value and use 4.0f instead

#include <bluefruit.h>

BLEDis bledis;
BLEHidAdafruit blehid;


bool hasKeyPressed = false;

#define MOVE_STEP 30

/*
 ** NOTE: If you run into TFLite arena allocation issue.
 **
 ** This may be due to may dynamic memory fragmentation.
 ** Try defining "-DEI_CLASSIFIER_ALLOCATION_STATIC" in boards.local.txt (create
 ** if it doesn't exist) and copy this file to
 ** `<ARDUINO_CORE_INSTALL_PATH>/arduino/hardware/<mbed_core>/<core_version>/`.
 **
 ** See
 ** (https://support.arduino.cc/hc/en-us/articles/360012076960-Where-are-the-installed-cores-located-)
 ** to find where Arduino installs cores on your machine.
 **
 ** If the problem persists then there's not enough memory for this model and application.
 */

//U8X8_SSD1306_64X48_ER_HW_I2C u8x8(/* reset=*/ U8X8_PIN_NONE);

/* Private variables ------------------------------------------------------- */
static bool debug_nn = false;  // Set this to true to see e.g. features generated from the raw signal
// LSM6DS3 myIMU(I2C_MODE, 0x6A);
/**
* @brief      Arduino setup function
*/

const int RED_ledPin = 11;
const int BLUE_ledPin = 12;
const int GREEN_ledPin = 13;

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  //u8g2.begin();
  //u8x8.begin();
  Serial.println("BLE HID Demo");

  //if (!IMU.begin()) {
  // if (!myIMU.begin()) {
  //   ei_printf("Failed to initialize IMU!\r\n");
  // } else {
  //   ei_printf("IMU initialized\r\n");
  // }



  Serial.println("Starting...");
  Serial.println("-----------------------------\n");
  Serial.println("Go to your phone's Bluetooth settings to pair your device");
  Serial.println("then open an application that accepts mouse input");
  Serial.println();
  Bluefruit.begin();
  // HID Device can have a min connection interval of 9*1.25 = 11.25 ms
  Bluefruit.Periph.setConnInterval(9, 16);  // min = 9*1.25=11.25 ms, max = 16*1.25=20ms
  Bluefruit.setTxPower(4);                  // Check bluefruit.h for supported values

  // Configure and Start Device Information Service
  bledis.setManufacturer("Creative Interactions Lab");
  bledis.setModel("Xiao Sense - AdaptiveStylus");
  bledis.begin();

  // BLE HID
  blehid.begin();


  // Set callback for set LED from central
 // blehid.setKeyboardLedCallback(set_keyboard_led);


  // Set up and start advertising
  startAdv();
}



void startAdv(void) {
  // Advertising packet
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addTxPower();
  Bluefruit.Advertising.addAppearance(BLE_APPEARANCE_HID_MOUSE);

  // Include BLE HID service
  Bluefruit.Advertising.addService(blehid);

  // There is enough room for 'Name' in the advertising packet
  Bluefruit.Advertising.addName();

  /* Start Advertising
   * - Enable auto advertising if disconnected
   * - Interval:  fast mode = 20 ms, slow mode = 152.5 ms
   * - Timeout for fast mode is 30 seconds
   * - Start(timeout) with timeout = 0 will advertise forever (until connected)
   * 
   * For recommended advertising interval
   * https://developer.apple.com/library/content/qa/qa1931/_index.html   
   */
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);  // in unit of 0.625 ms
  Bluefruit.Advertising.setFastTimeout(30);    // number of seconds in fast mode
  Bluefruit.Advertising.start(0);              // 0 = Don't stop advertising after n seconds
}




void loop() {

  //delay(2000); //needed?
  // blehid.mouseMove(0, MOVE_STEP);
  Serial.println("Sending an N");
  blehid.keyPress('n');
  delay(5);
  blehid.keyRelease();
  delay (2000);
}


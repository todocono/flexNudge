/*********************************************************************
 * Combined TinyUSB MSC with QSPI Flash and SdFat
 * (SdFat V1.x.x Compatible, No .card(), No .isInitialized())
 *********************************************************************/

#include <SPI.h>
#include "Adafruit_SPIFlash.h"
#include "Adafruit_TinyUSB.h"
#include "SdFat.h"  // SdFat Library (Assumed V1.x.x based on previous errors)

// Helper macro for pragma message (Optional, for debug)
#define SD_FAT_STRINGIFY_HELPER(x) #x
#define SD_FAT_STRINGIFY(x) SD_FAT_STRINGIFY_HELPER(x)

// #ifdef SD_FAT_VERSION
// #pragma message("SdFat Version detected by compiler: " SD_FAT_STRINGIFY(SD_FAT_VERSION))
// #else
// #pragma message("SD_FAT_VERSION not defined by SdFat.h, assuming SdFat V1.x.x")
// #endif


// Built from the P25Q16H datasheet.
SPIFlash_Device_t const P25Q16H{
  .total_size = (1UL << 21),  // 2MiB
  .start_up_time_us = 10000,  // Don't know where to find that value

  .manufacturer_id = 0x85,
  .memory_type = 0x60,
  .capacity = 0x15,

  .max_clock_speed_mhz = 55,
  .quad_enable_bit_mask = 0x02,      // Datasheet p. 27
  .has_sector_protection = 1,        // Datasheet p. 27
  .supports_fast_read = 1,           // Datasheet p. 29
  .supports_qspi = 1,                // Obviously
  .supports_qspi_writes = 1,         // Datasheet p. 41
  .write_status_register_split = 1,  // Datasheet p. 28
  .single_status_byte = 0,           // 2 bytes
  .is_fram = 0,                      // Flash Memory
};
// If your P25Q16H definition from your QSPI test sketch worked, ensure that P25Q16H struct is defined above this line,
// and then uncomment the following line and comment out the Winbond definition above:
// extern SPIFlash_Device_t const P25Q16H; // If defined in another .cpp/.h
// SPIFlash_Device_t const QSPI_FLASH_DEVICE = P25Q16H;


// QSPI transport layer
Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash flash(&flashTransport);

// SdFat object
FatFileSystem fatfs;  // For SdFat V1, FatFileSystem is often a typedef for FatVolume

// USB Mass Storage object
Adafruit_USBD_MSC usb_msc;

// State variables
bool fs_mounted = false;


//--------------------------------------------------------------------+
// MACROS & Globals for LED
//--------------------------------------------------------------------+
#ifdef LED_BUILTIN
#define BLINK_MSC_NOT_MOUNTED (500)
#define BLINK_MSC_MOUNTED (2500)
uint8_t led_pin = LED_BUILTIN;
#else
// Define a default LED pin for XIAO nRF52840 if LED_BUILTIN is not set by BSP
// Common LED pins: PIN_LED_TXL (Green), PIN_LED_RXL (Blue), P0_13 (User Blue on some)
// Adjust if your board's LED is different.
uint8_t led_pin = PIN_LED_TXL;
#define BLINK_MSC_NOT_MOUNTED (500)
#define BLINK_MSC_MOUNTED (2500)
#endif


// // file system object from SdFat
// FatVolume fatfs;

// Configuration for the datalogging file:
#define FILE_NAME "data.csv"

void led_blinky_cb(void) {
  static uint32_t last_blink_ms = 0;
  static bool led_state = false;
  uint32_t blink_interval_ms = fs_mounted ? BLINK_MSC_MOUNTED : BLINK_MSC_NOT_MOUNTED;

  if (millis() - last_blink_ms < blink_interval_ms) return;
  last_blink_ms = millis();

  if (led_pin != 0xFF && led_pin != 0) {
    digitalWrite(led_pin, led_state);
    led_state = !led_state;
  }
}

void setup() {


  while (!Serial) delay(100);  // wait for native usb



  delay(2000);  // Optional: Give time to open Serial Monitor

  Serial.println("Adafruit TinyUSB MSC QSPI Datalogger - Step 2 (SdFat V1, No .card(), No .isInitialized())");
  Serial.println("Initializing QSPI Flash...");

  flash.begin(&P25Q16H, 1);

  pinMode(led_pin, OUTPUT);
  flash.setIndicator(led_pin, false);


  // if (!flash.begin(&QSPI_FLASH_DEVICE, 1)) {
  //   Serial.println("Error: Failed to initialize QSPI flash! Check JEDEC ID and chip definition.");
  //   while (1) {
  //     if (led_pin != 0xFF && led_pin != 0) {
  //       digitalWrite(led_pin, HIGH);
  //       delay(100);
  //       digitalWrite(led_pin, LOW);
  //       delay(100);
  //     }
  //   }
  // }
  Serial.print("QSPI Flash JEDEC ID: 0x");
  Serial.println(flash.getJEDECID(), HEX);
  Serial.print("Flash size: ");
  Serial.print(flash.size() / 1024.0 / 1024.0, 2);
  Serial.println(" MB");

  Serial.println("Initializing SdFat on QSPI Flash (Attempting to mount)...");

  fs_mounted = fatfs.begin(&flash);

  uint32_t msc_block_count = 0;  // Total 512-byte blocks for MSC capacity

  if (!fs_mounted) {
    Serial.println("Failed to mount FATFS. Filesystem may not be formatted or is corrupted.");
    Serial.println("--------------------------------------------------------------------");
    Serial.println("IMPORTANT: For SdFat V1.x.x with QSPI flash, you usually need to");
    Serial.println("run a SEPARATE formatting sketch first (e.g., adapt SdFat's SdFormatter.ino)");
    Serial.println("OR try formatting the drive via your PC if it appears (even as corrupted).");
    Serial.println("--------------------------------------------------------------------");
    msc_block_count = flash.size() / 512;
    Serial.println("Filesystem not mounted, reporting raw flash size to USB host for MSC capacity.");
  } else {
    Serial.println("QSPI Flash FAT Filesystem mounted successfully!");
    uint32_t volumeClusterCount = fatfs.clusterCount();
    uint8_t sectorsPerCluster = fatfs.sectorsPerCluster();

    if (volumeClusterCount > 0 && sectorsPerCluster > 0) {
      msc_block_count = volumeClusterCount * sectorsPerCluster;
      Serial.print("Volume size (from FAT): ");
      Serial.print(msc_block_count * 512.0 / (1024.0 * 1024.0), 2);
      Serial.println(" MB");
    } else {
      Serial.println("Could not determine volume size from FAT. Using raw flash size for MSC capacity.");
      msc_block_count = flash.size() / 512;
    }
  }

  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  usb_msc.setMaxLun(1);
  usb_msc.setID(0, "XIAO", "QSPI_FLASH", "1.0");  // Changed name slightly

  if (msc_block_count == 0 && flash.size() > 0) {
    msc_block_count = flash.size() / 512;
    Serial.println("Warning: msc_block_count was 0, using raw flash size as fallback.");
  } else if (flash.size() == 0) {
    Serial.println("ERROR: flash.size() is 0! QSPI flash not detected or init failed.");
    // msc_block_count will be 0, MSC drive will likely be 0 size or fail.
  }

  usb_msc.setCapacity(0, msc_block_count, 512);  //I threw there 16
  usb_msc.setReadWriteCallback(0, msc_read_cb, msc_write_cb, msc_flush_cb);
  usb_msc.setUnitReady(0, true);
  usb_msc.begin();


  // If already enumerated, additional class driverr begin() e.g msc, hid, midi won't take effect until re-enumeration
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  if (msc_block_count > 0) {
    Serial.print("MSC Disk Capacity (reported to USB host): ");
    Serial.print(msc_block_count * 512.0 / (1024.0 * 1024.0), 2);
    Serial.println(" MB");
  } else {
    Serial.println("MSC Disk Capacity is 0. Drive will likely not work or show errors.");
  }

  Serial.println("USB MSC device configured.");
  if (!fs_mounted) {
    Serial.println("WARNING: Drive is likely NOT USABLE until QSPI flash is formatted with a FAT filesystem.");
  } else {
    Serial.println("Drive should be accessible. Check your PC.");
  }
}

void loop() {
#ifdef TINYUSB_NEED_POLLING_TASK
  TinyUSBDevice.task();
#endif
  // For Adafruit nRF52 core, TinyUSBDevice.task() is often handled by SoftDevice events or a scheduler.
  // Calling it here might be redundant but generally harmless.

  led_blinky_cb();

 // Open the datalogging file for writing.  The FILE_WRITE mode will open
  // the file for appending, i.e. it will add new data to the end of the file.
  File32 dataFile = fatfs.open(FILE_NAME, FILE_WRITE);
  // Check that the file opened successfully and write a line to it.
  if (dataFile) {
    // Take a new data reading from a sensor, etc.  For this example just
    // make up a random number.
    int reading = random(0, 100);
    // Write a line to the file.  You can use all the same print functions
    // as if you're writing to the serial monitor.  For example to write
    // two CSV (commas separated) values:
    dataFile.print("Sensor #1");
    dataFile.print(",");
    dataFile.print(reading, DEC);
    dataFile.println();
    // Finally close the file when done writing.  This is smart to do to make
    // sure all the data is written to the file.
    dataFile.close();
    Serial.print("Wrote new measurement to data file:");
    Serial.println(reading,DEC);
  } else {
    Serial.println("Failed to open data file for writing!");
  }

  Serial.println("Trying again in 6 seconds...");

  // Wait 60 seconds.
  delay(6000L);
}
//--------------------------------------------------------------------+
// TinyUSB MSC Callbacks (SdFat V1.x.x Compatible - using flash object directly)
//--------------------------------------------------------------------+

int32_t msc_read_cb(uint32_t lba, void* buffer, uint32_t bufsize) {
  // Assuming flash.begin() in setup succeeded if we've reached here.

  uint32_t num_blocks = bufsize / 512;  // bufsize is always a multiple of 512 for MSC
  // Adafruit_SPIFlash::readBlocks expects number of 512-byte blocks
  bool success = flash.readBlocks(lba, (uint8_t*)buffer, num_blocks);

  // For debugging:
  Serial.print("MSC Read: LBA=");
  Serial.print(lba);
  Serial.print(", NBlk=");
  Serial.print(num_blocks);
  Serial.println(success ? " OK" : " Fail");

  return success ? bufsize : -1;
}

int32_t msc_write_cb(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
  // Assuming flash.begin() in setup succeeded.

  uint32_t num_blocks = bufsize / 512;
  // Adafruit_SPIFlash::writeBlocks expects number of 512-byte blocks
  bool success = flash.writeBlocks(lba, (uint8_t*)buffer, num_blocks);

  // For debugging:
  Serial.print("MSC Write: LBA=");
  Serial.print(lba);
  Serial.print(", NBlk=");
  Serial.print(num_blocks);
  Serial.println(success ? " OK" : " Fail");

  return success ? bufsize : -1;
}

void msc_flush_cb(void) {
  // Assuming flash.begin() in setup succeeded.

  // Adafruit_SPIFlash::syncBlocks ensures data is physically written
  flash.syncBlocks();

  // For debugging:
  Serial.println("MSC Flush");
}
#define DISK_LABEL "EXT FLASH"  // up to 11 characters
#define FILE_NAME "data.csv"    // Configuration for the datalogging file:
#define RAM_BUFFER_SIZE 100

//#define DEBUG_MSC

// Since SdFat doesn't fully support FAT12 such as format a new flash
// We will use Elm Cham's fatfs f_mkfs() to format
#include "ff.h"
#include "diskio.h"
#include "flash_devices.h"

Adafruit_FlashTransport_QSPI flashTransport;
Adafruit_SPIFlash flash(&flashTransport);

FatFileSystem fatfs;        // For SdFat V1, FatFileSystem is often a typedef for FatVolume
Adafruit_USBD_MSC usb_msc;  // USB Mass Storage object

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


struct SensorData {
  unsigned long timestamp;
  float ax, ay, az;
  int tdx, tdy;  // For touchDX, touchDY
  int flex;
};


bool fs_mounted = false;

// The RAM buffer
SensorData dataBuffer[RAM_BUFFER_SIZE];
int bufferWriteIndex = 0;         // Index for the next write operation in the circular buffer
int samplesInCircularBuffer = 0;  // Number of valid samples currently in the buffer (max RAM_BUFFER_SIZE)

void format_fat12(void) {
// Working buffer for f_mkfs.
#ifdef __AVR__
  uint8_t workbuf[512];
#else
  uint8_t workbuf[4096];
#endif

  // Elm Cham's fatfs objects
  FATFS elmchamFatfs;

  // Make filesystem.
  FRESULT r = f_mkfs("", FM_FAT, 0, workbuf, sizeof(workbuf));
  if (r != FR_OK) {
    Serial.print(F("Error, f_mkfs failed with error code: "));
    Serial.println(r, DEC);
    while (1) yield();
  }

  // mount to set disk label
  r = f_mount(&elmchamFatfs, "0:", 1);
  if (r != FR_OK) {
    Serial.print(F("Error, f_mount failed with error code: "));
    Serial.println(r, DEC);
    while (1) yield();
  }

  // Setting label
  Serial.println(F("Setting disk label to: " DISK_LABEL));
  r = f_setlabel(DISK_LABEL);
  if (r != FR_OK) {
    Serial.print(F("Error, f_setlabel failed with error code: "));
    Serial.println(r, DEC);
    while (1) yield();
  }

  // unmount
  f_unmount("0:");

  // sync to make sure all data is written to flash
  flash.syncBlocks();

  Serial.println(F("Formatted flash!"));
}

void check_fat12(void) {
  // Check new filesystem
  FatVolume fatfs;
  if (!fatfs.begin(&flash)) {
    Serial.println(F("Error, failed to mount newly formatted filesystem!"));
    while (1) delay(1);
  }
}





//--------------------------------------------------------------------+
// fatfs diskio
//--------------------------------------------------------------------+
extern "C" {

  DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    return 0;
  }

  DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    return 0;
  }

  DRESULT disk_read(
    BYTE pdrv,    /* Physical drive nmuber to identify the drive */
    BYTE *buff,   /* Data buffer to store read data */
    DWORD sector, /* Start sector in LBA */
    UINT count    /* Number of sectors to read */
  ) {
    (void)pdrv;
    return flash.readBlocks(sector, buff, count) ? RES_OK : RES_ERROR;
  }

  DRESULT disk_write(
    BYTE pdrv,        /* Physical drive nmuber to identify the drive */
    const BYTE *buff, /* Data to be written */
    DWORD sector,     /* Start sector in LBA */
    UINT count        /* Number of sectors to write */
  ) {
    (void)pdrv;
    return flash.writeBlocks(sector, buff, count) ? RES_OK : RES_ERROR;
  }

  DRESULT disk_ioctl(
    BYTE pdrv, /* Physical drive nmuber (0..) */
    BYTE cmd,  /* Control code */
    void *buff /* Buffer to send/receive control data */
  ) {
    (void)pdrv;

    switch (cmd) {
      case CTRL_SYNC:
        flash.syncBlocks();
        return RES_OK;

      case GET_SECTOR_COUNT:
        *((DWORD *)buff) = flash.size() / 512;
        return RES_OK;

      case GET_SECTOR_SIZE:
        *((WORD *)buff) = 512;
        return RES_OK;

      case GET_BLOCK_SIZE:
        *((DWORD *)buff) = 8;  // erase block size in units of sector size
        return RES_OK;

      default:
        return RES_PARERR;
    }
  }
}


//--------------------------------------------------------------------+
// TinyUSB MSC Callbacks (SdFat V1.x.x Compatible - using flash object directly)
//--------------------------------------------------------------------+

int32_t msc_read_cb(uint32_t lba, void *buffer, uint32_t bufsize) {
  // Assuming flash.begin() in setup succeeded if we've reached here.

  uint32_t num_blocks = bufsize / 512;  // bufsize is always a multiple of 512 for MSC
  // Adafruit_SPIFlash::readBlocks expects number of 512-byte blocks
  bool success = flash.readBlocks(lba, (uint8_t *)buffer, num_blocks);

#ifdef DEBUG_MSC  // For debugging:
  Serial.print("MSC Read: LBA=");
  Serial.print(lba);
  Serial.print(", NBlk=");
  Serial.print(num_blocks);
  Serial.println(success ? " OK" : " Fail");
#endif
  return success ? bufsize : -1;
}

int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
  // Assuming flash.begin() in setup succeeded.

  uint32_t num_blocks = bufsize / 512;
  // Adafruit_SPIFlash::writeBlocks expects number of 512-byte blocks
  bool success = flash.writeBlocks(lba, (uint8_t *)buffer, num_blocks);


#ifdef DEBUG_MSC  // For debugging:
  Serial.print("MSC Write: LBA=");
  Serial.print(lba);
  Serial.print(", NBlk=");
  Serial.print(num_blocks);
  Serial.println(success ? " OK" : " Fail");
#endif
  return success ? bufsize : -1;
}

void msc_flush_cb(void) {
  // Assuming flash.begin() in setup succeeded.

  // Adafruit_SPIFlash::syncBlocks ensures data is physically written
  flash.syncBlocks();

#ifdef DEBUG_MSC  // For debugging:
  Serial.println("MSC Flush");
#endif
}



void DATA_init(void) {

  flash.begin(&P25Q16H, 1);

  // Initialize flash library and check its chip ID.
  if (!flash.begin()) {
    Serial.println(F("Error, failed to initialize flash chip!"));
    while (1) yield();
  }


  Serial.print(F("Flash chip JEDEC ID: 0x"));
  Serial.println(flash.getJEDECID(), HEX);
  Serial.print(F("Flash size: "));
  Serial.print(flash.size() / 1024.0 / 1024.0, 2);
  Serial.println(" MB");

  Serial.println("Initializing SdFat on QSPI Flash (Attempting to mount)...");
  fs_mounted = fatfs.begin(&flash);

  uint32_t msc_block_count = 0;  // Total 512-byte blocks for MSC capacity

  delay(1000);
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
    // uint32_t volumeClusterCount = fatfs.clusterCount();
    // uint8_t sectorsPerCluster = fatfs.sectorsPerCluster();

    //This calculation sometimes didn't show the size (why??)

    // if (volumeClusterCount > 0 && sectorsPerCluster > 0) {
    //   msc_block_count = volumeClusterCount * sectorsPerCluster;
    //   Serial.print("Volume size (from FAT): ");
    //   Serial.print(msc_block_count * 512.0 / (1024.0 * 1024.0), 2);
    //   Serial.println(" MB");
    // } else {
    //   Serial.println("Could not determine volume size from FAT. Using raw flash size for MSC capacity.");
    //   msc_block_count = flash.size() / 512;
    // }
  }
  delay(100);
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }
  delay(100);
  usb_msc.setMaxLun(1);
  usb_msc.setID(0, "XIAO", "QSPI_FLASH", "1.0");  // didn't work when I changed name slightly
  delay(100);

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



void logDataToCircularBuffer(void) {
  // Store data at the current bufferWriteIndex
  dataBuffer[bufferWriteIndex].timestamp = last_interval_ms;  // last_interval_ms holds the current sample's timestamp
  dataBuffer[bufferWriteIndex].ax = accX;
  dataBuffer[bufferWriteIndex].ay = accY;
  dataBuffer[bufferWriteIndex].az = accZ;
  dataBuffer[bufferWriteIndex].tdx = touchDX;
  dataBuffer[bufferWriteIndex].tdy = touchDY;
  dataBuffer[bufferWriteIndex].flex = flexValue;

  // Move to the next index, wrap around if necessary
  bufferWriteIndex++;
  if (bufferWriteIndex >= RAM_BUFFER_SIZE) {
    bufferWriteIndex = 0;  // Wrap around
  }

  // Keep track of how many valid items are in the buffer, capped at RAM_BUFFER_SIZE
  if (samplesInCircularBuffer < RAM_BUFFER_SIZE) {
    samplesInCircularBuffer++;
  }

  // Optional: Indicate buffer status for debugging
  // static unsigned long lastDebugPrint = 0;
  // if (millis() - lastDebugPrint > 1000) { // Print every second
  //   lastDebugPrint = millis();
  //   Serial.print("Samples in buffer: "); Serial.print(samplesInCircularBuffer);
  //   Serial.print(", Next write index: "); Serial.println(bufferWriteIndex);
  // }

  // Open the datalogging file for writing.  The FILE_WRITE mode will open
  // the file for appending, i.e. it will add new data to the end of the file.
  // File32 dataFile = fatfs.open(FILE_NAME, FILE_WRITE);
  // // Check that the file opened successfully and write a line to it.
  // if (dataFile) {
  //   // int reading = random(0, 100);
  //   // Write a line to the file.  You can use all the same print functions
  //   // as if you're writing to the serial monitor.  For example to write
  //   // two CSV (commas separated) values:
  //   dataFile.print(last_interval_ms, DEC);  //timestamp (needed?)
  //   dataFile.print(",");
  //   dataFile.print(touchDX, DEC);
  //   dataFile.print(",");
  //   dataFile.print(touchDY, DEC);
  //   dataFile.print(",");
  //   dataFile.print(random(0, 100), DEC);  //flex
  //   dataFile.print(",");
  //   dataFile.print(random(0, 100), DEC);  //Ax
  //   dataFile.print(",");
  //   dataFile.print(random(0, 100), DEC);  //Ay
  //   dataFile.print(",");
  //   dataFile.print(random(0, 100), DEC);  //Az
  //   dataFile.println();
  //   // Finally close the file when done writing.  This is smart to do to make
  //   // sure all the data is written to the file.
  //   dataFile.close();
  //   //Serial.print("Wrote new measurement to data file:");
  //   //Serial.println(reading, DEC);
  // } else {
  //   Serial.println("Failed to open data file for writing!");
  // }
}

// Function to write the circular buffer content to flash
void saveBufferToFlash() {
  if (samplesInCircularBuffer == 0) {
    Serial.println("Buffer empty, nothing to save.");
    return;
  }

  Serial.print("Saving ");
  Serial.print(samplesInCircularBuffer);
  Serial.println(" samples to flash...");
  digitalWrite(GREEN_ledPin, HIGH);  // Indicate saving activity

  File32 dataFile = fatfs.open(FILE_NAME, FILE_WRITE);  // FILE_WRITE appends

  if (dataFile) {
    // Check if the file is new/empty to write a header
    if (dataFile.size() == 0) {
      Serial.println("Writing CSV header to new file.");
      dataFile.println("Timestamp,Ax,Ay,Az,MPR_dX,MPR_dY,Flex");
    }

    // Determine the starting index for reading from the circular buffer
    // to get data in chronological order.
    int readStartIndex = 0;
    if (samplesInCircularBuffer == RAM_BUFFER_SIZE) {
      // Buffer is full and has wrapped, oldest data is at current bufferWriteIndex
      readStartIndex = bufferWriteIndex;
    } else {
      // Buffer is not yet full, oldest data is at index 0
      readStartIndex = 0;
    }

    // Write data from the buffer
    for (int i = 0; i < samplesInCircularBuffer; i++) {
      int currentIndex = (readStartIndex + i) % RAM_BUFFER_SIZE;  // Calculate actual index in circular buffer

      dataFile.print(dataBuffer[currentIndex].timestamp);
      dataFile.print(",");
      dataFile.print(dataBuffer[currentIndex].ax, 1);  // 4 decimal places for floats
      dataFile.print(",");
      dataFile.print(dataBuffer[currentIndex].ay, 1);
      dataFile.print(",");
      dataFile.print(dataBuffer[currentIndex].az, 1);
      dataFile.print(",");
      dataFile.print(dataBuffer[currentIndex].tdx);
      dataFile.print(",");
      dataFile.print(dataBuffer[currentIndex].tdy);
      dataFile.print(",");
      dataFile.println(dataBuffer[currentIndex].flex);
    }
    dataFile.close();
    Serial.println("Data saved to flash.");


  } else {
    Serial.println("Failed to open data file for writing!");
  }
  digitalWrite(GREEN_ledPin, LOW);  // Turn off activity LED
}

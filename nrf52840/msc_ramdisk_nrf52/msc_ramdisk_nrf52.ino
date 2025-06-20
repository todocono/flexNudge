/*********************************************************************
 Adafruit invests time and resources providing this open source code,
 please support Adafruit and open-source hardware by purchasing
 products from Adafruit!

 MIT license, check LICENSE for more information
 Copyright (c) 2019 Ha Thach for Adafruit Industries
 All text above, and the splash screen below must be included in
 any redistribution
*********************************************************************/

#include "Adafruit_TinyUSB.h"

// 8KB is the smallest size that windows allow to mount
#define DISK_BLOCK_NUM  16
#define DISK_BLOCK_SIZE 512

#include "ramdisk.h"

Adafruit_USBD_MSC usb_msc;

// the setup function runs once when you press reset or power the board
void setup() {
  // Manual begin() is required on core without built-in support e.g. mbed rp2040
  if (!TinyUSBDevice.isInitialized()) {
    TinyUSBDevice.begin(0);
  }

  Serial.begin(115200);

  usb_msc.setMaxLun(1);

  // Set disk size and callback for Logical Unit 0 (LUN 0)
  usb_msc.setID(0, "Adafruit", "Lun0", "1.0");
  usb_msc.setCapacity(0, DISK_BLOCK_NUM, DISK_BLOCK_SIZE);
  usb_msc.setReadWriteCallback(0, ram0_read_cb, ram0_write_cb, ram0_flush_cb);
  usb_msc.setUnitReady(0, true);

  // // Set disk size and callback for Logical Unit 1 (LUN 1)
  // usb_msc.setID(1, "Adafruit", "Lun1", "1.0");
  // usb_msc.setCapacity(1, DISK_BLOCK_NUM, DISK_BLOCK_SIZE);
  // usb_msc.setReadWriteCallback(1, ram1_read_cb, ram1_write_cb, ram1_flush_cb);
  // usb_msc.setUnitReady(1, true);

  usb_msc.begin();

  // If already enumerated, additional class driverr begin() e.g msc, hid, midi won't take effect until re-enumeration
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }

  //while ( !Serial ) delay(10);   // wait for native usb
  Serial.println("Adafruit TinyUSB Mass Storage Disk");
}

void loop() {
  #ifdef TINYUSB_NEED_POLLING_TASK
  // Manual call tud_task since it isn't called by Core's background
  TinyUSBDevice.task();
  #endif
}


//--------------------------------------------------------------------+
// LUN 0 callback
//--------------------------------------------------------------------+

// Callback invoked when received READ10 command.
// Copy disk's data to buffer (up to bufsize) and 
// return number of copied bytes (must be multiple of block size) 
int32_t ram0_read_cb(uint32_t lba, void* buffer, uint32_t bufsize) {
  uint8_t const* addr = msc_disk0[lba];
  memcpy(buffer, addr, bufsize);

  return bufsize;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and 
// return number of written bytes (must be multiple of block size)
int32_t ram0_write_cb(uint32_t lba, uint8_t* buffer, uint32_t bufsize) {
  uint8_t* addr = msc_disk0[lba];
  memcpy(addr, buffer, bufsize);

  return bufsize;
}

// Callback invoked when WRITE10 command is completed (status received and accepted by host).
// used to flush any pending cache.
void ram0_flush_cb(void) {
  // nothing to do
}


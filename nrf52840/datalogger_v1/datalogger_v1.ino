// Adafruit SPI Flash FatFs Format Example
// Author: Tony DiCola
//
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// !!  NOTE: YOU WILL ERASE ALL DATA BY RUNNING THIS SKETCH!  !!
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//
// Usage:
// - Modify the pins and type of fatfs object in the config
//   section below if necessary (usually not necessary).
// - Upload this sketch to your M0 express board.
// - Open the serial monitor at 115200 baud.  You should see a
//   prompt to confirm formatting.  If you don't see the prompt
//   close the serial monitor, press the board reset button,
//   wait a few seconds, then open the serial monitor again.
// - Type OK and enter to confirm the format when prompted.
// - Partitioning and formatting will take about 30-60 seconds.
//   Once formatted a message will be printed to notify you that
//   it is finished.
//

#include <Wire.h>
#include <stdint.h>  // For standard integer types
#include <stdlib.h>  // For abs()

// --- Configuration ---
#define MPR121_I2C_ADDRESS 0x5A  // Default MPR121 Address

#include <SPI.h>
#include <SdFat.h>
#include <Adafruit_SPIFlash.h>
#include "Adafruit_TinyUSB.h"


// Since SdFat doesn't fully support FAT12 such as format a new flash
// We will use Elm Cham's fatfs f_mkfs() to format
#include "ff.h"
#include "diskio.h"
#include "flash_devices.h"

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


// up to 11 characters
#define DISK_LABEL "EXT FLASH"

// for flashTransport definition
// #include "flash_config.h"


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

#define BLINK_MSC_NOT_MOUNTED (500)
#define BLINK_MSC_MOUNTED (2500)



// // file system object from SdFat
// FatVolume fatfs;

// Configuration for the datalogging file:
#define FILE_NAME "data.csv"



// Define one of these filter types, or 0 for no specific filter logic in Pol_mouse_dat
#define FILTER_NONE 0
#define FILTER_G 1  // Example: Gaussian-like filter
#define FILTER_D 2  // Example: Derivative-like filter
#define FILTER_A 3  // Example: Another filter type

#define CURRENT_FILTER FILTER_G  // CHOOSE YOUR FILTER TYPE HERE

// Sensitivity thresholds for mouse movement - TUNE THESE!
// These are used in the DX/DY scaling part of Pol_mouse_dat_arduino
#define S_X 200  // Example: Increased from 20 due to larger coordinate range from GetPosXY
#define S_Y 200  // Example: Increased from 20

// Touch and Release Thresholds (as in original code)
// More sensitive (adjust these values as needed):
#define TouchThre 6  // Lower value means easier to trigger touch
#define ReleaThre 4  // Keep release slightly lower than touch


// --- Global Variables ---
// Buffer to store raw register data from MPR121
// Registers 0x00 to 0x2A (inclusive) = 43 bytes
#define MPR121_READ_LENGTH 0x2B  // 43 bytes
uint8_t readingArray[MPR121_READ_LENGTH];

// Delta values for each electrode (signal - baseline)
int16_t ele_delta[13];  // We'll calculate 13, use 12 for X/Y



// Touch status structure
struct TouchStatus {
  uint8_t Reg0;
  uint8_t Reg1;
  bool Touched;  // Using bool for Arduino
};


TouchStatus CurrTouchStatus;
TouchStatus PreTouchStatus = { 0, 0, false };  // Initialize previous state

// Variables for X/Y calculation from original code
long SampSumX = 0;
long SampSX = 0;
long SampSumY = 0;
long SampSY = 0;

long CurSumX = 0;
long CurSX = 0;
long CurSumY = 0;
long CurSY = 0;

long PrevSumX = 0;
long PrevSX = 0;
long PrevSumY = 0;
long PrevSY = 0;

int CurPosX = 0;  // Position values (output from GetPosXY, can be large)
int CurPosY = 0;
int PrevPosX = 0;
int PrevPosY = 0;

int SamDX = 0;  // Delta X/Y samples (difference between CurPosX and PrevPosX)
int SamDY = 0;
int CurDX = 0;  // Current delta X/Y (filtered version of SamDX/SamDY)
int CurDY = 0;
int PrevDX = 0;
int PrevDY = 0;

int DX = 0;  // Final mouse delta X output (scaled version of CurDX)
int DY = 0;  // Final mouse delta Y output (scaled version of CurDY)

uint8_t FstFlg = 0;   // Flag for ignoring initial samples
bool SndFlg = false;  // Another flag used in filter logic



// --- Low-level I2C Helper Functions (Arduino Wire equivalent) ---
void MPR121_write_reg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPR121_I2C_ADDRESS);
  Wire.write(reg);
  Wire.write(value);
  if (Wire.endTransmission() != 0) {
    Serial.print("I2C Write Error! Reg: 0x");
    Serial.println(reg, HEX);
  }
}

// Reads a block of 'length' bytes from 'startReg' into 'buffer'
bool MPR121_read_block(uint8_t startReg, uint8_t *buffer, uint8_t length) {
  Wire.beginTransmission(MPR121_I2C_ADDRESS);
  Wire.write(startReg);
  if (Wire.endTransmission(false) != 0) {  // false = send restart, keep connection active
    Serial.println("I2C Read Setup Error!");
    return false;
  }

  if (Wire.requestFrom((uint8_t)MPR121_I2C_ADDRESS, length) == length) {
    for (uint8_t i = 0; i < length; i++) {
      buffer[i] = Wire.read();
    }
    return true;
  }
  Serial.println("I2C Read Error or not enough bytes!");
  return false;
}


// --- MPR121 Initialization and Data Processing (Translated) ---
void MPR121_init_arduino() {
  // Stop mode to allow register changes
  MPR121_write_reg(0x5E, 0x00);  // ELECONF: Enter Stop mode
  delay(1);                      // Short delay after stop

  // Soft reset (optional, but good practice if unsure about initial state)
  MPR121_write_reg(0x80, 0x63);  // SOFT_RESET
  delay(10);                     // Allow time for reset

  // Baseline filtering (Original values from your code)
  MPR121_write_reg(0x2B, 0x01);  // MHD_R: Max Half Delta Rising
  MPR121_write_reg(0x2C, 0x01);  // NHD_R: Noise Half Delta Rising
  MPR121_write_reg(0x2D, 0x00);  // NCL_R: Noise Count Limit Rising
  MPR121_write_reg(0x2E, 0x00);  // FDL_R: Filter Delay Limit Rising

  MPR121_write_reg(0x2F, 0x01);  // MHD_F: Max Half Delta Falling
  MPR121_write_reg(0x30, 0x01);  // NHD_F: Noise Half Delta Falling
  MPR121_write_reg(0x31, 0xFF);  // NCL_F: Noise Count Limit Falling
  MPR121_write_reg(0x32, 0x02);  // FDL_F: Filter Delay Limit Falling

  MPR121_write_reg(0x33, 0x00);  // NHD_T: Noise Half Delta Touched (Proximity related)
  MPR121_write_reg(0x34, 0x00);  // NCL_T: Noise Count Limit Touched (Proximity related)
  MPR121_write_reg(0x35, 0x00);  // FDL_T: Filter Delay Limit Touched (Proximity related)

  // Touch/Release Thresholds for all 12 channels
  for (uint8_t i = 0; i < 12; i++) {
    MPR121_write_reg(0x41 + i * 2, TouchThre);  // ELEi_TouchThreshold
    MPR121_write_reg(0x42 + i * 2, ReleaThre);  // ELEi_ReleaseThreshold
  }

  // AFE (Analog Front End) Configuration - INCREASED SENSITIVITY
  // Original from your code: MPR121_write_reg(0x5C, 0xC0); // FFI=3 (1ms), CDC=0 (1uA) - VERY LOW SENSITIVITY
  MPR121_write_reg(0x5C, 0x20);  // New: FFI=0 (0.5ms sample interval), CDC=32uA.
                                 // Try 0x10 for 16uA, or 0x2F for 62uA if needed.
                                 // Values for CDC (bits 5-0): 0x01=1uA, 0x10=16uA, 0x20=32uA, 0x2F=63uA

  MPR121_write_reg(0x5D, 0x00);  // AFE_CONF2: SFI=0 (4 samples), ESI=0 (1ms sample interval) - Default from your code
                                 // ESI (bits 5-0): 0x00=1ms, 0x01=0.5ms. FFI and ESI combine for overall sample rate.

  // Debounce for touch and release (DT and DR bits)
  // Bits 6-4 for DT (touch debounce), Bits 2-0 for DR (release debounce)
  // Value 1 = 1 sample, Value 2 = 2 samples etc.
  MPR121_write_reg(0x5B, (1 << 4) | (1 << 0));  // Debounce: Touch=1 sample, Release=1 sample. (Sensitive)
                                                // Try (2 << 4) | (2 << 0) for 2 samples if too noisy.

  // Auto-Configuration Registers (Original values from your code)
  // These fine-tune the auto-calibration behavior.
  MPR121_write_reg(0x7B, 0xCB);  // AUTO_CONFIG_0 (USL, Target Level related for ELE0-ELE3)
  MPR121_write_reg(0x7C, 0x00);  // AUTO_CONFIG_0 (LSL, Target Level related for ELE0-ELE3) - This was not in your list, typically set. Default may be 0.
                                 // Default for 0x7C by Adafruit library is often 0x8C or LSL = USL * 0.65
  MPR121_write_reg(0x7D, 0xE4);  // AUTO_CONFIG_1 (USL, Target Level related for ELE4-ELE11)
  MPR121_write_reg(0x7E, 0x94);  // AUTO_CONFIG_CONTROL_0 (SFI, RETRY, BVA, ARE, ACE)
  MPR121_write_reg(0x7F, 0xCD);  // AUTO_CONFIG_CONTROL_1 (SCTS, ACFAILI, CL)


  // Enable electrodes and enter Run mode (12 Electrodes)
  MPR121_write_reg(0x5E, 0x0C);  // ELECONF: Enable 12 electrodes (ELE0-ELE11), Target: Run mode, No proximity.
}

void Read_MPR121_ele_register_arduino() {
  // Read registers 0x00 to 0x2A (43 bytes)
  if (!MPR121_read_block(0x00, readingArray, MPR121_READ_LENGTH)) {
    Serial.println("Failed to read MPR121 registers!");
  }
}

void Get_ele_data_arduino() {
  uint16_t tmp_sig, tmp_bas;
  for (uint8_t i = 0; i < 13; i++) {  // Process 13 values (ELE0-ELE11 and PROX/ELE12)
    // Filtered data: LSB at 0x04 + 2*i, MSB at 0x05 + 2*i
    // The original `readingArray[0x04 + 1 + 2*i]` is MSB, `readingArray[0x04 + 2*i]` is LSB
    tmp_sig = (readingArray[0x04 + (2 * i)] | ((uint16_t)readingArray[0x05 + (2 * i)] << 8));
    // MPR121 Filtered data is 10-bit. The mask 0xFFFC in original code seems to ignore 2 LSBs.
    // Sticking to original mask:
    tmp_sig &= 0xFFFC;

    // Baseline data: 1 byte per channel (0x1E + i), value is upper 8 bits of 10-bit baseline.
    tmp_bas = ((uint16_t)readingArray[0x1E + i]) << 2;

    ele_delta[i] = abs((int16_t)(tmp_sig - tmp_bas));
  }
}

void Get_touch_status_arduino() {
  CurrTouchStatus.Reg0 = readingArray[0x00];  // Touch Status LSB (ELE0-ELE7)
  CurrTouchStatus.Reg1 = readingArray[0x01];  // Touch Status MSB (ELE8-ELE11, plus OOR and PROX)

  // Check if any of the first 12 electrodes are touched
  if (((CurrTouchStatus.Reg0 & 0xFF) != 0) || ((CurrTouchStatus.Reg1 & 0x0F) != 0)) {  // Mask MSB for ELE8-11
    CurrTouchStatus.Touched = true;
  } else {
    CurrTouchStatus.Touched = false;
  }
}

// UPDATED for new electrode mapping
void Intp5x7_arduino() {
  SampSumX = 0;
  SampSX = 0;
  SampSumY = 0;
  SampSY = 0;

  for (uint8_t i = 0; i < 12; i++) {
    // Optional: Add a small threshold for ele_delta to be considered significant for interpolation
    // if (ele_delta[i] < 5) continue; // Example: ignore very small deltas for position calculation

    if (i >= 0 && i <= 4) {          // Electrodes 0-4 for Y-axis (Top Row: ELE0 is Y-pos 1, ELE4 is Y-pos 5)
      long weightY = (long)(i + 1);  // ELE0 -> 1, ELE1 -> 2, ..., ELE4 -> 5
      SampSumY += weightY * ele_delta[i];
      SampSY += ele_delta[i];
    } else if (i >= 5 && i <= 11) {         // Electrodes 5-11 for X-axis (Columns: ELE11=left, ELE5=right)
                                            // ELE11 is X-pos 1, ELE5 is X-pos 7
      long weightX = (long)(1 + (11 - i));  // ELE11 (i=11) -> weight 1; ELE5 (i=5) -> weight 7
      SampSumX += weightX * ele_delta[i];
      SampSX += ele_delta[i];
    }
  }
}

// Filter function from original code
int FilterXY(int prev, int spl, int m) {
  if (m == 1) return prev - (prev >> 2) + (spl >> 2);                                  // 0.75*prev + 0.25*spl
  else if (m == 2) return (prev >> 1) + (spl >> 1);                                    // 0.5*prev + 0.5*spl
  else if (m == 3) return prev - (prev >> 1) + (prev >> 3) + (spl >> 2) + (spl >> 3);  // 0.625*prev + 0.375*spl
  else if (m == 4) return prev - (prev >> 3) + (spl >> 3);                             // 0.875*prev + 0.125*spl
  return spl;                                                                          // Default if m is invalid
}

// Position calculation (fixed-point division) from original code
int GetPosXY(long fz, long fm) {
  int i;
  int w = 0;
  int q = 0, b = 0;
  int s = 0, g = 0;

  if (fm == 0) return 0;
  if (fz == 0) return 0;

  for (i = 0; i < 5; i++) {
    if (fz < fm) {
      if (i == 0) w = 0;
      if (i == 1) q = 0;
      if (i == 2) b = 0;
      if (i == 3) s = 0;
      if (i == 4) g = 0;
      fz = (fz << 3) + (fz << 1);
      continue;
    }
    while (1) {
      fz -= fm;
      if (i == 0) ++w;
      if (i == 1) ++q;
      if (i == 2) ++b;
      if (i == 3) ++s;
      if (i == 4) ++g;
      if (fz < fm) {
        fz = (fz << 3) + (fz << 1);
        break;
      }
    }
  }
  w = (w << 13) + (w << 10) + (w << 9) + (w << 8) + (w << 4);
  q = (q << 9) + (q << 8) + (q << 7) + (q << 6) + (q << 5) + (q << 3);
  b = (b << 6) + (b << 5) + (b << 2);
  s = (s << 3) + (s << 1);
  return w + q + b + s + g;
}

// Main mouse data polling and calculation logic
void Pol_mouse_dat_arduino() {
  if (CurrTouchStatus.Touched) {
    CurSumX = FilterXY(PrevSumX, SampSumX, 1);
    CurSX = FilterXY(PrevSX, SampSX, 1);
    CurSumY = FilterXY(PrevSumY, SampSumY, 1);
    CurSY = FilterXY(PrevSY, SampSY, 1);

    CurPosX = GetPosXY(CurSumX, CurSX);
    CurPosY = GetPosXY(CurSumY, CurSY);

#if CURRENT_FILTER == FILTER_G
    CurPosX = FilterXY(PrevPosX, CurPosX, 2);
    CurPosY = FilterXY(PrevPosY, CurPosY, 2);
    CurDX = CurPosX - PrevPosX;
    CurDY = CurPosY - PrevPosY;
#elif CURRENT_FILTER == FILTER_D
    SamDX = CurPosX - PrevPosX;
    SamDY = CurPosY - PrevPosY;
    CurDX = FilterXY(PrevDX, SamDX, 1);
    CurDY = FilterXY(PrevDY, SamDY, 1);
#elif CURRENT_FILTER == FILTER_A
    CurPosX = FilterXY(PrevPosX, CurPosX, 3);
    CurPosY = FilterXY(PrevPosY, CurPosY, 3);
    SamDX = CurPosX - PrevPosX;
    SamDY = CurPosY - PrevPosY;
    CurDX = FilterXY(PrevDX, SamDX, 3);
    CurDY = FilterXY(PrevDY, SamDY, 3);
#else  // No specific filter or FILTER_NONE
    CurDX = CurPosX - PrevPosX;
    CurDY = CurPosY - PrevPosY;
#endif

    if (!PreTouchStatus.Touched) {
#if CURRENT_FILTER == FILTER_D || CURRENT_FILTER == FILTER_A
      SndFlg = true;
#endif
      PrevSumX = SampSumX;
      PrevSX = SampSX;
      PrevSumY = SampSumY;
      PrevSY = SampSY;
      PrevPosX = GetPosXY(PrevSumX, PrevSX);  // Initialize PrevPosX/Y with first unfiltered position
      PrevPosY = GetPosXY(PrevSumY, PrevSY);
      FstFlg = 0;
    } else {
      if (FstFlg < 3) {
        FstFlg++;
        DX = 0;
        DY = 0;
      } else {
        // --- PASTE YOUR ORIGINAL COMPLEX DX/DY SCALING LOGIC HERE if desired ---
        // It started with:
        // if (((CurDX < S_X) && (CurDX >= 0)) || ((CurDX > -S_X) && (CurDX <= 0))) { DX = 0; } else { ... }
        // For now, using a simplified scaling based on S_X and S_Y.
        // CurDX and CurDY can be large depending on GetPosXY output.
        // S_X and S_Y need to be tuned according to the range of CurDX/CurDY you observe.
        if ((CurPosX != PrevPosX) || (CurPosY != PrevPosY)) {  // Only calculate DX/DY if position changed
          if (((CurDX < S_X) && (CurDX >= 0)) || ((CurDX > -S_X) && (CurDX <= 0))) {
            DX = 0;
          } else {
            DX = CurDX / S_X;                                    // Simple linear scaling. Add 1 for positive, -1 for negative if you want minimum step.
                                                                 // e.g. DX = CurDX > 0 ? (CurDX/S_X + 1) : (CurDX/S_X -1);
            if (DX == 0 && CurDX != 0) DX = CurDX > 0 ? 1 : -1;  // Ensure at least 1 if non-zero after division
          }

          if (((CurDY < S_Y) && (CurDY >= 0)) || ((CurDY > -S_Y) && (CurDY <= 0))) {
            DY = 0;
          } else {
            DY = CurDY / S_Y;
            if (DY == 0 && CurDY != 0) DY = CurDY > 0 ? 1 : -1;
          }
        } else {
          DX = 0;
          DY = 0;
        }
        // --- END OF DX/DY SCALING SECTION ---
      }
    }
    PrevSumX = CurSumX;
    PrevSX = CurSX;
    PrevSumY = CurSumY;
    PrevSY = CurSY;
    PrevPosX = CurPosX;
    PrevPosY = CurPosY;

#if CURRENT_FILTER == FILTER_D || CURRENT_FILTER == FILTER_A
    if (SndFlg) {
      SndFlg = false;
      PrevDX = SamDX;
      PrevDY = SamDY;
    } else {
      PrevDX = CurDX;
      PrevDY = CurDY;
    }
#else
    PrevDX = CurDX;
    PrevDY = CurDY;
#endif
    PreTouchStatus.Touched = true;

  } else {  // Not touched (finger released)
    FstFlg = 0;
    PreTouchStatus.Touched = false;
    DX = 0;
    DY = 0;
    // Optionally reset "previous" state variables here if desired for new touch.
    // e.g., PrevPosX = 0; PrevPosY = 0;
    // The current code keeps PrevPosX/Y for filtering continuity if a touch quickly reappears.
  }
}



void led_blinky_cb(void) {
  static uint32_t last_blink_ms = 0;
  static bool led_state = false;
  uint32_t blink_interval_ms = fs_mounted ? BLINK_MSC_MOUNTED : BLINK_MSC_NOT_MOUNTED;

  if (millis() - last_blink_ms < blink_interval_ms) return;
  last_blink_ms = millis();

  digitalWrite(LED_BUILTIN, led_state);
  led_state = !led_state;
}


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


void setup() {
  // Initialize serial port and wait for it to open before continuing.
  unsigned long startTime = millis();


  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  pinMode(8, OUTPUT);
  digitalWrite(8, HIGH);
  Serial.begin(115200);

  while (!Serial && (millis() - startTime < 5000)) {  // Wait for serial, with 5-sec timeout
    delay(10);
  }
  Serial.println("MPR121 Custom X/Y Position Test - v2 (Sensitivity & Mapping)");

  Wire.begin();  // Initialize I2C (use Wire.begin(SDA_PIN, SCL_PIN) if non-default)

  MPR121_init_arduino();
  Serial.println("MPR121 Initialized.");

  // Initialize previous states
  PreTouchStatus.Touched = false;
  PrevSumX = 0;
  PrevSX = 0;
  PrevSumY = 0;
  PrevSY = 0;
  PrevPosX = 0;
  PrevPosY = 0;
  PrevDX = 0;
  PrevDY = 0;



  Serial.println(F("Adafruit SPI Flash FatFs Format Example"));

  // Initialize flash library and check its chip ID.
  // if (!flash.begin()) {
  //   Serial.println(F("Error, failed to initialize flash chip!"));
  //   while(1) yield();
  // }

  flash.begin(&P25Q16H, 1);

  Serial.print(F("Flash chip JEDEC ID: 0x"));
  Serial.println(flash.getJEDECID(), HEX);
  Serial.print(F("Flash size: "));
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
    Serial.print(DX);
    Serial.print(',');
    Serial.println(DY);

    int reading = random(0, 100);
    // Write a line to the file.  You can use all the same print functions
    // as if you're writing to the serial monitor.  For example to write
    // two CSV (commas separated) values:
    dataFile.print("DX", DEC);
    dataFile.print(",");
    dataFile.print(DY, DEC);
    dataFile.println();
    // Finally close the file when done writing.  This is smart to do to make
    // sure all the data is written to the file.
    dataFile.close();
    Serial.print("Wrote new measurement to data file:");
    Serial.println(reading, DEC);
  } else {
    Serial.println("Failed to open data file for writing!");
  }

  Serial.println("Next measurement again in 6 seconds...");

  // Wait 60 seconds.
  Serial.flush();
  delay(6000L);



  if (Serial.available()) {
    // Serial.println(F("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"));
    // Serial.println(F("This sketch will ERASE ALL DATA on the flash chip and format it with a new filesystem!"));
    // Serial.println(F("Send OK (all caps) and press enter to continue."));
    // Serial.println(F("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"));
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

  // For debugging:
  Serial.print("MSC Read: LBA=");
  Serial.print(lba);
  Serial.print(", NBlk=");
  Serial.print(num_blocks);
  Serial.println(success ? " OK" : " Fail");

  return success ? bufsize : -1;
}

int32_t msc_write_cb(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
  // Assuming flash.begin() in setup succeeded.

  uint32_t num_blocks = bufsize / 512;
  // Adafruit_SPIFlash::writeBlocks expects number of 512-byte blocks
  bool success = flash.writeBlocks(lba, (uint8_t *)buffer, num_blocks);

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
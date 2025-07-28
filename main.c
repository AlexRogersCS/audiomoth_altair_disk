/****************************************************************************
 * main.c
 * openacousticdevices.info
 * March 2025
 *****************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "em_usb.h"

#include "i8080.h"
#include "audiomoth.h"
#include "usbserial.h"

#include "basicdisk24k50.h"

/* Sleep and LED constants */

#define DEFAULT_WAIT_INTERVAL                   200
#define LED_FLASH_THRESHOLD                     50000
#define SWITCH_CHANGE_THRESHOLD                 200000
#define LINE_PRINTER_THRESHOLD                  200000

/* USB CDC constants */

#define CDC_BULK_EP_SIZE                        USB_FS_BULK_EP_MAXSIZE
#define CDC_USB_RX_BUF_SIZ                      CDC_BULK_EP_SIZE

#define CDC_USB_BUF_SIZ                         128

/* Altair 8800 constants */

#define MEMORY_SIZE                             (64 * 1024)

#define SERIAL_BUFFER_SIZE                      1024

#define LINE_PRINTER_BUFFER_SIZE                1024

/* Disk controller status bits */

#define DISK_STATUS_WRITE_CIRCUIT_READY         0x01
#define DISK_STATUS_HEAD_MOVEMENT_ALLOWED       0x02
#define DISK_STATUS_HEAD_LOADED                 0x04
#define DISK_STATUS_NOT_USED_1                  0x08
#define DISK_STATUS_NOT_USED_2                  0x10
#define DISK_STATUS_INTERRUPTS_ENABLED          0x20
#define DISK_STATUS_HEAD_ON_TRACK_ZERO          0x40
#define DISK_STATUS_READ_CIRCUIT_READY          0x80

#define DISK_STATUS_INITIAL                     (DISK_STATUS_NOT_USED_1 | DISK_STATUS_NOT_USED_2 | DISK_STATUS_HEAD_MOVEMENT_ALLOWED)                     

/* Disk constants */

#define MAX_NUMBER_OF_DISKS                     16

#define DISK_SECTOR_SIZE                        137
#define DISK_NUMBER_OF_SECTORS                  32
#define DISK_TRACK_SIZE                         (DISK_SECTOR_SIZE * DISK_NUMBER_OF_SECTORS)
#define DISK_NUMBER_OF_TRACKS                   77
#define DISK_SIZE                               (DISK_NUMBER_OF_TRACKS * DISK_TRACK_SIZE)

#define FILE_NAME_BUFFER_LENGTH                 32

/* Altair 8800 state */

static struct i8080 cpu;

static volatile bool sendingToTeleprinter;

/* Disk state */

static uint8_t currentFlags;

static uint32_t currentDisk;
static uint32_t currentTrack;
static uint32_t currentSector;
static uint32_t currentByte;

static uint8_t sectorBuffer[DISK_SECTOR_SIZE];

static char filename[FILE_NAME_BUFFER_LENGTH];

static bool checkedExistence[MAX_NUMBER_OF_DISKS];

/* Serial buffer */

static volatile uint32_t serialBufferReadIndex;

static volatile uint32_t serialBufferWriteIndex;

static volatile char serialBuffer[SERIAL_BUFFER_SIZE];

/* Line printer buffer */

static volatile uint32_t linePrinterBufferWriteIndex;

static char linePrinterBuffer[LINE_PRINTER_BUFFER_SIZE];

/* USB CDC state */

STATIC_UBUF(usbRxBuffer, CDC_USB_BUF_SIZ);
STATIC_UBUF(usbTxBuffer, CDC_USB_BUF_SIZ);

/* USB CDC data structures */

SL_PACK_START(1)
typedef struct {
    uint32_t dwDTERate;               
    uint8_t  bCharFormat;             
    uint8_t  bParityType;             
    uint8_t  bDataBits;               
    uint8_t  dummy;                   
} SL_ATTRIBUTE_PACKED cdcLineCoding_TypeDef;
SL_PACK_END()

SL_ALIGN(4)
SL_PACK_START(1)
static cdcLineCoding_TypeDef SL_ATTRIBUTE_ALIGN(4) cdcLineCoding = {9600, 0, 0, 8, 0};
SL_PACK_END()

/* USB CDC line coding handler */

static int LineCodingReceived(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining) {

    if ((status == USB_STATUS_OK) && (xferred == 7)) return USB_STATUS_OK;

    return USB_STATUS_REQ_ERR;

}

/* USB CDC function prototypes */

static int UsbDataSent(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining);

static int UsbDataReceived(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining);

/* USB CDC functions */

static int setupCmd(const USB_Setup_TypeDef *setup) {

    int retVal = USB_STATUS_REQ_UNHANDLED;
   
    if ( ( setup->Type == USB_SETUP_TYPE_CLASS) && ( setup->Recipient == USB_SETUP_RECIPIENT_INTERFACE)) {

        switch (setup->bRequest) {

            case USB_CDC_GETLINECODING:

                if ((setup->wValue == 0) && (setup->wIndex == CDC_CTRL_INTERFACE_NO) && (setup->wLength == 7) && (setup->Direction == USB_SETUP_DIR_IN)) {
            
                    USBD_Write(0, (void*) &cdcLineCoding, 7, NULL);
    
                    retVal = USB_STATUS_OK;

                }

                break;
                
            case USB_CDC_SETLINECODING:

                if ((setup->wValue == 0) && (setup->wIndex == CDC_CTRL_INTERFACE_NO) && (setup->wLength == 7) && (setup->Direction != USB_SETUP_DIR_IN)) {

                    USBD_Read(0, (void*) &cdcLineCoding, 7, LineCodingReceived);
        
                    retVal = USB_STATUS_OK;
                
                }

                break;

            case USB_CDC_SETCTRLLINESTATE:

                if ((setup->wIndex == CDC_CTRL_INTERFACE_NO) && (setup->wLength == 0)) {

                    retVal = USB_STATUS_OK;

                }

                break;
                
        }
            
    }
 
    return retVal;

}

static void stateChange(USBD_State_TypeDef oldState, USBD_State_TypeDef newState) {

    if (newState == USBD_STATE_CONFIGURED) {

        USBD_Read(CDC_EP_DATA_OUT, (void*)usbRxBuffer, CDC_USB_RX_BUF_SIZ, UsbDataReceived);

    }

}

/* USB data sent and receive callbacks */

static int UsbDataSent(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining) {

    sendingToTeleprinter = false;

    return USB_STATUS_OK;

}

static int UsbDataReceived(USB_Status_TypeDef status, uint32_t xferred, uint32_t remaining) {
    
    if ((status == USB_STATUS_OK) && (xferred > 0)) {

        uint32_t index = 0;

        while (index < xferred) {

            serialBuffer[serialBufferWriteIndex] = usbRxBuffer[index];

            serialBufferWriteIndex = (serialBufferWriteIndex + 1) % SERIAL_BUFFER_SIZE;

            index += 1;

        }

        USBD_Read(CDC_EP_DATA_OUT, (void*)usbRxBuffer, CDC_USB_RX_BUF_SIZ, UsbDataReceived);

    }

    return USB_STATUS_OK;

}

/* Firmware version and description */

static uint8_t firmwareVersion[AM_FIRMWARE_VERSION_LENGTH] = {1, 0, 1};

static uint8_t firmwareDescription[AM_FIRMWARE_DESCRIPTION_LENGTH] = "AudioMoth-Altair-8800-Disk";

/* AudioMoth interrupt handlers */

inline void AudioMoth_handleSwitchInterrupt() { }

inline void AudioMoth_handleMicrophoneChangeInterrupt() { }

inline void AudioMoth_handleMicrophoneInterrupt(int16_t sample) { }

inline void AudioMoth_handleDirectMemoryAccessInterrupt(bool isPrimaryBuffer, int16_t **nextBuffer) { }

/* AudioMoth USB message handlers */

inline void AudioMoth_usbFirmwareVersionRequested(uint8_t **firmwareVersionPtr) {

    *firmwareVersionPtr = firmwareVersion;

}

inline void AudioMoth_usbFirmwareDescriptionRequested(uint8_t **firmwareDescriptionPtr) {

    *firmwareDescriptionPtr = firmwareDescription;

}

inline void AudioMoth_usbApplicationPacketRequested(uint32_t messageType, uint8_t *transmitBuffer, uint32_t size) { }

inline void AudioMoth_usbApplicationPacketReceived(uint32_t messageType, uint8_t* serialBuffer, uint8_t *transmitBuffer, uint32_t size) { }

/* AudioMoth time requests */

inline void AudioMoth_timezoneRequested(int8_t *timezoneHours, int8_t *timezoneMinutes) { }

/* Load and unload sector */

static void checkAndCreateDiskImage() {

    bool exists = AudioMoth_doesFileExist(filename);

    if (exists == false) {

        bool success = AudioMoth_openFile(filename);

        if (success == false) return;

        memset(sectorBuffer, 0, DISK_SECTOR_SIZE);

        for (uint32_t i = 0; i < DISK_NUMBER_OF_TRACKS; i += 1) {

            for (uint32_t j = 0; j < DISK_NUMBER_OF_SECTORS; j += 1) {

                AudioMoth_writeToFile(sectorBuffer, DISK_SECTOR_SIZE);

            }

        }

        checkedExistence[currentDisk] = true;

        AudioMoth_closeFile();

    }

}

static void loadSector() {

    AudioMoth_setRedLED(true);

    sprintf(filename, "DISK%02ld.DSK", currentDisk);

    if (checkedExistence[currentDisk] == false) checkAndCreateDiskImage();

    bool success = AudioMoth_openFileToRead(filename);

    if (success == false) return;

    AudioMoth_seekInFile(currentTrack * DISK_TRACK_SIZE + currentSector * DISK_SECTOR_SIZE);

    AudioMoth_readFile((char*)sectorBuffer, DISK_SECTOR_SIZE);

    AudioMoth_closeFile();

    AudioMoth_setRedLED(false);

}

static void unloadSector() {

    sprintf(filename, "DISK%02ld.DSK", currentDisk);
  
    if (checkedExistence[currentDisk] == false) checkAndCreateDiskImage();

    bool success = AudioMoth_openFileToEdit(filename);

    if (success == false) return;

    AudioMoth_setRedLED(true);

    AudioMoth_seekInFile(currentTrack * DISK_TRACK_SIZE + currentSector * DISK_SECTOR_SIZE);

    AudioMoth_writeToFile(sectorBuffer, DISK_SECTOR_SIZE);

    AudioMoth_closeFile();

    AudioMoth_setRedLED(false);

}

/* Intel 8080 IN and OUT handlers */

uint handle_input(struct i8080 *cpu, uint device) {

    if (device == 0x02) {
        
        return 0xFF;
        
    } else if (device == 0x10) {

        return (sendingToTeleprinter ? 0x00 : 0x02) | (serialBufferReadIndex == serialBufferWriteIndex ? 0x00 : 0x01);

    } else if (device == 0x11) {

        if (serialBufferReadIndex == serialBufferWriteIndex) return 0x00;

        uint8_t data = serialBuffer[serialBufferReadIndex];
        
        serialBufferReadIndex = (serialBufferReadIndex + 1) % SERIAL_BUFFER_SIZE;

        return data;

    } else if (device == 0x08) {

        return ~(currentFlags) & 0xFF;
        
    } else if (device == 0x09) {

        if (currentFlags & DISK_STATUS_HEAD_LOADED) {
        
            /* Head loaded */

            currentByte = 0;

            currentSector += 1;

            if (currentSector > DISK_NUMBER_OF_SECTORS - 1) currentSector = 0;

            return currentSector << 1;

        } else {

            /* Head not loaded */

            return 0x00;
        
        }

    } else if (device == 0x0A) {

        if (currentByte >= DISK_SECTOR_SIZE) return 0x00;

        if (currentByte == 0) loadSector();

        uint8_t data = sectorBuffer[currentByte];

        currentByte += 1;

        return data;

    }

    return 0x00;

}

void handle_output(struct i8080 *cpu, uint device, uint data) {

    static bool firstByte = false;
    static bool secondByte = false;

    if (device == 0x02) {

        if (data == 0x01 || data == 0x02) {

            linePrinterBuffer[linePrinterBufferWriteIndex++] = '\r';

            linePrinterBuffer[linePrinterBufferWriteIndex++] = '\n';

        }

    } else if (device == 0x03) {

        if (data == '\r' || data == '\n') {

            linePrinterBuffer[linePrinterBufferWriteIndex++] = '\r';

            linePrinterBuffer[linePrinterBufferWriteIndex++] = '\n';

        } else if (data != 0x11) {

            linePrinterBuffer[linePrinterBufferWriteIndex++] = data;

        }

    } else if (device == 0x08) {

        currentDisk = data & 0x0F;

        if (data & 0x80) {

            /* Disable drive */

            currentFlags = 0x00;

        } else {

            /* Enable drive */

            currentFlags = DISK_STATUS_INITIAL;
        
            if (currentTrack == 0) currentFlags |= DISK_STATUS_HEAD_ON_TRACK_ZERO;

        }

    } else if (device == 0x09) {

        if (data & 0x01) {

            currentTrack += 1;

            if (currentTrack > DISK_NUMBER_OF_TRACKS - 1) currentTrack = DISK_NUMBER_OF_TRACKS - 1;

            currentFlags &= ~DISK_STATUS_HEAD_ON_TRACK_ZERO;

        }

        if (data & 0x02) {

            if (currentTrack == 0) {

                currentFlags |= DISK_STATUS_HEAD_ON_TRACK_ZERO;

            } else {

                currentFlags &= ~DISK_STATUS_HEAD_ON_TRACK_ZERO;

                currentTrack -= 1;

            }

        }

        if (data & 0x04) {   

            /* Head load */

            currentFlags |= DISK_STATUS_HEAD_LOADED | DISK_STATUS_READ_CIRCUIT_READY;
        
        }

        if (data & 0x08) {

            /* Head unload */

            currentFlags &= ~(DISK_STATUS_HEAD_LOADED | DISK_STATUS_READ_CIRCUIT_READY);

        }

        if (data & 0x80) {

            /* Write sequence start */

            currentFlags |= DISK_STATUS_WRITE_CIRCUIT_READY;

        }

    } else if (device == 0x0A) {

        if (currentByte < DISK_SECTOR_SIZE) {

            sectorBuffer[currentByte] = data;

            currentByte += 1;

        }

        if (currentByte == DISK_SECTOR_SIZE) {

            currentFlags &= ~DISK_STATUS_WRITE_CIRCUIT_READY;
            
            unloadSector();

        }

    } else if (device == 0x11) {

        sendingToTeleprinter = true;

        usbTxBuffer[0] = data & 0x7F;

        USBD_Write(CDC_EP_DATA_IN, (void*)usbTxBuffer, 1, UsbDataSent);

    } else if (device == 0x31) {

        if (data == 0xE7 || data == 0xEF) {

            linePrinterBuffer[linePrinterBufferWriteIndex++] = ' ';

        } else if (data != 0xF3 && secondByte) {

            linePrinterBuffer[linePrinterBufferWriteIndex++] = ((~data) >> 1) & 0x7F;

        }

    } else if (device == 0x33) {

        secondByte = firstByte && data == 0xFF;

        firstByte = data == 0xBF;

        if (data == 0x7F) {

            linePrinterBuffer[linePrinterBufferWriteIndex++] = '\r';

            linePrinterBuffer[linePrinterBufferWriteIndex++] = '\n';

        }

    }

}

/* Clear terminal */

void clearTerminal() {

    AudioMoth_delay(DEFAULT_WAIT_INTERVAL);

    uint32_t length = sprintf((char*)usbTxBuffer, "\033[2J\033[H");

    USBD_Write(CDC_EP_DATA_IN, (void*)usbTxBuffer, length, UsbDataSent);

    AudioMoth_delay(DEFAULT_WAIT_INTERVAL);

}
/* Main function */

int main(void) {

    /* Initialise device */

    AudioMoth_initialise();

    /* Respond to switch state */

    AM_switchPosition_t switchPosition = AudioMoth_getSwitchPosition();

    if (switchPosition == AM_SWITCH_USB) {

        /* Use conventional USB routine */

        AudioMoth_handleUSB();

        /* Power down */

        AudioMoth_powerDownAndWakeMilliseconds(DEFAULT_WAIT_INTERVAL);

    }

    /* Enable the serial USB interface */

    USBD_Init(&initstruct);

    /* Clear terminal */

    clearTerminal();

    /* Enable SD card */

    bool success = AudioMoth_enableExternalSRAM();

    if (success == false) {

        while (switchPosition != AM_SWITCH_USB) {

            uint32_t length = sprintf((char*)usbTxBuffer, "Could not enable external SRAM on this device.\r\n");

            USBD_Write(CDC_EP_DATA_IN, (void*)usbTxBuffer, length, UsbDataSent);

            AudioMoth_setBothLED(true);

            AudioMoth_delay(500);

            AudioMoth_setBothLED(false);

            AudioMoth_delay(500);

            /* Feed watchdog */

            AudioMoth_feedWatchdog();

            /* Check for a switch change */

            switchPosition = AudioMoth_getSwitchPosition();

        }

        AudioMoth_powerDownAndWakeMilliseconds(DEFAULT_WAIT_INTERVAL);

    }

    /* Main loop */

    while (true) {

        /* Clear terminal */

        clearTerminal();

        /* Check file system */

        bool success = AudioMoth_enableFileSystem(AM_SD_CARD_NORMAL_SPEED);

        while (success == false && switchPosition != AM_SWITCH_USB) {

            uint32_t length = sprintf((char*)usbTxBuffer, "Could not enable SD card on this device.\r\n");

            USBD_Write(CDC_EP_DATA_IN, (void*)usbTxBuffer, length, UsbDataSent);

            AudioMoth_setBothLED(true);

            AudioMoth_delay(500);

            AudioMoth_setBothLED(false);

            AudioMoth_delay(500);

            /* Feed watchdog */

            AudioMoth_feedWatchdog();

            /* Check for a switch change */

            switchPosition = AudioMoth_getSwitchPosition();

            /* Recheck SD card */

            success = AudioMoth_enableFileSystem(AM_SD_CARD_NORMAL_SPEED);

            if (success) clearTerminal();

        }

        if (switchPosition == AM_SWITCH_USB) AudioMoth_powerDownAndWakeMilliseconds(DEFAULT_WAIT_INTERVAL);

        /* Initialise disks */

        currentFlags = 0x00;

        currentDisk = 0;
        currentTrack = 0;
        currentSector = 0;
        currentByte = 0;

        /* Reset disk lookup */

        memset(checkedExistence, 0, MAX_NUMBER_OF_DISKS);

        /* Reset Intel 8080 */

        i8080_reset(&cpu);

        cpu.memsize = MEMORY_SIZE;
        
        cpu.memory = (char*)AM_EXTERNAL_SRAM_START_ADDRESS;

        cpu.input_handler = handle_input;
        
        cpu.output_handler = handle_output;

        serialBufferReadIndex = 0;

        serialBufferWriteIndex = 0;

        sendingToTeleprinter = false;

        /* Clear the memory */

        memset(cpu.memory, 0, MEMORY_SIZE);

        /* Copy program to memory */

        memcpy(cpu.memory, basicdisk24k50, sizeof(basicdisk24k50));

        /* Main loop */

        bool ledState = false;

        uint32_t ledFlashCounter = 0;

        uint32_t switchChangeCounter = 0;

        uint32_t linePrinterCounter = 0;

        while (true) {

            /* Check switch positions */

            AM_switchPosition_t currentSwitchPosition = AudioMoth_getSwitchPosition();

            switchChangeCounter = currentSwitchPosition != switchPosition ? switchChangeCounter + 1 : 0;
            
            if (switchChangeCounter > SWITCH_CHANGE_THRESHOLD) {

                switchPosition = currentSwitchPosition;

                break;

            }

            /* Write lineprinter output */

            linePrinterCounter = linePrinterBufferWriteIndex > 0 ? linePrinterCounter + 1 : 0;

            if (linePrinterCounter > LINE_PRINTER_THRESHOLD) {

                AudioMoth_setRedLED(true);

                AudioMoth_appendFile("LINEPRINTER.TXT");

                AudioMoth_writeToFile(linePrinterBuffer, linePrinterBufferWriteIndex);

                AudioMoth_closeFile();

                AudioMoth_setRedLED(false);

                linePrinterBufferWriteIndex = 0;

                linePrinterCounter = 0;

            }

            /* Flash LED */

            ledFlashCounter += 1;

            if (ledFlashCounter > LED_FLASH_THRESHOLD) {

                ledState = !ledState;

                AudioMoth_setGreenLED(ledState);

                ledFlashCounter = 0;

            }

            /* Perform Intel 8080 step */

            i8080_step(&cpu);

            /* Feed watchdog */

            AudioMoth_feedWatchdog();

        }

        /* Turn off LED */

        AudioMoth_setBothLED(false);

        /* Exit if switch position is USB/OFF */

        if (switchPosition == AM_SWITCH_USB) break;

    }
    
    /* Power down */

    AudioMoth_powerDownAndWakeMilliseconds(DEFAULT_WAIT_INTERVAL);

}
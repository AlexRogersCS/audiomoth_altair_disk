/****************************************************************************
 * usbserial.h
 * openacousticdevices.info
 * May 2024
 *****************************************************************************/

#ifndef __USBSERIAL_H
#define __USBSERIAL_H

/* USB serial constants */

#define CDC_CTRL_INTERFACE_NO           0       
#define CDC_DATA_INTERFACE_NO           1

#define CDC_EP_DATA_OUT                 0x01
#define CDC_EP_DATA_IN                  0x81
#define CDC_EP_NOTIFY                   0x82

/* Configuration descriptor constants */

#define CONFIG_DESCSIZE (USB_CONFIG_DESCSIZE                     \
                         + (USB_INTERFACE_DESCSIZE * 2)          \
                         + (USB_ENDPOINT_DESCSIZE * 3) \
                         + USB_CDC_HEADER_FND_DESCSIZE           \
                         + USB_CDC_CALLMNG_FND_DESCSIZE          \
                         + USB_CDC_ACM_FND_DESCSIZE              \
                         + 5)

/* Device descriptor */

SL_ALIGN(4)
const USB_DeviceDescriptor_TypeDef deviceDesc SL_ATTRIBUTE_ALIGN(4) = {
    .bLength            = USB_DEVICE_DESCSIZE,
    .bDescriptorType    = USB_DEVICE_DESCRIPTOR,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = USB_CLASS_CDC,
    .bDeviceSubClass    = 0,
    .bDeviceProtocol    = 0,
    .bMaxPacketSize0    = USB_FS_CTRL_EP_MAXSIZE,
    .idVendor           = 0x10C4,
    .idProduct          = 0x0003,
    .bcdDevice          = 0x0000,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 0,
    .bNumConfigurations = 1
};

/* HID Report Descriptor */

SL_ALIGN(4)
static const char HID_ReportDescriptor[] __attribute__ ((aligned(4))) = {
    0x06, 0xAB, 0xFF,
    0x0A, 0x00, 0x02,
    0xA1, 0x01,                                 /* Collection 0x01                      */
    0x75, 0x08,                                 /* Report size = 8 bits                 */
    0x15, 0x00,                                 /* Logical minimum = 0                  */
    0x26, 0xFF, 0x00,                           /* logical maximum = 255                */
    0x95, 64,                                   /* Report count                         */
    0x09, 0x01,                                 /* Usage                                */
    0x81, 0x02,                                 /* Input (array)                        */
    0x95, 64,                                   /* Report count                         */
    0x09, 0x02,                                 /* Usage                                */
    0x91, 0x02,                                 /* Output (array)                       */
    0xC0                                        /* End collection                       */
};

/* HID Descriptor */

SL_ALIGN(4)
static const char HID_Descriptor[] __attribute__ ((aligned(4))) = {
    USB_HID_DESCSIZE,                           /* bLength                              */
    USB_HID_DESCRIPTOR,                         /* bDescriptorType                      */
    0x11,                                       /* bcdHID (LSB)                         */
    0x01,                                       /* bcdHID (MSB)                         */
    0x00,                                       /* bCountryCode                         */
    0x01,                                       /* bNumDescriptors                      */
    USB_HID_REPORT_DESCRIPTOR,                  /* bDecriptorType                       */
    sizeof(HID_ReportDescriptor),               /* wDescriptorLength(LSB)               */
    0x00                                        /* wDescriptorLength(MSB)               */
};

/* Configuration descriptor */

SL_ALIGN(4)
const uint8_t configDesc[] SL_ATTRIBUTE_ALIGN(4) = {

    /*** Configuration descriptor ***/

    USB_CONFIG_DESCSIZE,                          /* bLength                                   */
    USB_CONFIG_DESCRIPTOR,                        /* bDescriptorType                           */
    CONFIG_DESCSIZE,                              /* wTotalLength (LSB)                        */
    CONFIG_DESCSIZE >> 8,                         /* wTotalLength (MSB)                        */
    2,                                            /* bNumInterfaces                            */
    1,                                            /* bConfigurationValue                       */
    0,                                            /* iConfiguration                            */
    CONFIG_DESC_BM_RESERVED_D7                    /* bmAttrib: Self powered                    */
    | CONFIG_DESC_BM_SELFPOWERED,
    CONFIG_DESC_MAXPOWER_mA(100),                 /* bMaxPower: 100 mA                         */

    /*** Communication Class Interface descriptor (interface no. 0) ***/

    USB_INTERFACE_DESCSIZE,                       /* bLength                                   */
    USB_INTERFACE_DESCRIPTOR,                     /* bDescriptorType                           */
    0,                                            /* bInterfaceNumber                          */
    0,                                            /* bAlternateSetting                         */
    1,                                            /* bNumEndpoints                             */
    USB_CLASS_CDC,                                /* bInterfaceClass                           */
    USB_CLASS_CDC_ACM,                            /* bInterfaceSubClass                        */
    0,                                            /* bInterfaceProtocol                        */
    0,                                            /* iInterface                                */

    /*** CDC Header Functional descriptor ***/

    USB_CDC_HEADER_FND_DESCSIZE,                  /* bFunctionLength                           */
    USB_CS_INTERFACE_DESCRIPTOR,                  /* bDescriptorType                           */
    USB_CLASS_CDC_HFN,                            /* bDescriptorSubtype                        */
    0x20,                                         /* bcdCDC spec.no LSB                        */
    0x01,                                         /* bcdCDC spec.no MSB                        */

    /*** CDC Call Management Functional descriptor ***/

    USB_CDC_CALLMNG_FND_DESCSIZE,                 /* bFunctionLength                           */
    USB_CS_INTERFACE_DESCRIPTOR,                  /* bDescriptorType                           */
    USB_CLASS_CDC_CMNGFN,                         /* bDescriptorSubtype                        */
    0,                                            /* bmCapabilities                            */
    1,                                            /* bDataInterface                            */

    /*** CDC Abstract Control Management Functional descriptor ***/

    USB_CDC_ACM_FND_DESCSIZE,                     /* bFunctionLength                           */
    USB_CS_INTERFACE_DESCRIPTOR,                  /* bDescriptorType                           */
    USB_CLASS_CDC_ACMFN,                          /* bDescriptorSubtype                        */
    0x02,                                         /* bmCapabilities                            */

    /*** CDC Union Functional descriptor ***/

    5,                                            /* bFunctionLength                           */
    USB_CS_INTERFACE_DESCRIPTOR,                  /* bDescriptorType                           */
    USB_CLASS_CDC_UNIONFN,                        /* bDescriptorSubtype                        */
    0,                                            /* bControlInterface,      itf. no. 0        */
    1,                                            /* bSubordinateInterface0, itf. no. 1        */

    /*** CDC Notification endpoint descriptor ***/

    USB_ENDPOINT_DESCSIZE,                        /* bLength                                   */
    USB_ENDPOINT_DESCRIPTOR,                      /* bDescriptorType                           */
    CDC_EP_NOTIFY,                                /* bEndpointAddress (IN)                     */
    USB_EPTYPE_INTR,                              /* bmAttributes                              */
    USB_FS_INTR_EP_MAXSIZE,                       /* wMaxPacketSize (LSB)                      */
    0,                                            /* wMaxPacketSize (MSB)                      */
    0xFF,                                         /* bInterval                                 */

    /*** Data Class Interface descriptor (interface no. 1) ***/

    USB_INTERFACE_DESCSIZE,                       /* bLength                                   */
    USB_INTERFACE_DESCRIPTOR,                     /* bDescriptorType                           */
    1,                                            /* bInterfaceNumber                          */
    0,                                            /* bAlternateSetting                         */
    2,                                            /* bNumEndpoints                             */
    USB_CLASS_CDC_DATA,                           /* bInterfaceClass                           */
    0,                                            /* bInterfaceSubClass                        */
    0,                                            /* bInterfaceProtocol                        */
    0,                                            /* iInterface                                */

    /*** CDC Data interface endpoint descriptors ***/

    USB_ENDPOINT_DESCSIZE,                        /* bLength                                   */
    USB_ENDPOINT_DESCRIPTOR,                      /* bDescriptorType                           */
    CDC_EP_DATA_IN,                               /* bEndpointAddress (IN)                     */
    USB_EPTYPE_BULK,                              /* bmAttributes                              */
    USB_FS_BULK_EP_MAXSIZE,                       /* wMaxPacketSize (LSB)                      */
    0,                                            /* wMaxPacketSize (MSB)                      */
    0,                                            /* bInterval                                 */
    USB_ENDPOINT_DESCSIZE,                        /* bLength                                   */
    USB_ENDPOINT_DESCRIPTOR,                      /* bDescriptorType                           */
    CDC_EP_DATA_OUT,                              /* bEndpointAddress (OUT)                    */
    USB_EPTYPE_BULK,                              /* bmAttributes                              */
    USB_FS_BULK_EP_MAXSIZE,                       /* wMaxPacketSize (LSB)                      */
    0,                                            /* wMaxPacketSize (MSB)                      */
    0                                             /* bInterval                                 */
};


/* String descriptors */

STATIC_CONST_STRING_DESC_LANGID(langID, 0x04, 0x09);

STATIC_CONST_STRING_DESC(iManufacturer, 'o', 'p', 'e', 'n', 'a', 'c', 'o', 'u', 's', 't', 'i', 'c', 'd', 'e', 'v', 'i', 'c', 'e', 's', '.', 'i', 'n', 'f', 'o');

STATIC_CONST_STRING_DESC(iProduct, 'A', 'u', 'd', 'i', 'o', 'M', 'o', 't', 'h', ' ', 'A', 'l', 't', 'a', 'i', 'r', ' ', '8', '8', '0', '0');

STATIC_CONST_STRING_DESC(iSerialNumber, '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0', '0');

/* End-point buffer sizes */

static const uint8_t bufferingMultiplier[NUM_EP_USED + 1] = {
    1,  /* Control */
    1,  /* Isochronous */
    2,  /* Interrupt */
    2,  /* Interrupt */
    0   /* Unused */
};

/* String array */

static const void* strings[] = {
    &langID,
    &iManufacturer,
    &iProduct,
    &iSerialNumber
};

/* USB callbacks */

static int setupCmd(const USB_Setup_TypeDef *setup);

static void stateChange(USBD_State_TypeDef oldState, USBD_State_TypeDef newState);

static const USBD_Callbacks_TypeDef callbacks = {
    .usbReset        = NULL,
    .usbStateChange  = stateChange,
    .setupCmd        = setupCmd,
    .isSelfPowered   = NULL,
    .sofInt          = NULL
};

/* Initialisation data structure */

static const USBD_Init_TypeDef initstruct = {
    .deviceDescriptor    = &deviceDesc,
    .configDescriptor    = configDesc,
    .stringDescriptors   = strings,
    .numberOfStrings     = sizeof(strings)/sizeof(void*),
    .callbacks           = &callbacks,
    .bufferingMultiplier = bufferingMultiplier,
    .reserved            = 0
};

#endif /* __USBSERIAL_H */



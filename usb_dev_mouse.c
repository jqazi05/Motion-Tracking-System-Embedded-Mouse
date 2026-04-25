#include <stdbool.h>
#include <stdint.h>
#include <inc/hw_memmap.h>
#include <inc/hw_types.h>
#include <inc/hw_gpio.h>
#include <inc/hw_sysctl.h>
#include <driverlib/debug.h>
#include <driverlib/gpio.h>
#include <driverlib/interrupt.h>
#include <driverlib/pin_map.h>
#include <driverlib/rom.h>
#include <driverlib/sysctl.h>
#include <driverlib/systick.h>
#include <driverlib/uart.h>
#include <driverlib/ssi.h>
#include <usblib/usblib.h>
#include <usblib/usbhid.h>
#include <usblib/device/usbdevice.h>
#include <usblib/device/usbdhid.h>
#include <usblib/device/usbdhidmouse.h>

#include "usb_mouse_structs.h"
#include "buttons.h"
#include "utils/uartstdio.h"

// LIS3DH Register - Retrieved from datasheet
#define LIS3DH_WHO_AM_I     0x0F  // Detects if device is hooked up right, 0x33 = Detected
#define LIS3DH_CTRL_REG1    0x20
#define LIS3DH_CTRL_REG4    0x23 
#define LIS3DH_STATUS_REG   0x27
#define LIS3DH_OUT_X_L      0x28
#define LIS3DH_OUT_X_H      0x29
#define LIS3DH_OUT_Y_L      0x2A
#define LIS3DH_OUT_Y_H      0x2B

#define SPI_READ_BIT        0x80
#define SPI_MULTI_BIT       0x40

#define CS_PORT             GPIO_PORTA_BASE
#define CS_PIN              GPIO_PIN_3

// Chip select low and high 
#define CS_LOW()   GPIOPinWrite(CS_PORT, CS_PIN, 0)  
#define CS_HIGH()  GPIOPinWrite(CS_PORT, CS_PIN, CS_PIN)

volatile enum {
    eStateNotConfigured,
    eStateIdle,
    eStateSuspend,
    eStateSending
} g_iMouseState;


// USB State Monitor
uint32_t HIDMouseHandler(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgData, void *pvMsgData)
{
    switch (ui32Event)
    {
        case USB_EVENT_CONNECTED:
            g_iMouseState = eStateIdle;
            UARTprintf("\nHost Connected...\n");
            break;
        case USB_EVENT_DISCONNECTED:
            g_iMouseState = eStateNotConfigured;
            UARTprintf("\nHost Disconnected...\n");
            break;
        case USB_EVENT_TX_COMPLETE:
            g_iMouseState = eStateIdle;
            break;
        case USB_EVENT_SUSPEND:
            g_iMouseState = eStateSuspend;
            UARTprintf("\nBus Suspended\n");
            break;
        case USB_EVENT_RESUME:
            g_iMouseState = eStateIdle;
            UARTprintf("\nBus Resume\n");
            break;
    }
    return (0);
}
void SysTickIntHandler(void){}
void ConfigureUART(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    UARTClockSourceSet(UART0_BASE, UART_CLOCK_PIOSC);
    UARTStdioConfig(0, 115200, 16000000);
}

void ConfigureSPI(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);  // enables SSI0 for SPI
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA); // enables GPIOA

    // PA2=CLK, PA4=MISO, PA5=MOSI only, PA3 = CS, CS is manually controlled with high and low
    GPIOPinConfigure(GPIO_PA2_SSI0CLK); // CLK
    GPIOPinConfigure(GPIO_PA4_SSI0RX);  // MISO
    GPIOPinConfigure(GPIO_PA5_SSI0TX);  // MOSI
    GPIOPinTypeSSI(GPIO_PORTA_BASE, GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_2); // configures for SSI use

    GPIOPinTypeGPIOOutput(CS_PORT, CS_PIN); // Set the CS Pin
    CS_HIGH(); // idle high

    //SSI_FRF_MOTO_MODE_3 captures on rising edge (CPOL=1, CPHA=1)
    SSIConfigSetExpClk(SSI0_BASE, SysCtlClockGet(), SSI_FRF_MOTO_MODE_3, SSI_MODE_MASTER, 1000000, 8);
    SSIEnable(SSI0_BASE);
}

// Low SPI byte transfer with manual CS
static uint8_t SPI_Transfer(uint8_t data)
{
    uint32_t received;
    SSIDataPut(SSI0_BASE, data);
    while(SSIBusy(SSI0_BASE));
    SSIDataGet(SSI0_BASE, &received);
    return (uint8_t)received;
}

// Register Write
static void SPI_WriteReg(uint8_t reg, uint8_t value) {
    CS_LOW();
    SPI_Transfer(reg & ~SPI_READ_BIT); // clear read bit = write, Tells which register is going to be interacted with
    SPI_Transfer(value); // write value to register
    CS_HIGH();
}


// Register Read
static uint8_t SPI_ReadReg(uint8_t reg) {
    uint8_t value;
    CS_LOW();
    SPI_Transfer(reg | SPI_READ_BIT); // Tells which register is going to be interacted with
    value = SPI_Transfer(0x00);
    CS_HIGH();
    return value;
}


// Read 2 Register  
static int16_t SPI_ReadAxis(uint8_t reg_l)
{
    uint8_t low, high;
    CS_LOW();
    SPI_Transfer(reg_l | SPI_READ_BIT | SPI_MULTI_BIT); // Loading two register,
    low  = SPI_Transfer(0x00);
    high = SPI_Transfer(0x00);
    CS_HIGH();
    return (int16_t)((high << 8) | low);
}

// LIS3DH Init 
void LIS3DH_Init(void)
{
    uint8_t whoami;

	SysCtlDelay(SysCtlClockGet() / 100);
		
		// Detects whether or not LIS3DH is detected 
    whoami = SPI_ReadReg(LIS3DH_WHO_AM_I);
    UARTprintf("WHO_AM_I = 0x%02X (expecting 0x33)\n", whoami);

    if (whoami != 0x33) { // print error, blink red LED
        UARTprintf("ERROR: LIS3DH not detected! Check wiring and SPI mode.\n");

        while(1) {
            GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);
            SysCtlDelay(SysCtlClockGet() / 10);
            GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0);
            SysCtlDelay(SysCtlClockGet() / 10);
        }
    }

    // Set CTRL_REG1 on 400Hz ODR, normal mode, and enable all 3 axes (x,y,z)
    SPI_WriteReg(LIS3DH_CTRL_REG1, 0x77);

    // Set CTRL_REG4 on high-resolution mode and ±2g range
    SPI_WriteReg(LIS3DH_CTRL_REG4, 0x08);

    SysCtlDelay(SysCtlClockGet() / 100);
    UARTprintf("LIS3DH Init OK\n");
}

// Axis Reader
void ReadSensorMovement(int8_t *deltaX, int8_t *deltaY)
{
    int16_t raw_x, raw_y;
		uint8_t status = SPI_ReadReg(LIS3DH_STATUS_REG);
	
		// if data isn't new (mouse doesn't move), send 0 to say no new action was made
    if (!(status & 0x08)) { //
        *deltaX = 0;
        *deltaY = 0;
        return;
    }

    raw_x = SPI_ReadAxis(LIS3DH_OUT_X_L);
    raw_y = SPI_ReadAxis(LIS3DH_OUT_Y_L);

    // Make variables int8 since USBDHIDMouseStateChange doesn't accept int16
    *deltaX = (int8_t)(raw_x >> 8);
    *deltaY = (int8_t)(raw_y >> 8);
}

// Main loop
int main(void)
{
		// General Initalization
    uint8_t ui8ButtonsChanged, ui8Buttons;
    uint8_t ui8ReportButtons = 0;
    int8_t deltaX = 0;
    int8_t deltaY = 0;
		
    SysCtlClockSet(SYSCTL_SYSDIV_4 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
		// Port Enable
	  SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD); // for USB
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
		
		// Configuring Sensors and Serial Comm protocls
    ConfigureUART();
    ConfigureSPI();
    LIS3DH_Init(); 

    UARTprintf("\033[2JTiva C Series USB mouse device example\n");
    UARTprintf("---------------------------------\n\n");
		
		// USB Configuration
    g_iMouseState = eStateNotConfigured;
		
    SysCtlGPIOAHBEnable(SYSCTL_PERIPH_GPIOD);
    GPIOPinTypeUSBAnalog(GPIO_PORTD_AHB_BASE, GPIO_PIN_4 | GPIO_PIN_5);

    ButtonsInit();

    UARTprintf("Configuring USB\n");
    USBStackModeSet(0, eUSBModeForceDevice, 0);
    USBDHIDMouseInit(0, &g_sMouseDevice);
    UARTprintf("\nWaiting For Host...\n");

    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2 | GPIO_PIN_3, 0);
		
		// Mouse's main control
    while (1)
    {
        if (g_iMouseState == eStateIdle) {
            bool bUpdateRequired = false;

            ButtonsPoll(&ui8ButtonsChanged, &ui8Buttons);
            if (ui8ButtonsChanged) {
                ui8ReportButtons = 0;
							
								// Determine if button has been pressed or not
                if (ui8Buttons & LEFT_BUTTON) {
									ui8ReportButtons |= MOUSE_REPORT_BUTTON_2;
								}									
                if (ui8Buttons & RIGHT_BUTTON) {
									ui8ReportButtons |= MOUSE_REPORT_BUTTON_1;
								}	
								
								// turn on/off LED based when button pressed/not pressed
								if (ui8ReportButtons) {
									GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_3 , GPIO_PIN_3);
								} else {
										GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_3 | GPIO_PIN_2 , 0);
								}			
                bUpdateRequired = true;
            }
						// reads sensor movement and prints update if it does move
            ReadSensorMovement(&deltaX, &deltaY);
            if (deltaX != 0 || deltaY != 0) {
                UARTprintf("Moved: X=%d Y=%d\n", deltaX, deltaY);
                bUpdateRequired = true;
            }
						// updates mouse position and clicks
            if (bUpdateRequired) {
                USBDHIDMouseStateChange((void*)&g_sMouseDevice, -deltaX, deltaY, ui8ReportButtons);
            }
        }

        SysCtlDelay(SysCtlClockGet() / 300);
    }
}
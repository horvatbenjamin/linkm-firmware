#include <avr/io.h>
#include <avr/wdt.h>        // watchdog
#include <avr/interrupt.h>  // for sei() 
#include <util/delay.h>     // for _delay_ms() 
#include <string.h>

#include <avr/pgmspace.h>   // required by usbdrv.h 
#include "usbdrv.h"

#include "i2cmaster.h"

#include "linkm-lib.h"

// these aren't used anywhere, just here to note them
#define PIN_LED_STATUS         PORTB4
#define PIN_I2C_ENABLE         PORTB5

#define PIN_I2C_SDA            PORTC4
#define PIN_I2C_SCL            PORTC5

#define PIN_USB_DPLUS          PORTD2
#define PIN_USB_DMINUS         PORTD3

#define LINKM_VERSION_MAJOR    0x13
#define LINKM_VERSION_MINOR    0x36   // not quite leet yet


#define I2C_ADXL345 83
#define I2C_HMC5843 30
#define I2C_PSITG3200 104

/* ------------------------------------------------------------------------- */
/* ----------------------------- USB interface ----------------------------- */
/* ------------------------------------------------------------------------- */

PROGMEM char usbHidReportDescriptor[22] = {    /* USB report descriptor */
    0x06, 0x00, 0xff,              // USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    // USAGE (Vendor Usage 1)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,              //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,                    //   REPORT_SIZE (8)
    0x95, 0x01,                    //   REPORT_COUNT (1)
    0x09, 0x00,                    //   USAGE (Undefined)
    0xb2, 0x02, 0x01,              //   FEATURE (Data,Var,Abs,Buf)
    0xc0                           // END_COLLECTION
};
/* The descriptor above is a dummy only, it silences the drivers. The report
 * it describes consists of one byte of undefined data.
 * We don't transfer our data through HID reports, we use custom requests
 * instead.
 */

//static uint8_t inmsgbuf[REPORT1_COUNT];
static uint8_t outmsgbuf[24];

static uint8_t goReset = 0;   // set to 1 to reset 

// magic value read on reset: 0x55 = run bootloader , other = boot normally

void(* softReset) (void) = 0;  //declare reset function @ address 0

static void (*nullVector)(void) __attribute__((__noreturn__));

static void resetChip()
{
    cli();
    USB_INTR_ENABLE = 0;
    USB_INTR_CFG = 0;       /* also reset config bits */
    nullVector();
}

// ------------------------------------------------------------------------- 
void statusLedToggle(void)
{
    PORTB ^= (1<< PIN_LED_STATUS);  // toggles LED
}
void statusLedSet(int v)
{
    if( v ) PORTB  |=  (1<<PIN_LED_STATUS);
    else    PORTB  &=~ (1<<PIN_LED_STATUS);
}
uint8_t statusLedGet(void)
{
    return (PINB & (1<<PIN_LED_STATUS)) ? 1 : 0;
}

void i2cEnable(int v) {
    if( v ) PORTB  |=  (1<<PIN_I2C_ENABLE);
    else    PORTB  &=~ (1<<PIN_I2C_ENABLE);
}

// I2C_Requests //

unsigned char i2c_trans(unsigned char addr,unsigned char* inbuf, unsigned char* outbuf, uint8_t inlen, uint8_t outlen){
//	    if( i2c_start_wait( (addr<<1) | I2C_WRITE ) == 1) {  // start i2c trans
//            i2c_stop();
//            return 0;
//        }
i2c_start_wait( (addr<<1) | I2C_WRITE );
        // start succeeded, so send data
/*		unsigned char* inbuf_end = inbuf+inlen;
		while(inbuf!=inbuf_end){
			i2c_write(*inbuf);
			inbuf++;
		};
*/
		for(uint8_t i=0;i<inlen;i++){
			i2c_write(inbuf[i]);
		};
        if( outlen != 0 ) {
            if( i2c_rep_start( (addr<<1) | I2C_READ ) == 1 ) { // start i2c
				return 0;
			}
            else {
                for( uint8_t i=0; i<outlen; i++) {
                    int c = i2c_read( (i!=(outlen-1)) ); // read from i2c
                    if( c == -1 ) {  // timeout, get outx
                        return 0;
                    }
                    outbuf[i] = c;             // store in response buff
                }
            }
        }
        i2c_stop();  // done
		return 1;
}

void i2c_init_sensors(){
	unsigned char tmp[2];
	tmp[0]=0x2D;
	tmp[1]=8;
	i2c_trans(I2C_ADXL345,tmp,0,2,0);

	tmp[0]=0x0;
	tmp[1]=0b00011000;
	i2c_trans(I2C_HMC5843,tmp,0,2,0);

	tmp[0]=0x2;
	tmp[1]=0;
	i2c_trans(I2C_HMC5843,tmp,0,2,0);

	tmp[0]=0x16;
	tmp[1]=0b00011000;
	i2c_trans(I2C_PSITG3200,tmp,0,2,0);

};

void i2c_read_sensors(){
	unsigned char tmp[1];

	tmp[0]=0x32;
	i2c_trans(I2C_ADXL345,tmp,outmsgbuf,1,6);

	tmp[0]=0x03;
	i2c_trans(I2C_ADXL345,tmp,outmsgbuf+6,1,6);

	tmp[0]=0x1D;
	i2c_trans(I2C_ADXL345,tmp,outmsgbuf+12,1,6);

};

// -------------------------------------------------------------------------

// ------------------------------------------------------------------------- 
/**
 *
 */
usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t    *rq = (void *)data;
    // HID class request 
    if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_VENDOR) {
        if(rq->bRequest == 111) { 
			// Do I2C_Requests
			i2c_read_sensors();
			//
			usbMsgPtr = outmsgbuf;
            return sizeof(outmsgbuf);  // use usbFunctionRead() to obtain data 
        } else {
			// ignore vendor type requests, we don't use any 
		}
    }
    return 0;
}

// ------------------------------------------------------------------------- 

int main(void)
{
    uchar   i,j;
    
    wdt_enable(WDTO_1S);
    // Even if you don't use the watchdog, turn it off here. On newer devices,
    // the status of the watchdog (on/off, period) is PRESERVED OVER RESET!
    //DBG1(0x00, 0, 0);       // debug output: main starts
    // RESET status: all port bits are inputs without pull-up.
    // That's the way we need D+ and D-. Therefore we don't need any
    // additional hardware initialization. (for USB)
    
    // make pins outputs 
    DDRB |= (1<< PIN_LED_STATUS) | (1<< PIN_I2C_ENABLE); 
    // enable pullups on SDA & SCL
    PORTC |= _BV(PIN_I2C_SDA) | _BV(PIN_I2C_SCL);

    statusLedSet( 0 );      // turn off LED to start

    i2c_init();             // init I2C interface
    i2cEnable(1);           // enable i2c buffer chip

    for( j=0; j<4; j++ ) {
        statusLedToggle();  // then toggle LED
        wdt_reset();
        for( i=0; i<2; i++) { // wait for power to stabilize & blink status
            _delay_ms(10);
        }
    }

    usbInit();
    usbDeviceDisconnect();  
    for( i=0; i<2; i++ ) {
        statusLedSet( 1 );
        _delay_ms(50);
        statusLedSet( 0 );      // turn off LED to start
        _delay_ms(50);
    }
	i2c_init_sensors();
    usbDeviceConnect();
    sei();

	
    
    for(;;) {  // main event loop 
        wdt_reset();
        usbPoll();
    }

    // this is never executed
    resetChip();
    return 0;
}


// ------------------------------------------------------------------------- 

/**
 * Originally from:
 * Name: main.c
 * Project: hid-data, example how to use HID for data transfer
 * Author: Christian Starkjohann
 * Creation Date: 2008-04-11
 * Tabsize: 4
 * Copyright: (c) 2008 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: GNU GPL v2 (see License.txt), GNU GPL v3 or proprietary (CommercialLicense.txt)
 * This Revision: $Id: main.c 692 2008-11-07 15:07:40Z cs $
 */

/*
This example should run on most AVRs with only little changes. No special
hardware resources except INT0 are used. You may have to change usbconfig.h for
different I/O pins for USB. Please note that USB D+ must be the INT0 pin, or
at least be connected to INT0 as well.
*/

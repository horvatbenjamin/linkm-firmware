#include <avr/io.h>
#include <avr/wdt.h>        // watchdog
#include <avr/eeprom.h>     //
#include <avr/interrupt.h>  // for sei() 
#include <util/delay.h>     // for _delay_ms() 
#include <string.h>

#include <avr/pgmspace.h>   // required by usbdrv.h 
#include "usbdrv.h"
// #include "oddebug.h"        // This is also an example for using debug macros 

#include "i2cmaster.h"
// #include "uart.h"

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

// #define REPORT1_COUNT 16
// #define REPORT2_COUNT 131
/*
PROGMEM char usbHidReportDescriptor[33] = {
    0x06, 0x00, 0xff,              // USAGE_PAGE (Generic Desktop)
    0x09, 0x01,                    // USAGE (Vendor Usage 1)
    0xa1, 0x01,                    // COLLECTION (Application)
    0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,              //   LOGICAL_MAXIMUM (255)
    0x75, 0x08,                    //   REPORT_SIZE (8)

    0x85, 0x01,                    //   REPORT_ID (1)
    0x95, REPORT1_COUNT,           //   REPORT_COUNT (was 6)
    0x09, 0x00,                    //   USAGE (Undefined)
    0xb2, 0x02, 0x01,              //   FEATURE (Data,Var,Abs,Buf)

    0x85, 0x02,                    //   REPORT_ID (2)
    0x95, REPORT2_COUNT,           //   REPORT_COUNT (131)
    0x09, 0x00,                    //   USAGE (Undefined)
    0xb2, 0x02, 0x01,              //   FEATURE (Data,Var,Abs,Buf)
    0xc0                           // END_COLLECTION
};
*/

/* Since we define only one feature report, we don't use report-IDs (which
 * would be the first byte of the report). The entire report consists of
 * REPORT_COUNT opaque data bytes.
 */

/* The following variables store the status of the current data transfer */
/*static uchar    currentAddress;
static uchar    bytesRemaining;
*/
//static int numWrites;  // FIXME: what was this for?

//static uint8_t inmsgbuf[REPORT1_COUNT];
static uint8_t outmsgbuf[19];
// static uint8_t reportId;  // which report Id we're currently working on

//static volatile uint16_t tick;         // tick tock clock
//static volatile uint16_t timertick;         // tick tock clock

static uint8_t goReset = 0;   // set to 1 to reset 

// magic value read on reset: 0x55 = run bootloader , other = boot normally
#define GOBOOTLOAD_MAGICVAL 0x55
uint8_t bootmode EEMEM = 0; 

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

// 
// Called from usbFunctionWrite() when we've received the entire USB message
// 
/*
void handleMessage(void)
{
//    statusLedSet(1);
//    uint8_t ledval = 0;

    //outmsgbuf[0]++;                   // say we've handled a msg
    outmsgbuf[0] = reportId;          // reportID   FIXME: Hack
    outmsgbuf[1] = LINKM_ERR_NONE;    // be optimistic
    // outmsgbuf[2] starts the actual received data

    uint8_t* inmbufp = inmsgbuf+1;  // was +1 because had forgot to send repotid

    if( inmbufp[0] != START_BYTE  ) {   // byte 0: start marker
        outmsgbuf[1] = LINKM_ERR_BADSTART;
        goto doneHandleMessage; //return;
    }
    
    uint8_t cmd      = inmbufp[1];     // byte 1: command
    uint8_t num_sent = inmbufp[2];     // byte 2: number of bytes sent
    uint8_t num_recv = inmbufp[3];     // byte 3: number of bytes to return back

    // i2c transaction
    // params:
    //   mpbufp[4] == i2c addr
    //   mpbufp[5..5+numsend] == data to write
    // returns:
    //   outmsgbuf[0] == transaction counter 
    //   outmsgbuf[1] == response code
    //   outmsgbuf[2] == i2c response byte 0  (if any)
    //   outmsgbuf[3] == i2c response byte 1  (if any)
    // ...
    // FIXME: because "num_sent" and "num_recv" are outside this command
    //        it's confusing
    if( cmd == LINKM_CMD_I2CTRANS ) {
        uint8_t addr      = inmbufp[4];  // byte 4: i2c addr or command

//        putchdebug('A');
        if( addr >= 0x80 ) {   // invalid valid I2C address
            outmsgbuf[1] = LINKM_ERR_BADARGS;
            goto doneHandleMessage; //return;
        }

//        putchdebug('B');
        if( i2c_start( (addr<<1) | I2C_WRITE ) == 1) {  // start i2c trans
//            printdebug("!");
            outmsgbuf[1] = LINKM_ERR_I2C;
            i2c_stop();
            goto doneHandleMessage; //return;
        }
//        putchdebug('C');
        // start succeeded, so send data
		unsigned char* buf_ptr = &inmbufp[5];
		unsigned char* buf_ptr_end = &inmbufp[5+num_sent-1];
		while(buf_ptr!=buf_ptr_end){
			i2c_write(*buf_ptr);
			buf_ptr++;
		};
//        for( uint8_t i=0; i<num_sent-1; i++) {
//            i2c_write( inmbufp[5+i] );   // byte 5-N: i2c command to send
//        }

//        putchdebug('D');
        if( num_recv != 0 ) {
            if( i2c_rep_start( (addr<<1) | I2C_READ ) == 1 ) { // start i2c
                outmsgbuf[1] = LINKM_ERR_I2CREAD;
            }
            else {
                for( uint8_t i=0; i<num_recv; i++) {
                    //uint8_t c = i2c_read( (i!=(num_recv-1)) );//read from i2c
                    int c = i2c_read( (i!=(num_recv-1)) ); // read from i2c
                    if( c == -1 ) {  // timeout, get outx
                        outmsgbuf[1] = LINKM_ERR_I2CREAD;
                        break;
                    }
					//FIXME: need some review
                    outmsgbuf[2+i] = c;             // store in response buff
                }
            }
			return;
        }
//        putchdebug('Z');
        i2c_stop();  // done!
    }
    // i2c bus scan
    // params:
    //   mbufp[4]     == start addr
    //   mbufp[5]     == end addr
    // returns:
    //   outmsgbuf[0] == transaction counter
    //   outmsgbuf[1] == response code
    //   outmsgbuf[2] == number of devices found
    //   outmsgbuf[3] == addr of 1st device
    //   outmsgbuf[4] == addr of 2nd device
    // ...
    else if( cmd == LINKM_CMD_I2CSCAN ) {
        uint8_t addr_start = inmbufp[4];  // byte 4: start addr of scan
        uint8_t addr_end   = inmbufp[5];  // byte 5: end addr of scan
        if( addr_start >= 0x80 || addr_end >= 0x80 || addr_start > addr_end ) {
            outmsgbuf[1] = LINKM_ERR_BADARGS;
            goto doneHandleMessage; //return;
        }
        int numfound = 0;
        for( uint8_t a = 0; a < (addr_end-addr_start); a++ ) {
            if( i2c_start( ((addr_start+a)<<1)|I2C_WRITE)==0 ) { // dev found
                outmsgbuf[3+numfound] = addr_start+a;  // save the address 
                numfound++;
            }
            i2c_stop();
        }
        outmsgbuf[2] = numfound;
    }
    // i2c bus connect/disconnect
    // params:
    //   mpbuf[4]  == connect (1) or disconnect (0)
    // returns:
    //   outmsgbuf[0] == transaction counter
    //   outmsgbuf[1] == response code
    else if( cmd == LINKM_CMD_I2CCONN  ) {
        uint8_t conn = inmbufp[4];        // byte 4: connect/disconnect boolean
        i2cEnable( conn );
    }
    // i2c init
    // params:
    //   none
    // returns:
    //   outmsgbuf[0] == transaction counter
    //   outmsgbuf[1] == response code
    else if( cmd == LINKM_CMD_I2CINIT ) {  // FIXME: what's the real soln here?
        i2c_stop();
        _delay_ms(1);
        i2c_init();
    }

    // get linkm version
    // params:
    //   none
    // returns:
    //   outmsgbuf[0] == transaction counter
    //   outmsgbuf[1] == response code
    //   outmsgbuf[2] == major linkm version
    //   outmsgbuf[3] == minor linkm version
    else if( cmd == LINKM_CMD_VERSIONGET ) {
        outmsgbuf[2] = LINKM_VERSION_MAJOR;
        outmsgbuf[3] = LINKM_VERSION_MINOR;
    }
    // reset into bootloader
    else if( cmd == LINKM_CMD_GOBOOTLOAD ) { 
        statusLedToggle();
        eeprom_write_byte( &bootmode, GOBOOTLOAD_MAGICVAL );
        goReset = 1;
    }
    // cmd xxxx == 

 doneHandleMessage:
	5; //bullshit.. nothing
    //statusLedSet(ledval);

}

*/

/* usbFunctionWrite() is called when the host sends a chunk of data to the
 * device. For more information see the documentation in usbdrv/usbdrv.h.
 */
/*
uchar   usbFunctionWrite(uchar *data, uchar len)
{
    if(bytesRemaining == 0)
        return 1;               // end of transfer 
    if(len > bytesRemaining)
        len = bytesRemaining;
    
    memcpy( inmsgbuf+currentAddress, data, len );
    currentAddress += len;
    bytesRemaining -= len;
    
    if( bytesRemaining == 0 )  {   // got it all
        handleMessage();
    }
    
    return bytesRemaining == 0; // return 1 if this was the last chunk 
}
*/

/* usbFunctionRead() is called when the host requests a chunk of data from
 * the device. For more information see the docs in usbdrv/usbdrv.h.
 */
/*
uchar   usbFunctionRead(uchar *data, uchar len)
{
    if(len > bytesRemaining)
        len = bytesRemaining;
    
    memcpy( data, outmsgbuf + currentAddress, len);
    //numWrites = 0;
    currentAddress += len;
    bytesRemaining -= len;
    return len;
}
*/
// ------------------------------------------------------------------------- 
/**
 *
 */

unsigned char read_ADXL345(unsigned char* data_ptr){
	 if( i2c_start( ( I2C_ADXL345<<1) | I2C_WRITE ) == 1) {
		 i2c_stop();
		 return 255;
	 }else{
		 i2c_write(0x32);
		 //i2c_stop(); // megnezni, hogy a stop kiuriti-e a buffert!
	 };
	 if( i2c_rep_start( (I2C_ADXL345<<1) | I2C_READ ) == 1 ) { // start i2c
		 return 255;
	 }else{
		 int x=0;
		 for(x=0;x<6;++x){
			data_ptr[x]=i2c_read(x!=5);
		 };
	 };
	
};

void read_devices(){
	unsigned char* data_ptr=outmsgbuf+1;
	outmsgbuf[0]=0;
	read_ADXL345(data_ptr);
/*	data_ptr+=6;
	read_HMC5843(data_ptr);
	data_ptr+=6;
	read_PSITG3200(data_ptr);
*/
};

unsigned char init_i2c_sensors(){
	//init ADXL345
	 if( i2c_rep_start( ( I2C_ADXL345<<1) | I2C_WRITE ) == 1) {
		 i2c_stop();
		 return 1;
	 }else{
		 i2c_write(0x2D);
		 i2c_write(0x08);
		 i2c_stop(); // megnezni, hogy a stop kiuriti-e a buffert!
	 };
	//init HMC5843
	 if( i2c_rep_start( ( I2C_HMC5843<<1) | I2C_WRITE ) == 1) {
		 i2c_stop();
		 return 2;
	 }else{
		 i2c_write(0x0);
		 i2c_write(0b00011000);		// Set to maximum rate
		 i2c_stop(); // megnezni, hogy a stop kiuriti-e a buffert!
	 };
	 if( i2c_rep_start( ( I2C_HMC5843<<1) | I2C_WRITE ) == 1) {
		 i2c_stop();
		 return 4;
	 }else{
		 i2c_write(0x02);
		 i2c_write(0x00);		// Enable measurement
		 i2c_stop(); // megnezni, hogy a stop kiuriti-e a buffert!
	 };
	//init PS-ITG-3200
	 if( i2c_rep_start( ( I2C_PSITG3200<<1) | I2C_WRITE ) == 1) {
		 i2c_stop();
		 return 8;
	 }else{
		 i2c_write(0x16);
		 i2c_write(0b00011000);		// Enable measurement
		 i2c_stop(); // megnezni, hogy a stop kiuriti-e a buffert!
	 };
	 return 0;
};

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
	usbRequest_t    *rq = (void *)data;
	if(rq->bRequest==1){
		//initialize I2C sensors
		outmsgbuf[0]=init_i2c_sensors();
		usbMsgPtr=outmsgbuf;
		return 1;
	};
	if(rq->bRequest==2){
		read_devices();
		usbMsgPtr=outmsgbuf;
		return 19;
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

#include <usb.h>        /* this is libusb, see http://libusb.sourceforge.net/ */
#include "opendevice.h" /* common code moved to separate module */

#define DEFAULT_USB_VID         0   /* any */
#define DEFAULT_USB_PID         0   /* any */

#define BYTETOBINARYPATTERN "%d%d%d%d%d%d%d%d"
#define BYTETOBINARY(byte)  \
  ((byte) & 0x80 ? 1 : 0), \
  ((byte) & 0x40 ? 1 : 0), \
  ((byte) & 0x20 ? 1 : 0), \
  ((byte) & 0x10 ? 1 : 0), \
  ((byte) & 0x08 ? 1 : 0), \
  ((byte) & 0x04 ? 1 : 0), \
  ((byte) & 0x02 ? 1 : 0), \
  ((byte) & 0x01 ? 1 : 0)

struct s1{
	unsigned char c1;
	unsigned char c2;	
};

union u1{
	struct s1 st;
	unsigned short int usi;
};


static int  vendorID = DEFAULT_USB_VID;
static int  productID = DEFAULT_USB_PID;
static char *vendorNamePattern = "Homemade";
static char *productNamePattern = "*";
static char *serialPattern = "*";
static char *sendBytes = NULL;
static int  sendByteCount;
static char *outputFile = NULL;
static int  endpoint = 0;
static int  outputFormatIsBinary = 0;
static int  showWarnings = 1;
static int  usbTimeout = 5000;
static int  usbCount = 128;
static int  usbConfiguration = 1;
static int  usbInterface = 0;

static int  usbDirection, usbType, usbRecipient, usbRequest, usbValue, usbIndex; /* arguments of control transfer */

/* ------------------------------------------------------------------------- */

/* ASCII to integer (number parsing) which allows hex (0x prefix),
 * octal (0 prefix) and decimal (1-9 prefix) input.
 */
static int  myAtoi(char *text)
{
long    l;
char    *endPtr;

    if(strcmp(text, "*") == 0)
        return 0;
    l = strtol(text, &endPtr, 0);
    if(endPtr == text){
        fprintf(stderr, "warning: can't parse numeric parameter ->%s<-, defaults to 0.\n", text);
        l = 0;
    }else if(*endPtr != 0){
        fprintf(stderr, "warning: numeric parameter ->%s<- only partially parsed.\n", text);
    }
    return l;
}

static int  parseEnum(char *text, ...)
{
va_list vlist;
char    *entries[64];
int     i, numEntries;

    va_start(vlist, text);
    for(i = 0; i < 64; i++){
        entries[i] = va_arg(vlist, char *);
        if(entries[i] == NULL)
            break;
    }
    numEntries = i;
    va_end(vlist);
    for(i = 0; i < numEntries; i++){
        if(strcasecmp(text, entries[i]) == 0)
            return i;
    }
    if(isdigit(*text)){
        return myAtoi(text);
    }
    fprintf(stderr, "Enum value \"%s\" not allowed. Allowed values are:\n", text);
    for(i = 0; i < numEntries; i++){
        fprintf(stderr, "  %s\n", entries[i]);
    }
    exit(1);
}

/* ------------------------------------------------------------------------- */

#define ACTION_LIST         0
#define ACTION_CONTROL      1
#define ACTION_INTERRUPT    2
#define ACTION_BULK         3

inline unsigned short endian_swap(unsigned short x)
{
    x = (x>>8)&0xff | 
        (x<<8);
	return x;
}


int main(int argc, char **argv)
{
usb_dev_handle  *handle = NULL;
int             opt, len, action, argcnt;
char            *myName = argv[0], *s, *rxBuffer = NULL;
FILE            *fp;
union u1 tmp[4];
productNamePattern = optarg;
action = ACTION_CONTROL;
argcnt = 7;
usb_init();

if(usbOpenDevice(&handle, vendorID, vendorNamePattern, productID, productNamePattern, serialPattern, action == ACTION_LIST ? stdout : NULL, showWarnings ? stderr : NULL) != 0){
	fprintf(stderr, "Could not find USB device with VID=0x%x PID=0x%x Vname=%s Pname=%s Serial=%s\n", vendorID, productID, vendorNamePattern, productNamePattern, serialPattern);
        exit(1);
    }
    usbDirection = 1;
    if(usbDirection){   /* IN transfer */
        rxBuffer = malloc(usbCount);
    }

        int requestType;
        usbType = 2;
        usbRecipient = 0;
        usbRequest = 1;
        usbValue = 0;
        usbIndex = 0;
        requestType = ((usbDirection & 1) << 7) | ((usbType & 3) << 5) | (usbRecipient & 0x1f);
		int qwerty=0;
while(++qwerty<2)
{
        if(usbDirection){   /* IN transfer */
            len = usb_control_msg(handle, requestType, usbRequest, usbValue, usbIndex, rxBuffer, usbCount, usbTimeout);
        };


    if(len < 0){
        fprintf(stderr, "USB error: %s\n", usb_strerror());
        exit(1);
    }
    if(rxBuffer != NULL){
        FILE *fp = stdout;
        if(fp == NULL){
            fprintf(stderr, "Error writing \"%s\": %s\n", outputFile, strerror(errno));
            exit(1);
        }
		fprintf(fp,"0b%d%d%d%d%d%d%d%d %d%d%d%d%d%d%d%d %d%d%d%d%d%d%d%d %d%d%d%d%d%d%d%d %d%d%d%d%d%d%d%d %d%d%d%d%d%d%d%d %d%d%d%d%d%d%d%d %d%d%d%d%d%d%d%d\n",BYTETOBINARY(rxBuffer[0] & 0xff),BYTETOBINARY(rxBuffer[1] & 0xff),BYTETOBINARY(rxBuffer[2] & 0xff),BYTETOBINARY(rxBuffer[3] & 0xff),BYTETOBINARY(rxBuffer[4] & 0xff),BYTETOBINARY(rxBuffer[5] & 0xff),BYTETOBINARY(rxBuffer[6] & 0xff),BYTETOBINARY(rxBuffer[7] & 0xff));
//		fprintf(fp,"0b%d%d %d%d%d%d%d%d%d%d%d%d%d%d %d%d\n",BYTETOBINARY(rxBuffer[0] & 0xff),BYTETOBINARY(rxBuffer[1] & 0xff));

//		int i=0;
//		for(i=0;i<4;i++){
//		tmp[i].st.c1=((rxBuffer[2*i] &0b00111111)>>2);
//		tmp[i].st.c2=((rxBuffer[2*i+1])>>2);
//		fprintf(fp,"%d\t",endian_swap(tmp[i].usi));
//		};
//		fprintf(fp,"\n");
//		fprintf(fp,"0b%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d\n",BYTETOBINARY(tmp.st.c1),BYTETOBINARY(tmp.st.c2));

//		fprintf(fp,"%d\n\n",endian_swap(tmp.usi));

    }
}
    usb_close(handle);
    if(rxBuffer != NULL)
        free(rxBuffer);
    return 0;
}

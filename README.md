# ESP32 Game Boy Game Link Cable API
Simple API that can be included in projects to provide a flexible and easy way to handle data exchange over a Game Link Cable.

## Current feature status and future roadmap:
- [x] Ability to use EXT clock source. i.e. connected device drives the clock line  
- [x] Callback on byte transfer completion  
- [x] Flexibility in IO pin selection at alloc time  
- [ ] Ability to enable and disable interrupt on input clock  
- [x] Ability to set timeout in microseconds between clock edges. If exceeded, it is assumed the next clock is the first bit of a byte  
- [x] Set a NO\_DATA\_BYTE pattern. i.e. after a byte transfer is complete, a default byte is prepared to be sent out if no new data is provided before the transfer starts  
- [x] Supports communication to GBC  
- [x] Supports communication to GBA using GBC games  
- [ ] Supports communication to GB (untested, but should work)  
- [ ] Supports communication to GBA using GBA games  
- [ ] Function as INT clock source. i.e. Flipper Zero drives the clock line  
- [ ] Drive clock at varying speeds as GBC supports  
- [ ] Proper documentation

## Use example

```c
#include <gblink.h>

//  define pins
#define GPIO_MISO       8
#define GPIO_MOSI       9
#define GPIO_SCLK       7

struct gblink_pins* gblink_pins;
//  Callback function
//  This function retrieves the context from the configuration (gblink_def.cb_context) and the byte received.
static void transferByte(void* context, uint8_t in_byte) {
    //  Ej.: MyType* anything = (MyType*)context;
    //  ...
}
void func(void) {
    //  Initialize
    gblink_pins = malloc(sizeof(struct gblink_pins*));
    //  Set Pins
    gblink_pins->serin  = GPIO_MOSI;
    gblink_pins->serout = GPIO_MISO;
    gblink_pins->clk    = GPIO_SCLK;

    struct gblink_def gblink_def = {0};
    gblink_def.pins = gblink_pins;
    //  Define Callback and Context
    gblink_def.callback = transferByte;
    gblink_def.cb_context = NULL;   //  any type

    gblink_handle = gblink_alloc(&gblink_def);
    gblink_nobyte_set(gblink_handle, SERIAL_NO_DATA_BYTE);
}

```

## Acknowledgments
 - This is a port for ESP32 of the original project by @kbembedded Kris Bahnsen for Flipper Zero.
    
    https://github.com/kbembedded/flipper-gblink
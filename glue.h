// glue minigb_apu and gb_cardputer together
// very rough
// matthew5pl.net

typedef unsigned int uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char uint8_t;

typedef int int32_t;
typedef short int16_t;
typedef char int8_t;

// who cares about efficiency
#define bool char
#define true 1
#define false 0
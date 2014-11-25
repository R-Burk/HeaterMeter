// HeaterMeter Copyright 2011 Bryan Mayland <bmayland@capnbry.net>
#include "ShiftRegLCD.h"
#include <digitalWriteFast.h>
#include "hmcore.h"

// See hmcore.h for most options and tweaks

#ifdef HEATERMETER_RFM12
#include "rf12_itplus.h"
#endif /* HEATERMETER_RFM12 */

/* Disable the watchdog timer immediately after zero_reg is set */
__attribute__((naked)) __attribute__((section(".init3")))
  void clearWdt(void)
{
  MCUSR = 0;
  WDTCSR = _BV(WDCE) | _BV(WDE);
  WDTCSR = 0;
}

//Stack reporting functions. For some reason Arduino will not run these correctly as an include

#define STACK_CANARY 0xc5
extern uint8_t _end;
extern uint8_t __stack;
void StackPaint(void) __attribute__ ((naked)) __attribute__ ((section (".init1")));
void StackPaint(void)
{
    __asm volatile ("    ldi r30,lo8(_end)\n"
                    "    ldi r31,hi8(_end)\n"
                    "    ldi r24,lo8(0xc5)\n" /* STACK_CANARY = 0xc5 */
                    "    ldi r25,hi8(__stack)\n"
                    "    rjmp .cmp\n"
                    ".loop:\n"
                    "    st Z+,r24\n"
                    ".cmp:\n"
                    "    cpi r30,lo8(__stack)\n"
                    "    cpc r31,r25\n"
                    "    brlo .loop\n"
                    "    breq .loop"::);
}

uint16_t StackCount(void)
{
    const uint8_t *p = &_end;
    uint16_t       c = 0;

    while(*p == STACK_CANARY && p <= &__stack)
    {
        p++;
        c++;
    }

    return c;
}

int main(void)
{
  init();
  hmcoreSetup();
  for (;;)
    hmcoreLoop();
  return 0;
}


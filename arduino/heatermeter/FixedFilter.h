#pragma once

#ifndef __FIXEDFILTER_H__
#define __FIXEDFILTER_H__

/* Created using http://www.micromodeler.com/dsp/
 * Butterworth Filter
 * IIR Type - LowPass
 * Order - 2
 * Start Freq - .1 hz
 * Magnitude - 1.0 gain
 * Sample Hz - 1
 * Math - float
 * Biquads - DF1
*/

#include <avr/pgmspace.h>

const static PROGMEM float filter_coefficients[5] = {
  // All these coeffients are made on a 1 sample/second design
  // PID_PERIOD lowers the cutoff freq
  // Example - .5 Hz / 10 second PID_PERIOD feed is .05 Hz or 20 seconds/cycle filter
  //
  // .5 Hz coefficents ie 2 seconds/cycle filter
  // 0.5, 1, 0.5, -1, 0
  // .333 Hz cofficients ie 3 seconds/cycle filter
  // 0.46507547, 0.93015094, 0.46507547, -0.61995063, -0.24035125
  // .2 Hz coefficients ie  5 seconds/cycle filter
  // 0.20657208, 0.41314416, 0.20657208, 0.36952737, -0.19581571
  // .1 Hz coefficients ie 10 seconds/cycle filter
  0.067455273, 0.13491054, 0.067455273, 1.1429805, -0.41280159
}; //b0,b1,b2,a1,a2

class FixedFilter
{
    //friend class FilterCascaded;

    protected:

        // input and output arrays
        float xv[3];
        float yv[3];
        signed char missed;

    public:
        FixedFilter()
        {
            init(0.0);
        }

        ~FixedFilter()
        {
        }

        float init(float val);

        float add(float val);                    // adds a value and returns the most recent filter output

        float readInput(void)
        {
            return xv[0];
        }

        float readOutput(void)
        {
            return yv[0];
        }

        float readPrevOutput(void)
        {
            return yv[1];
        }

        float readOldestOutput(void)
        {
            return yv[2];
        }
};

#endif /* __FIXEDFILTER_H__ */

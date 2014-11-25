#ifndef __PID_H__
#define __PID_H__

#include "Arduino.h"
#include "grill_conf.h"

// Indexes into the pid array
#define PIDB 0
#define PIDP 1
#define PIDI 2
#define PIDD 3

// Error limit for Integrator during startup
#define INTEGRAL_FEED_LIMIT 15

class PID
{
private:
  // Last values used in PID calculation = B + P + I + D;
  float _pidCurrent[4];
  float _derivFilt; // derivative filter

public:
  PID(void) {};

  // The PID constants
  float Pid[4];

  void setPidConstant(unsigned char idx, float value);
  unsigned char calcPidOutput(void);
  float getDerivative(void) const { return _derivFilt; }
  float getPidCurrent(unsigned char idx) const { return _pidCurrent[idx]; }
};

#endif /* __PID_H__ */


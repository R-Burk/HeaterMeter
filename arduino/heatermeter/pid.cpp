#include "pid.h"
#include "grillcontrol.h"
#include "hmcore.h"

/* Calucluate the desired output percentage using the proportional–integral-derivative (PID) controller algorithm */
unsigned char PID::calcPidOutput(void)
{
  unsigned char pidOutput;
  unsigned char lastOutput = control.getControlOutput();
  unsigned char maxPidOutput = control.getMaxControlOutput();

  // If the pit probe is registering 0 degrees, don't jack the fan up to MAX
  if (!control.Probes[TEMP_PIT]->hasTemperature())
    return lastOutput;

  // If we're in lid open mode, fan should be off
  if (control.isLidOpen())
    return lastOutput;

  float currentTemp = control.Probes[TEMP_PIT]->Temperature;
  float error = control.getSetPoint() - currentTemp;

  // PPPPP = fan speed percent per degree of error
  // Proportional weighting - Reflects that is easier to heatup than cool down ( in normal range)
  if (currentTemp < control.getSetPoint() ) 
    _pidCurrent[PIDP] = Pid[PIDP] * error;
  else
    _pidCurrent[PIDP] = Pid[PIDP] * error * 2;

  // IIIII = fan speed percent per degree of accumulated error
  // Limit the error rate reaching Integrator
  if ( (control.getPitStartRecover() == PIDSTARTRECOVER_STARTUP) && (error > INTEGRAL_FEED_LIMIT) ) {
    error = INTEGRAL_FEED_LIMIT;
  }  // anti-windup: Make sure we only adjust the I term while inside the proportional control range
  if ((error > 0 && lastOutput < control.getMaxControlOutput()) || (error < 0 && lastOutput > 0))
    _pidCurrent[PIDI] += Pid[PIDI] * error;

  // DDDDD = fan speed percent per degree of change over TEMPPROBE_AVG_SMOOTH period
  error = control.Probes[TEMP_PIT]->TemperatureAvg - currentTemp;
  // Smooth out the Deriv as Pid calcs are being done extremely often
  // Will also tame huge instantaneous changes
  calcExpMovingAverage(DERIV_FILTER_SMOOTH, &_derivFilt, error);
  _pidCurrent[PIDD] = Pid[PIDD] * _derivFilt;

  // BBBBB = fan speed percent
  _pidCurrent[PIDB] = Pid[PIDB];

  int control = _pidCurrent[PIDB] + _pidCurrent[PIDP] + _pidCurrent[PIDI] + _pidCurrent[PIDD];
  pidOutput = constrain(control, 0, maxPidOutput);

  return pidOutput;
}

void PID::setPidConstant(unsigned char idx, float value)
{
  Pid[idx] = value;
}


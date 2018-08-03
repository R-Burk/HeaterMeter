﻿// HeaterMeter Copyright 2011 Bryan Mayland <bmayland@capnbry.net>
// GrillPid uses TIMER1 COMPB vector, as well as modifies the waveform
// generation mode of TIMER1. Blower output pin needs to be a hardware PWM pin.
// Fan output is 489Hz phase-correct PWM
// Servo output is 50Hz pulse duration
#include <math.h>
#include <string.h>
#include <util/atomic.h>
#include <digitalWriteFast.h>

#include "strings.h"
#include "grillpid.h"

extern GrillPid pid;

// For this calculation to work, ccpm()/8 must return a round number
#define uSecToTicks(x) ((unsigned int)(clockCyclesPerMicrosecond() / 8) * x)

// LERP percentage o into the unsigned range [A,B]. B - A must be < 655
#define mappct(o, a, b)  (((b - a) * (unsigned int)o / UPSCALE(100)) + a)

#define DIFFMAX(x,y,d) ((x - y + d) <= (d*2U))

#if defined(GRILLPID_SERVO_ENABLED)
ISR(TIMER1_CAPT_vect)
{
  unsigned int nextStep = pid.getServoStepNext(OCR1B);
  if (nextStep != 0)
  {
    digitalWriteFast(PIN_SERVO, HIGH);
    // Add the current timer1 count to offset the delay of calculating
    // the next move, and any interrupt latency due to the ADC code
    OCR1B = TCNT1 + nextStep;
  }
}

ISR(TIMER1_COMPB_vect, ISR_NAKED)
{
  digitalWriteFast(PIN_SERVO, LOW);
  asm("reti\n");
}
#endif

static struct tagAdcState
{
  unsigned char top;       // Number of samples to take per reading
  unsigned char cnt;       // count left to accumulate
  unsigned long accumulator;  // total
  unsigned char discard;   // Discard this many ADC readings
  unsigned char thisHigh;  // High this period
  unsigned char thisLow;   // Low this period
  unsigned char pin;       // pin for which these readings were made
  unsigned long analogReads[NUM_ANALOG_INPUTS]; // Current values
  unsigned char analogRange[NUM_ANALOG_INPUTS]; // high-low on last period
#if defined(GRILLPID_DYNAMIC_RANGE)
  bool useBandgapReference[NUM_ANALOG_INPUTS]; // Use 1.1V reference instead of AVCC
  unsigned int bandgapAdc;                     // 10-bit adc reading of BG with AVCC ref
#endif
#if defined(NOISEDUMP_PIN)
  unsigned int data[256];
#endif
} adcState;

#if defined(NOISEDUMP_PIN)
unsigned char g_NoisePin = NOISEDUMP_PIN;
#endif

ISR(ADC_vect)
{
  if (adcState.discard != 0)
  {
    --adcState.discard;
    // Actually do the calculations for the previous set of reads while in the
    // discard period of the next set of reads. Break the code up into chunks
    // of roughly the same number of clock cycles.
    if (adcState.discard == 1)
    {
      adcState.analogReads[adcState.pin] = adcState.accumulator;
      adcState.analogRange[adcState.pin] = adcState.thisHigh - adcState.thisLow;
    }
    else if (adcState.discard == 0)
    {
      adcState.thisHigh = 0;
      adcState.accumulator = 0;
      adcState.thisLow = 0xff;
      adcState.cnt = adcState.top;
      adcState.pin = ADMUX & 0x0f;
    }
    return;
  }

  if (adcState.cnt != 0)
  {
    --adcState.cnt;
    unsigned int adc = ADC;
#if defined(NOISEDUMP_PIN)
    if ((ADMUX & 0x07) == g_NoisePin)
      adcState.data[adcState.cnt] = adc;
#endif
    adcState.accumulator += adc;

    unsigned char a = adc >> 2;
    if (a > adcState.thisHigh)
      adcState.thisHigh = a;
    if (a < adcState.thisLow)
      adcState.thisLow = a;
  }
  else
  {
    unsigned char pin = ADMUX & 0x0f;
#if defined(GRILLPID_DYNAMIC_RANGE)
    if (pin > NUM_ANALOG_INPUTS)
    {
      // Store only the last ADC value, giving the bandgap ~25ms to stabilize
      adcState.bandgapAdc = ADC;
    }
#endif // GRILLPID_DYNAMIC_RANGE

    ++pin;
    if (pin >= NUM_ANALOG_INPUTS)
      pin = 0;
#if defined(GRILLPID_DYNAMIC_RANGE)
    unsigned char newref =
      adcState.useBandgapReference[pin] ? (INTERNAL << 6) : (DEFAULT << 6);
    unsigned char curref = ADMUX & 0xc0;
    // If switching references, allow time for AREF cap to charge
    if (curref != newref)
      adcState.discard = 48;  // 48 / 9615 samples/s = 5ms
    else
      adcState.discard = 3;

    ADMUX = newref | pin;
#else
    ADMUX = (DEFAULT << 6) | pin;
    adcState.discard = 3;
#endif
  }
}

unsigned int analogReadOver(unsigned char pin, unsigned char bits)
{
  unsigned long a;
  ATOMIC_BLOCK(ATOMIC_FORCEON)
  {
    a = adcState.analogReads[pin];
  }
  // Scale up to 256 samples then divide by 2^4 for 14 bit oversample
  unsigned int retVal = a * 16 / adcState.top;
  return retVal >> (14 - bits);
}

unsigned char analogReadRange(unsigned char pin)
{
  return adcState.analogRange[pin];
}

#if defined(GRILLPID_DYNAMIC_RANGE)
bool analogIsBandgapReference(unsigned char pin)
{
  return adcState.useBandgapReference[pin];
}

void analogSetBandgapReference(unsigned char pin, bool enable)
{
  adcState.useBandgapReference[pin] = enable;
}
#endif /* GRILLPID_DYNAMIC_RANGE */

static void adcDump(void)
{
#if defined(NOISEDUMP_PIN)
  static uint8_t x;
  ++x;
  if (x == 5)
  {
    x = 0;
    ADCSRA = bit(ADEN) | bit(ADATE) | bit(ADPS2) | bit(ADPS1) | bit (ADPS0);
    SerialX.print("HMLG,NOISE ");
    for (unsigned int i=0; i<adcState.top; ++i)
    {
      SerialX.print(adcState.data[i], DEC);
      SerialX.print(' ');
    }
    Serial_nl();
    ADCSRA = bit(ADEN) | bit(ADATE) | bit(ADIE) | bit(ADPS2) | bit(ADPS1) | bit (ADPS0) | bit(ADSC);
  }
#endif
}

static void calcExpMovingAverage(const float smooth, float *currAverage, float newValue)
{
  newValue = newValue - *currAverage;
  *currAverage = *currAverage + (smooth * newValue);
}

void ProbeAlarm::updateStatus(int value)
{
  // Low: Arming point >= Thresh + 1.0f, Trigger point < Thresh
  // A low alarm set for 100 enables at 101.0 and goes off at 99.9999...
  if (getLowEnabled())
  {
    if (value >= (getLow() + 1))
      Armed[ALARM_IDX_LOW] = true;
    else if (value < getLow() && Armed[ALARM_IDX_LOW])
      Ringing[ALARM_IDX_LOW] = true;
  }

  // High: Arming point < Thresh - 1.0f, Trigger point >= Thresh
  // A high alarm set for 100 enables at 98.9999... and goes off at 100.0
  if (getHighEnabled())
  {
    if (value < (getHigh() - 1))
      Armed[ALARM_IDX_HIGH] = true;
    else if (value >= getHigh() && Armed[ALARM_IDX_HIGH])
      Ringing[ALARM_IDX_HIGH] = true;
  }

  if (pid.isLidOpen())
    Ringing[ALARM_IDX_LOW] = Ringing[ALARM_IDX_HIGH] = false;
}

void ProbeAlarm::setHigh(int value)
{
  setThreshold(ALARM_IDX_HIGH, value);
}

void ProbeAlarm::setLow(int value)
{
  setThreshold(ALARM_IDX_LOW, value);
}

void ProbeAlarm::setThreshold(unsigned char idx, int value)
{
  Armed[idx] = false;
  Ringing[idx] = false;
  /* 0 just means silence */
  if (value == 0)
    return;
  Thresholds[idx] = value;
}

TempProbe::TempProbe(const unsigned char pin) :
  _pin(pin), _tempStatus(TSTATUS_NONE)
{
}

void TempProbe::loadConfig(struct __eeprom_probe *config)
{
  _probeType = config->probeType;
  Offset = config->tempOffset;
  memcpy(Steinhart, config->steinhart, sizeof(Steinhart));
  Alarms.setLow(config->alarmLow);
  Alarms.setHigh(config->alarmHigh);
}

void TempProbe::setProbeType(unsigned char probeType)
{
  _probeType = probeType;
  _tempStatus = TSTATUS_NONE;
  _hasTempAvg = false;
}

void TempProbe::calcTemp(unsigned int adcval)
{
  // Units 'A' = ADC value
  if (pid.getUnits() == 'A')
  {
    Temperature = adcval;
    _tempStatus = TSTATUS_OK;
    return;
  }

  // Ignore probes within 1 LSB of max
  if (adcval > 1022 * pow(2, TEMP_OVERSAMPLE_BITS) || adcval == 0)
    _tempStatus = TSTATUS_NONE;
  else
  {
    const float ADCmax = 1023 * pow(2, TEMP_OVERSAMPLE_BITS);

    if (_probeType == PROBETYPE_TC_ANALOG)
    {
      float mvScale = Steinhart[3];
      // Commented out because there's no "divide by zero" exception so
      // just allow undefined results to save prog space
      //if (mvScale == 0.0f)
      //  mvScale = 1.0f;
      // If scale is <100 it is assumed to be mV/C with a 3.3V reference
      if (mvScale < 100.0f)
        mvScale = 3300.0f / mvScale;
#if defined(GRILLPID_DYNAMIC_RANGE)
      if (analogIsBandgapReference(_pin))
      {
        analogSetBandgapReference(_pin, adcval < (1000U * (unsigned char)pow(2, TEMP_OVERSAMPLE_BITS)));
        mvScale /= 1023.0f / adcState.bandgapAdc;
      }
      else
        analogSetBandgapReference(_pin, adcval < (300U * (unsigned char)pow(2, TEMP_OVERSAMPLE_BITS)));
#endif
      setTemperatureC(adcval / ADCmax * mvScale);
    }
    else {
      float R, T;
      // If you put the fixed resistor on the Vcc side of the thermistor, use the following
      R = Steinhart[3] / ((ADCmax / (float)adcval) - 1.0f);
      // If you put the thermistor on the Vcc side of the fixed resistor use the following
      //R = Steinhart[3] * ADCmax / (float)Vout - Steinhart[3];

      // Units 'R' = resistance, unless this is the pit probe (which should spit out Celsius)
      if (pid.getUnits() == 'R' && this != pid.Probes[TEMP_CTRL])
      {
        Temperature = R;
        _tempStatus = TSTATUS_OK;
        return;
      };

      // Compute degrees K
      R = log(R);
      T = 1.0f / ((Steinhart[2] * R * R + Steinhart[1]) * R + Steinhart[0]);

      setTemperatureC(T - 273.15f);
    } /* if PROBETYPE_INTERNAL */
  } /* if ADCval */
}

void TempProbe::processPeriod(void)
{
  // Called once per measurement period after temperature has been calculated
  if (hasTemperature())
  {
    if (!_hasTempAvg)
    {
      TemperatureAvg = Temperature;
      _hasTempAvg = true;
    }
    else
      calcExpMovingAverage(TEMPPROBE_AVG_SMOOTH, &TemperatureAvg, Temperature);
    Alarms.updateStatus(Temperature);
  }
  else
    Alarms.silenceAll();
}

void TempProbe::setTemperatureC(float T)
{
  // Sanity - anything less than -20C (-4F) or greater than 500C (932F) is rejected
  if (T <= -20.0f)
    _tempStatus = TSTATUS_LOW;
  else if (T >= 500.0f)
    _tempStatus = TSTATUS_HIGH;
  else
  {
    if (pid.getUnits() == 'F')
      Temperature = (T * (9.0f / 5.0f)) + 32.0f;
    else
      Temperature = T;
    Temperature += Offset;
    _tempStatus = TSTATUS_OK;
  }
}

void GrillPid::init(void)
{
#if defined(GRILLPID_SERVO_ENABLED)
  pinModeFast(PIN_SERVO, OUTPUT);

  // CTC mode with ICR1 as TOP, 8 prescale, INT on COMPB and TOP (ICR
  // Period set to SERVO_REFRESH
  // If GrillPid is constructed statically this can't be done in the constructor
  // because the Arduino core init is called after the constructor and will set
  // the values back to the default
  ICR1 = uSecToTicks(SERVO_REFRESH);
  TCCR1A = 0;
  TCCR1B = bit(WGM13) | bit(WGM12) | bit(CS11);
  TIMSK1 = bit(ICIE1) | bit(OCIE1B);
#endif
  // Initialize ADC for free running mode at 125kHz
#if defined(GRILLPID_DYNAMIC_RANGE)
  // Start by measuring the bandgap reference for dynamic range scaling
  ADMUX = (DEFAULT << 6) | 0b1110;
#else
  ADMUX = (DEFAULT << 6) | 0;
#endif // GRILLPID_DYNAMIC_RANGE
  ADCSRB = bit(ACME);
  ADCSRA = bit(ADEN) | bit(ADATE) | bit(ADIE) | bit(ADPS2) | bit(ADPS1) | bit (ADPS0) | bit(ADSC);

  updateControlProbe();
}

void __attribute__ ((noinline)) GrillPid::updateControlProbe(void)
{
  // Set control to the first non-Disabled probe. If all probes are disabled, return TEMP_PIT
  Probes[TEMP_CTRL] = Probes[TEMP_PIT];
  for (uint8_t i=0; i<TEMP_COUNT; ++i)
    if (Probes[i]->getProbeType() != PROBETYPE_DISABLED)
    {
      Probes[TEMP_CTRL] = Probes[i];
      break;
    }
}

void GrillPid::setProbeType(unsigned char idx, unsigned char probeType)
{
  Probes[idx]->setProbeType(probeType);
  updateControlProbe();
}

void GrillPid::setOutputFlags(unsigned char value)
{
  _outputFlags = value;

  unsigned char newTop;
  // 50Hz = 192.31 samples
  if (bit_is_set(value, PIDFLAG_LINECANCEL_50))
     newTop = 192;
  // 60Hz = 160.25 samples
  else if (bit_is_set(value, PIDFLAG_LINECANCEL_60))
    newTop = 160;
  else
    newTop = 255;
  ATOMIC_BLOCK(ATOMIC_FORCEON)
  {
    adcState.top = newTop;
    adcState.discard = 3;
  }
#if defined(FAN_30HZ)
  //TCCR2A = bit(WGM21) | bit(WGM20);  // Timer2 Fast PWM
  TCCR2A = bit(WGM20); // Timer 2 Phase Correct PWM
  //TCCR2B = bit(CS20); // 62kHz (fast) 31kHz (phase correct)
  //TCCR2B = bit(CS21); // 7.8khz (fast) 3.9kHz (phase correct)
  //TCCR2B = bit(CS21) | bit(CS20); // 1953Hz (fast) 980Hz (phase correct)
  //TCCR2B = bit(CS22); // 976Hz (fast) 490Hz (phase correct)
  //TCCR2B = bit(CS22) | bit(CS20); // 488Hz (fast) 245Hz (phase correct)
  //TCCR2B = bit(CS22) | bit(CS21); // 244Hz (fast) 122Hz (phase correct)
  TCCR2B = bit(CS22) | bit(CS21) | bit(CS20); // 61Hz 30.6Hz (phase correct)
#endif
#if defined(FAN_20KHZ)
  TCCR2A = bit(WGM21) | bit(WGM20);  // Timer2 Fast PWM
  TCCR2B = bit(WGM22) | bit(CS21); // 7.8khz (fast), top compare to OCR2A
  OCR2A = 100;  // 20khz instead of 7.8khz
#endif
#if defined(FAN_31KHZ)
  TCCR2A = bit(WGM20); // Timer 2 Phase Correct PWM
  TCCR2B = bit(CS20); // 62kHz (fast) 31kHz (phase correct)
#endif
}

void GrillPid::servoRangeChanged(void)
{
#if defined(SERVO_MIN_THRESH)
  _servoHoldoff = SERVO_MAX_HOLDOFF;
#endif
}

void GrillPid::setServoMinPos(unsigned char value)
{
  _servoMinPos = value;
  servoRangeChanged();
}

void GrillPid::setServoMaxPos(unsigned char value)
{
  _servoMaxPos = value;
  servoRangeChanged();
}

unsigned int GrillPid::countOfType(unsigned char probeType) const
{
  unsigned char retVal = 0;
  for (unsigned char i=0; i<TEMP_COUNT; ++i)
    if (Probes[i]->getProbeType() == probeType)
      ++retVal;
  return retVal;  
}

/* Calculate the desired output percentage using the proportional–integral-derivative (PID) controller algorithm */
inline void GrillPid::calcPidOutput(void)
{
  _pidOutput = 0;
  unsigned char lastOutput = DNSCALE(_pidOutput);

  // If the pit probe is registering 0 degrees, don't jack the fan up to MAX
  if (!Probes[TEMP_CTRL]->hasTemperature())
    return;
    return;
  }
  _pidPeriodCounter = 0;
  
  float currentTemp, derivative;

  float currentTemp = Probes[TEMP_CTRL]->Temperature;
  float error;
  error = _setPoint - currentTemp;

  // PPPPP = fan speed percent per degree of error
  _pidCurrent[PIDP] = Pid[PIDP] * error;

  // IIIII = fan speed percent per degree of accumulated error
  // anti-windup: Make sure we only adjust the I term while inside the proportional control range
  if ((error > 0 && lastOutput < 100) || (error < 0 && lastOutput > 0))
  {
    _pidCurrent[PIDI] += Pid[PIDI] * error;
    // I term can never be negative, because if curr = set, then P and D are 0, so I must be output
    if (_pidCurrent[PIDI] < 0.0f) _pidCurrent[PIDI] = 0.0f;
  }

  // DDDDD = fan speed percent per degree of change over TEMPPROBE_AVG_SMOOTH period
  _pidCurrent[PIDD] = Pid[PIDD] * (Probes[TEMP_CTRL]->TemperatureAvg - currentTemp);
  // BBBBB = fan speed percent (always 0)
  //_pidCurrent[PIDB] = Pid[PIDB];
#if defined(UPSCALAR)  
  int control = (_pidCurrent[PIDP] + _pidCurrent[PIDI] + _pidCurrent[PIDD]) * UPSCALAR;
#else
  int control = _pidCurrent[PIDP] + _pidCurrent[PIDI] + _pidCurrent[PIDD];
#endif
  _pidOutput = constrain(control, 0, UPSCALE(100));
}

void GrillPid::adjustFeedbackVoltage(void)
{
  if ( _fanStartBoost > 0) {
    _fanStartBoost--;
#if defined(FAN_30HZ) || defined(FAN_31KHZ)
    analogWrite(PIN_BLOWER, 255);
#endif
#if defined(FAN_20KHZ)
    analogWrite(PIN_BLOWER, 100);
#endif
  }
  else {
    analogWrite(PIN_BLOWER, _lastBlowerOutput);
  }
}

#if 0
inline unsigned char FeedvoltToAdc(float v)
{
  // Calculates what an 8 bit ADC value would be for the given voltage
  const unsigned long R1 = 22000;
  const unsigned long R2 = 68000;
  // Scale the voltage by the voltage divder
  // v * R1 / (R1 + R2) = pV
  // Scale to ADC assuming 3.3V reference
  // (pV / 3.3) * 256 = ADC
  return ((v * R1 * 256) / ((R1 + R2) * 3.3f));
}
#endif

inline void GrillPid::commitFanOutput(void)
{
  /* Long PWM period is 10 sec */
  const unsigned int LONG_PWM_PERIOD = 10000;
  const unsigned int PERIOD_SCALE = (LONG_PWM_PERIOD / TEMP_MEASURE_PERIOD);

  if (_pidOutput < UPSCALE(_fanActiveFloor))
    _fanSpeed = 0;
  else
  {
    unsigned char max;
    if (_pitStartRecover == PIDSTARTRECOVER_STARTUP)
      max = _fanMaxStartupSpeed;
    else
      max = _fanMaxSpeed;

    // _fanActiveFloor should be constrained to 0-99 to prevent a divide by 0
    unsigned char range = 100 - _fanActiveFloor;
#ifndef UPSCALAR
    _fanSpeed = (unsigned int)(_pidOutput - _fanActiveFloor) * max / range;
#else
    _fanSpeed = (unsigned long)(_pidOutput - UPSCALE(_fanActiveFloor)) * max / range;
#endif /* UPSCALAR */
  }

  // 0 is always 0
  if (_fanSpeed == 0)
    _lastBlowerOutput = 0;
  else
  {
    // If going from 0% to non-0%, turn the blower fully on for one period
    // to get it moving (boost mode)
    if (_lastBlowerOutput == 0)
    {
      // some fans don't get up  to speed fast enough for PWM to keep running
      _fanStartBoost = TEMP_OUTADJUST_CNT + 1;
    }
    // Make sure we don't drive fan lower than user has indicated PWM can handle
#if defined(FAN_30HZ) || defined(FAN_31KHZ)
      unsigned char minPWM = mappct(_fanMinSpeed, 1, 254);
#ifndef UPSCALAR
      _lastBlowerOutput = mappct(_fanSpeed, minPWM, 254);
#else
      //_lastBlowerOutput = (unsigned char)map(_fanSpeed, 0, UPSCALE(100), minPWM, 254);
      _lastBlowerOutput = (unsigned char)MAP(_fanSpeed, 0, UPSCALE(100), minPWM, 254);
#endif /* UPSCALAR */
#endif
#if defined(FAN_20KHZ)
      unsigned char minPWM = mappct(_fanMinSpeed, 1, 100);
#ifndef UPSCALAR
      _lastBlowerOutput = mappct(_fanSpeed, minPWM, 100);
#else
      //_lastBlowerOutput = (unsigned char)map(_fanSpeed, 0,UPSCALE(100), minPWM, 100);
      _lastBlowerOutput = (unsigned char)MAP(_fanSpeed, 0,UPSCALE(100), minPWM, 100);
#endif /* UPSCALAR */
#endif
  }
  adjustFeedbackVoltage();
}

unsigned int GrillPid::getServoStepNext(unsigned int curr)
{
#if defined(GRILLPID_SERVO_ENABLED)
  const unsigned int SERVO_STEP = uSecToTicks(15U);
  const unsigned int SERVO_HOLD_SECS = 2U;

  // Hold the servo for SERVO_HOLD_SECS seconds then turn off on the next period
  if (_servoStepTicks >= (SERVO_HOLD_SECS * 1000000UL / SERVO_REFRESH))
    return 0;

  // If at or close to target, snap to target
  // curr is 0 on first interrupt
  if (DIFFMAX(_servoTarget, curr, SERVO_STEP) || curr == 0)
  {
    ++_servoStepTicks;
    return _servoTarget;
  }
  // Else slew toward target
  else if (_servoTarget > curr)
    return curr + SERVO_STEP;
  else
    return curr - SERVO_STEP;
#endif
}

inline void GrillPid::commitServoOutput(void)
{
#if defined(GRILLPID_SERVO_ENABLED)
#ifndef UPSCALAR
  unsigned char output;
#else
  unsigned int  output;
  output = constrain(_pidOutput, 0, UPSCALE(_servoActiveCeil));
#endif /* UPSCALAR */

#ifndef UPSCALAR
  // Servo is open 0% at 0 PID output and 100% at _servoActiveCeil PID output
  if (_pidOutput >= UPSCALE(_servoActiveCeil))
    output = UPSCALE(100);
  else {
    output = (unsigned int)_pidOutput * 100U / _servoActiveCeil;
  }
#endif /* UPSCALAR */

  if (bit_is_set(_outputFlags, PIDFLAG_INVERT_SERVO))
    output = UPSCALE(100) - output;

  // Get the output speed in 10x usec by LERPing between min and max
#ifndef UPSCALAR
  output = mappct(output, _servoMinPos, _servoMaxPos);
#else
  //output = (unsigned int)map(output, 0, UPSCALE(_servoActiveCeil), _servoMinPos, _servoMaxPos);
  output = (unsigned int)MAP(output, 0, UPSCALE(_servoActiveCeil), _servoMinPos, _servoMaxPos);
#endif /*UPSCALAR */
  unsigned int targetTicks = uSecToTicks(10U * output);
#if defined(SERVO_MIN_THRESH)
  if (_servoHoldoff < 0xff)
    ++_servoHoldoff;
  // never pulse the servo if change isn't needed
  if (_servoTarget == targetTicks)
    return;

  // and only trigger the servo if a large movement is needed or holdoff expired
  boolean isBigMove = !DIFFMAX(_servoTarget, targetTicks, uSecToTicks(SERVO_MIN_THRESH));
  if (isBigMove || _servoHoldoff > SERVO_MAX_HOLDOFF)
#endif
  {
    ATOMIC_BLOCK(ATOMIC_FORCEON)
    {
      _servoStepTicks = 0;
      _servoTarget = targetTicks;
    }
    _servoHoldoff = 0;
  }
#endif
}

inline void GrillPid::commitPidOutput(void)
{
  calcExpMovingAverage(PIDOUTPUT_AVG_SMOOTH, &PidOutputAvg, DNSCALE(_pidOutput));
  commitFanOutput();
  commitServoOutput();
}

boolean GrillPid::isAnyFoodProbeActive(void) const
{
  unsigned char i;
  for (i=TEMP_FOOD1; i<TEMP_COUNT; i++)
    if (Probes[i]->hasTemperature())
      return true;
      
  return false;
}
  
void GrillPid::resetLidOpenResumeCountdown(void)
{
  LidOpenResumeCountdown = _lidOpenDuration;
  _pitStartRecover = PIDSTARTRECOVER_RECOVERY;
}

void GrillPid::setSetPoint(int value)
{
  // I don't want a recovery mode for small changes in setpoint - RCB
  if ( abs(value - _setPoint) > 20) {
      _pitStartRecover = PIDSTARTRECOVER_STARTUP;
  }
  _setPoint = value;
  _manualOutputMode = false;
  LidOpenResumeCountdown = 0;
}

void GrillPid::setPidOutput(int value)
{
  _manualOutputMode = true;
  _pidOutput = constrain(value, 0, UPSCALE(100));
  LidOpenResumeCountdown = 0;
}

void GrillPid::setPidConstant(unsigned char idx, float value)
{
  Pid[idx] = value;
  if (idx == PIDI && value == 0)
    _pidCurrent[PIDI] = 0;
}

void GrillPid::setLidOpenDuration(unsigned int value)
{
  _lidOpenDuration = (value > LIDOPEN_MIN_AUTORESUME) ? value : LIDOPEN_MIN_AUTORESUME;
}

void GrillPid::status(void)
{
#if defined(GRILLPID_SERIAL_ENABLED)
  SerialX.print(getSetPoint(), DEC);
  Serial_csv();

  for (unsigned char i=0; i<TEMP_COUNT; ++i)
  {
    if (Probes[i]->hasTemperature())
      SerialX.print(Probes[i]->Temperature, 1);
    else
      Serial_char('U');
    Serial_csv();
  }

  SerialX.print(DNSCALE(getPidOutput()), DEC);
  Serial_csv();
  SerialX.print((int)PidOutputAvg, DEC);
  Serial_csv();
  SerialX.print(LidOpenResumeCountdown, DEC);
  Serial_csv();
  SerialX.print(getFanSpeed(), DEC);
#endif
}

boolean GrillPid::doWork(void)
{
  unsigned int elapsed = millis() - _lastWorkMillis;
  if (elapsed < (TEMP_MEASURE_PERIOD / TEMP_OUTADJUST_CNT))
    return false;
  _lastWorkMillis = millis();

  if (_periodCounter < (TEMP_OUTADJUST_CNT-1))
  {
    ++_periodCounter;
    adjustFeedbackVoltage();
    return false;
  }
  _periodCounter = 0;

#if defined(GRILLPID_CALC_TEMP) 
  for (unsigned char i=0; i<TEMP_COUNT; i++)
  {
    if (Probes[i]->getProbeType() == PROBETYPE_INTERNAL ||
        Probes[i]->getProbeType() == PROBETYPE_TC_ANALOG)
      Probes[i]->calcTemp(analogReadOver(Probes[i]->getPin(), 10+TEMP_OVERSAMPLE_BITS));
    Probes[i]->processPeriod();
  }

  // Always calculate the output
  // calcPidOutput() will bail if it isn't supposed to be in control
  calcPidOutput();
  
  if (!_manualOutputMode)
  {
    int tempDiff = _setPoint - (int)Probes[TEMP_CTRL]->Temperature;
    if ((tempDiff <= 0) &&
      (_lidOpenDuration - LidOpenResumeCountdown > LIDOPEN_MIN_AUTORESUME))
    {
      // When we first achieve temperature, reduce any I sum we accumulated during startup
      // If we actually neded that sum to achieve temperature we'll rebuild it, and it
      // prevents bouncing around above the temperature when you first start up
#if defined GRILLPID_CUTBACK
      if (_pitStartRecover == PIDSTARTRECOVER_STARTUP)
      {
        _pidCurrent[PIDI] *= 0.50f;
      }
#endif
      _pitStartRecover = PIDSTARTRECOVER_NORMAL;
      LidOpenResumeCountdown = 0;
    }
    else if (LidOpenResumeCountdown != 0)
    {
      LidOpenResumeCountdown = LidOpenResumeCountdown - (TEMP_MEASURE_PERIOD / 1000);
    }
    // If the pit temperature has been reached
    // and if the pit temperature is [lidOpenOffset]% less that the setpoint
    // and if the fan has been running less than 90% (more than 90% would indicate probable out of fuel)
    // Note that the code assumes we're not currently counting down
    else if (isPitTempReached() && 
      (((tempDiff*100)/_setPoint) >= (int)LidOpenOffset) &&
      ((int)PidOutputAvg < 90))
    {
      resetLidOpenResumeCountdown();
    }
  }   /* if !manualFanMode */
#endif

  commitPidOutput();
  adcDump();
  return true;
}

void GrillPid::pidStatus(void)
{
#if defined(GRILLPID_SERIAL_ENABLED)
  TempProbe const* const pit = Probes[TEMP_CTRL];
  if (pit->hasTemperature())
  {
    print_P(PSTR("HMPS"CSV_DELIMITER));
    for (unsigned char i=PIDB; i<=PIDD; ++i)
    {
      SerialX.print(_pidCurrent[i], 2);
      Serial_csv();
    }

    SerialX.print(pit->Temperature - pit->TemperatureAvg, 2);
    Serial_nl();
  }
#endif
}

void GrillPid::setUnits(char units)
{
  switch (units)
  {
    case 'A':
    case 'C':
    case 'F':
    case 'R':
      _units = units;
      break;
  }
}

#ifndef __GRILLFLC_H__
#define __GRILLFLC_H__

#include "Arduino.h"

/* Input Maps for Error Fuzzification */
const signed char errMapNegHigh[] = {-40,-40,-20};
const signed char errMapNegLow[]  = {-25,-10,  0};
const signed char errMapZero[]    = { -5,  0, -5};
const signed char errMapPosLow[]  = {  0, 10, 25};
const signed char errMapPosHigh[] = { 20, 40, 40};

/* Input maps for ChangeInError Fuzzification */
const signed char cieMapNegFast[] = {-4, -4, -2};
const signed char cieMapNegSlow[] = {-4, -2,  0};
const signed char cieMapZero[]    = {-2,  0, -2};
const signed char cieMapPosSlow[] = { 0,  2,  4};
const signed char cieMapPosFast[] = { 2,  4,  4};

/* Output coefficients for Defuzzification */
const signed char outNegHigh = -10;
const signed char outNeg =     -5;
const signed char outZero =     0;
const signed char outPos =      5;
const signed char outPosHigh =  10;

/* Gain weights for setpoint */
//const unsigned int _spLow[] = {200, 200, 350, 1, 4};
//const unsigned int _spMedLow[] = {200, 350, 500, 1, 2};
//const unsigned int _spMed[] = {350, 500, 650, 0, 1, 1};
//const unsigned int _spMedHigh[] = {500, 650, 800, 2, 1};
//const unsigned int _spHigh[] = {650, 800, 800, 4, 1};

typedef struct {
  float fit;
  unsigned char vote;
} output;

//Mapping for tuning array
#define FLC_ERR 0
#define FLC_CIE 1

class GrillFLC
{
private:
  float _errNegHigh, _errNegLow, _errZero, _errPosLow, _errPosHigh;
  float _cieNegFast, _cieNegSlow, _cieZero, _ciePosSlow, _ciePosFast;
  output _outNegHigh, _outNeg, _outZero, _outPos, _outPosHigh;
  float _crispOutput;

  float _err, _cie;

  void fuzzifyError (void);
  void fuzzifyCIE (void);
  void processRules (void);
  void defuzzifyOutput (void);
  float calcPertinence(float crispVal, const signed char* map);
  void outputVote (output out, float in1, float in2);

public:
  float tuning[2]; // adjustments used in fuzzifying inputs

  unsigned char doWork (void);
  float getDerivative(void) const { return _cie; }
  void setFlcTuning(unsigned char idx, float val);
};

#endif /* __GRILLFLC_H__ */


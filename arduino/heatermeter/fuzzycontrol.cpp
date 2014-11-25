#include "fuzzycontrol.h"
#include "grillcontrol.h"
#include "grill_conf.h"
#include "hmcore.h"

/* Determine which error nouns have membership */
void GrillFLC::fuzzifyError (void)
{
  float tuned_err = this->_err * this->tuning[FLC_ERR];

  this->_errPosHigh = this->calcPertinence(tuned_err, errMapPosHigh);
  this->_errPosLow = this->calcPertinence(tuned_err, errMapPosLow);
  this->_errZero = this->calcPertinence(tuned_err, errMapZero);
  this->_errNegLow = this->calcPertinence(tuned_err, errMapNegLow);
  this->_errNegHigh = this->calcPertinence(tuned_err, errMapNegHigh);
}

/* Determine which change in error nouns have membership */
void GrillFLC::fuzzifyCIE (void)
{
  float tuned_cie = this->_cie * this->tuning[FLC_CIE];

  this->_cieNegFast = this->calcPertinence(tuned_cie, cieMapNegFast);
  this->_cieNegSlow = this->calcPertinence(tuned_cie, cieMapNegSlow);
  this->_cieZero = this->calcPertinence(tuned_cie, cieMapZero);
  this->_ciePosSlow = this->calcPertinence(tuned_cie, cieMapPosSlow);
  this->_ciePosFast = this->calcPertinence(tuned_cie, cieMapPosFast);
}

/* Take a given value and compare against a triangle range of values
    if less than left side or higher than right side returns 0.0 indicating no membership
    otherwise returns a val between 0 and 1.0 indicating strength of membership
*/
float GrillFLC::calcPertinence(float crispVal, const signed char* map)
{
  float pert;

  if (crispVal < map[0]) {
    if (map[0] == map[1] && map[1] != map[2]) {
      pert = 1.0;
    } else {
      pert = 0.0;
    }
  } else if (crispVal >= map[0] && crispVal < map[1]) {
    pert = (map[1] - crispVal) / (map[1] - map[0]);
  } else if (crispVal == map[1]) {
    pert = 1.0;
  } else if (crispVal > map[1] && crispVal <= map[2]) {
    pert = (crispVal - map[1]) / (map[2] - map[1]);
  } else if (crispVal > map[2]) {
    if (map[2] == map[1] && map[1] != map[0]) {
      pert = 1.0;
    } else {
      pert = 0.0;
    }
  }
  return pert;
}

void GrillFLC::outputVote (output out, float in1, float in2) {
   out.fit += min( in1, in2);
   out.vote++;
 }

/* Rule Matrix
                    CIE 
              NF  NS  Zero  PS PF
        PH    PH  PH   PH   P   Z
        PL    PH  PH   P    Z   N
  Err   Zero  PH  P    Z    N   NH
        NL    P   Z    N    NH  NH
        NH    Z   N    NH   NH  NH
*/
void GrillFLC::processRules(void)
{
  this->_outNegHigh.fit = this->_outNeg.fit = this->_outZero.fit = this->_outPos.fit = this->_outPosHigh.fit = 0.0;
  this->_outNegHigh.vote = this->_outNeg.vote = this->_outZero.vote = this->_outPos.vote = this->_outPosHigh.vote = 0;

  //Upper Left Corner of rule matrix
  if ( ((this->_errPosHigh > 0.0) || (this->_errPosLow > 0.0))
    && ((this->_cieNegFast > 0.0) || (this->_cieNegSlow > 0.0)) ) {
      float tempErr = max(this->_errPosHigh, this->_errPosLow);
      float tempCIE = max(this->_cieNegFast, this->_cieNegSlow);
      outputVote(this->_outPosHigh, tempErr, tempCIE);
  }
  // Remaining rules for first row
  if ( (this->_errPosHigh > 0.0) && (this->_cieZero > 0.0))
    outputVote(this->_outPosHigh, this->_errPosHigh, this->_cieZero);
  if ( (this->_errPosHigh > 0.0) && (this->_ciePosSlow > 0.0))
    outputVote(this->_outPos, this->_errPosHigh, this->_ciePosSlow);
  if ( (this->_errPosHigh > 0.0) && (this->_ciePosFast > 0.0))
    outputVote(this->_outZero, this->_errPosHigh, this->_ciePosFast);
  //Remaining rule for second row
  if ( (this->_errPosLow > 0.0) && (this->_cieZero > 0.0))
    outputVote(this->_outPos, this->_errPosLow, this->_cieZero);
  if ( (this->_errPosLow > 0.0) && (this->_ciePosSlow > 0.0))
    outputVote(this->_outZero, this->_errPosLow, this->_ciePosSlow);
  if ( (this->_errPosLow > 0.0) && (this->_ciePosFast > 0.0) )
    outputVote(this->_outNeg, this->_errPosLow, this->_ciePosFast);
  //Rules for center row
  if ( (this->_errZero > 0.0) && (this->_cieNegFast > 0.0) )
    outputVote(this->_outPosHigh, this->_errZero, this->_cieNegFast);
  if ( (this->_errZero > 0.0) && (this->_cieNegSlow > 0.0) )
    outputVote(this->_outPos, this->_errZero, this->_cieNegSlow);
  if ( (this->_errZero > 0.0) && (this->_cieZero > 0.0) )
    outputVote(this->_outZero, this->_errZero, this->_cieZero);
  if ( (this->_errZero > 0.0) && (this->_ciePosSlow > 0.0) )
    outputVote(this->_outNeg, this->_errZero, this->_ciePosSlow);
  if ( (this->_errZero > 0.0) && (this->_ciePosFast > 0.0) )
    outputVote(this->_outNegHigh, this->_errZero, this->_ciePosFast);
  //Remaing rules for fourth row
  if ( (this->_errNegLow > 0.0) && (this->_cieNegFast > 0.0) )
    outputVote(this->_outPos, this->_errNegLow, this->_cieNegFast);
  if ( (this->_errNegLow > 0.0) && (this->_cieNegSlow > 0.0) )
    outputVote(this->_outZero, this->_errNegLow, this->_cieNegSlow);
  if ( (this->_errNegLow > 0.0) && (this->_cieZero > 0.0) )
    outputVote(this->_outNeg, this->_errNegLow, this->_cieZero);
  //Remaining rules fifth row
  if ( (this->_errNegHigh > 0.0) && (this->_cieNegFast > 0.0) )
    outputVote(this->_outZero, this->_errNegHigh, this->_cieNegFast);
  if ( (this->_errNegHigh > 0.0) && (this->_cieNegSlow > 0.0) )
    outputVote(this->_outNeg, this->_errNegHigh, this->_cieNegSlow);
  if ( (this->_errNegHigh > 0.0) && (this->_cieZero > 0.0) )
    outputVote(this->_outNegHigh, this->_errNegHigh, this->_cieZero);
  //Lower Right corner of matrix
  if ( ((this->_errNegHigh > 0.0) || (this->_errNegLow > 0.0))
    && ((this->_cieNegFast > 0.0) || (this->_cieNegSlow > 0.0)) ) {
      float tempErr = max(this->_errNegHigh, this->_errNegLow);
      float tempCIE = max(this->_cieNegFast, this->_cieNegSlow);
      outputVote(this->_outNegHigh, tempErr, tempCIE);
  }
  //Accumulate any rules with more than one vote
  if (this->_outNegHigh.vote > 1 ) {
    this->_outNegHigh.fit = this->_outNegHigh.fit / this->_outNegHigh.vote;
    this->_outNegHigh.vote = 1;
  }
  if (this->_outNeg.vote > 1) {
    this->_outNeg.fit = this->_outNeg.fit / this->_outNeg.vote;
    this->_outNeg.vote = 1;
  }
  if (this->_outZero.vote > 1) {
    this->_outZero.fit = this->_outZero.fit / this->_outZero.vote;
    this->_outZero.vote = 1;
  }
  if (this->_outPos.vote > 1) {
    this->_outPos.fit = this->_outPos.fit / this->_outPos.vote;
    this->_outPos.vote = 1;
  }
  if (this->_outPosHigh.vote > 1) {
    this->_outPosHigh.fit = this->_outPosHigh.fit / this->_outPosHigh.vote;
    this->_outPosHigh.vote = 1;
  }
}

void GrillFLC::defuzzifyOutput (void) {
  float num = 0.0;
  float denom = 0.0;

  if (this->_outNegHigh.vote ) {
    num += this->_outNegHigh.fit * outNegHigh;
    denom += this->_outNegHigh.fit;
  }
  if (this->_outNeg.vote) {
    num += this->_outNeg.fit * outNeg;
    denom += this->_outNeg.fit;
  }
  if (this->_outZero.vote) {
    num += this->_outZero.fit * outZero;
    denom += this->_outZero.fit;
  }
  if (this->_outPos.vote) {
    num += this->_outPos.fit * outPos;
    denom += this->_outPos.fit;
  }
  if (this->_outPosHigh.vote) {
    num += this->_outPosHigh.fit * outPosHigh;
    denom += this->_outPosHigh.fit;
  }

  if ( denom == 0.0) {
    _crispOutput = 0.0;
    return;
  }

  _crispOutput = num / denom;
}

unsigned char GrillFLC::doWork (void)
{
  float currentTemp = control.Probes[TEMP_PIT]->Temperature;
  this->_err = control.getSetPoint() - currentTemp;
  float cie = control.Probes[TEMP_PIT]->TemperatureAvg - currentTemp;
  calcExpMovingAverage(DERIV_FILTER_SMOOTH, &(this->_cie), cie);
  
  this->fuzzifyError();
  this->fuzzifyCIE();
  this->processRules();
  this->defuzzifyOutput();
  unsigned char retVal = _crispOutput;
  retVal += control.getControlOutput();
  retVal = constrain(retVal, 0, control.getMaxControlOutput() );

  return retVal;
}

void GrillFLC::setFlcTuning(unsigned char idx, float val)
{
  this->tuning[idx] = val;
}

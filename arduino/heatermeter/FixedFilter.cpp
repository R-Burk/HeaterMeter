/*
 * Copyright 2012-2013 BrewPi/Elco Jacobs.
 *
 * This file is part of BrewPi.
 * 
 * BrewPi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * BrewPi is distributed in the hope that it will be useful,
 * 
 * You should have received a copy of the GNU General Public License
 * along with BrewPi.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "FixedFilter.h"
#include <stdlib.h>

float FixedFilter::add(float val){
  if (yv[2] == 0) {
    init(val);
  }
    
  //Load from program memory into sram
  float b0 = pgm_read_float_near(&filter_coefficients[0]);
  float b1 = pgm_read_float_near(&filter_coefficients[1]);
  float b2 = pgm_read_float_near(&filter_coefficients[2]);
  float a1 = pgm_read_float_near(&filter_coefficients[3]);
  float a2 = pgm_read_float_near(&filter_coefficients[4]);

  //Shuffle left side history
	xv[2] = xv[1];
	xv[1] = xv[0];
	xv[0] = val;
  
  //Shuffle right side history
	yv[2] = yv[1];
	yv[1] = yv[0];

  //Left side of biquad
  yv[0]  = xv[2] * b2;
  yv[0] += xv[1] * b1;
  yv[0] += xv[0] * b0;
  
  //Right side of biquad
  yv[0] += yv[2] * a2;
  yv[0] += yv[1] * a1;
  
	return yv[0];
}

float FixedFilter::init(float val){
  xv[0] = val;
  xv[1] = val;
  xv[2] = val;

  yv[0] = val;
  yv[1] = val;
  yv[2] = val;

  return yv[0];
}


#ifndef __GRILLPID_CONF__
#define __GRILLPID_CONF__
// HeaterMeter Copyright 2016 Bryan Mayland <bmayland@capnbry.net>

#define GRILLPID_CALC_TEMP
#define GRILLPID_SERIAL_ENABLED
#define GRILLPID_SERVO_ENABLED
#define GRILLPID_DYNAMIC_RANGE
//#define GRILLPID_FEEDVOLT_DEBUG
#define FAN_30HZ
//#define FAN_20KHZ
//#define FAN_31KHZ
//#define GRILLPID_CUTBACK

#define UPSCALAR    10
#if defined(UPSCALAR)
#define UPSCALE(P)  (P * UPSCALAR)
#define DNSCALE(P)  ((unsigned char)(P / UPSCALAR))
#define MAP(x, in_min, in_max, out_min, out_max)   (((unsigned long)x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min)
#else
#define UPSCALE(P)  (P)
#define DNSCALE(P)  (P)
#endif

#define TEMP_PIT    0
#define TEMP_FOOD1  1
#define TEMP_FOOD2  2
#define TEMP_AMB    3
#define TEMP_CTRL   4 // Just a copy of one of the others
#define TEMP_COUNT  4

#define APIN_FFEEDBACK 1
#define PIN_BLOWER  3
#define PIN_SERVO   8

// Use oversample/decimation to increase ADC resolution to 2^(10+n) bits n=[0..4]
#define TEMP_OVERSAMPLE_BITS 4

// The time (ms) of the measurement period
#define TEMP_MEASURE_PERIOD 1000
// Number of times the ouput is adusted over TEMP_MEASURE_PERIOD
// This affects fan boost mode and FFEEDBACK output
#define TEMP_OUTADJUST_CNT 4
// 2/(1+Number of samples used in the exponential moving average)
// #define TEMPPROBE_AVG_SMOOTH (2.0f/(1.0f+60.0f))
// Using ewma for filtering the calculation for alpha is different
// alpha = cos(2*PI*fc / fs) - + sqrt(cos(2*pi*fc/fs)^2 - 4*cos(2*PI*fc/fs)+3)
// we are doing 1 sample/second so fs = 1
// alspha for a 30 second/cycle or .0333 HZ
#define TEMPPROBE_LPF_ALPHA 0.188344f
#define PIDOUTPUT_AVG_SMOOTH (2.0f/(1.0f+240.0f))
// Once entering LID OPEN mode, the minimum number of seconds to stay in
// LID OPEN mode before autoresuming due to temperature returning to setpoint
#define LIDOPEN_MIN_AUTORESUME 30

// Servo refresh period in usec, 20000 usec = 20ms = 50Hz
#define SERVO_REFRESH          20000
// Miniumum number of uS of difference in servo position to force immediate move
// undefine to force continuous servo operation
// #define SERVO_MIN_THRESH       50
// Max number of seconds to hold off a servo write due to being below threshold
#define SERVO_MAX_HOLDOFF      10
// Maximum number of usec to busy wait in the event handler to get more accurate
// timing of the servo pulse length
#define SERVO_BUSYWAIT         0

#define PROBE_NAME_SIZE 13

#if HM_BOARD_REV == 'A'
  #undef GRILLPID_SERVO_ENABLED
#endif

#endif /* __GRILLPID_CONF__ */

#pragma once
#include <hardware/clocks.h>
#include "hardware/pio.h"
#include <hardware/pwm.h>
#include <hardware/timer.h>

#define PIO_74HC595 pio0
#define PIO_CLOCK pio0

#define CLK_LATCH_595_BASE_PIN (PWM_LEFT_CHANNEL)
#define DATA_595_PIN (PWM_BEEPER)

#ifndef CLOCK_PIN
#define CLOCK_PIN 17
#endif
#define CLOCK_FREQUENCY (3579545)

//(14'318'180)


#define A0 (1 << 8)
#define A1 (1 << 9)

#define IC (1 << 10)

#define SN_1_CS (1 << 11)

#define SAA_1_CS (1 << 12)
#define SAA_2_CS (1 << 13)

#define OPL2 (1 << 14)
#define OPL3 (1 << 15)


void SN76489_write(uint8_t byte);
void SAA1099_write(uint8_t addr, uint8_t chip, uint8_t byte);
void OPL2_write_byte(uint16_t addr, uint16_t register_set, uint8_t byte);
void OPL3_write_byte(uint16_t addr, uint16_t register_set, uint8_t byte);
void reset_chips();
void init_74hc595();
void clock_init(uint pin, uint32_t frequency);

#ifndef SM16380SC_H
#define SM16380SC_H

#include <stdint.h>

/*
 * Two daisy-chained 64x32 panels form one 128x32 display. Each panel has
 * 24 SM16380SCs: four chips on each of the six HUB75 RGB data lanes. The two
 * panels therefore form an eight-chip serial chain on every lane. Two physical
 * rows are active together, so 32 physical rows require 16 scan addresses.
 */
#define SM16380SC_SCAN_ROWS       16u
#define SM16380SC_CHIPS_PER_LANE   8u
#define SM16380SC_PANEL_WIDTH     (SM16380SC_CHIPS_PER_LANE * 16u)

/* Conservative software-current setting; valid range is 0..63. */
#define SM16380SC_CURRENT_GAIN     63u
#define SM16380SC_FMPWM_MODE       3u

/* First-light diagnostic: every grayscale word is identical. */
#define SM16380SC_TEST_SOLID_RED    0u
#define SM16380SC_TEST_COLOR_BARS   1u
#define SM16380SC_TEST_TV_PATTERN   2u
#define SM16380SC_TEST_RAINBOW      3u
#define SM16380SC_TEST_PATTERN      SM16380SC_TEST_RAINBOW
#define SM16380SC_TEST_LEVEL        0x2000u

/* Reference waveform: 1024 / 2^FMPWM plus three row-transition clocks. */
#define SM16380SC_GCLK_PER_ROW \
    ((1024u >> SM16380SC_FMPWM_MODE) + 3u)

/* STM32F446 TIM1 has an 8-bit repetition counter: one burst is at most 256. */
#if SM16380SC_GCLK_PER_ROW > 256u
#error "Selected FMPWM mode needs more than one TIM1 RCR burst per row"
#endif

/*
 * NUCLEO-F446RE wiring copied from the working reference project. RGB/DCLK/LE
 * share GPIOC. Row A..E are contiguous on GPIOB. GCLK is PA8.
 *
 * HUB75  -> STM32 pin
 * CLK    -> PC4
 * LAT/LE -> PC5
 * R1     -> PC6
 * G1     -> PC7
 * B1     -> PC8
 * R2     -> PC9
 * G2     -> PC10
 * B2     -> PC11
 * OE     -> PA8  (SM16380SC GCLK; active pulse, not conventional blanking OE)
 * A      -> PB4
 * B      -> PB5
 * C      -> PB6
 * D      -> PB7
 * E      -> PB8
 */

typedef struct {
    uint16_t r1;
    uint16_t g1;
    uint16_t b1;
    uint16_t r2;
    uint16_t g2;
    uint16_t b2;
} SM16380SC_RGB6;

typedef struct {
    void (*setup_GCLK_timer)();
    void (*bus_delay)();
    void (*set_LE)();
    void (*reset_LE)();
    void (*set_DCLK)();
    void (*reset_DCLK)();
    void (*set_row)(unsigned);
    void (*write_rgb6)(const SM16380SC_RGB6, unsigned);
} SM16380SC_HW_Config;

void SM16380SC_Init(SM16380SC_HW_Config config);
void SM16380SC_UploadTestImage(void);
void SM16380SC_UploadMovingTestImage(unsigned frame);
void SM16380SC_RowPeriodElapsed(void);

#endif /* SM16380SC_H */

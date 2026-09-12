#include "sm16380sc.h"
#include <stdbool.h>

static SM16380SC_HW_Config hw_config;

enum {
    SM_CMD_VSYNC      = 3u,
    SM_CMD_CFG1       = 4u,
    SM_CMD_CFG5       = 6u,
    SM_CMD_CFG2       = 8u,
    SM_CMD_CFG7       = 11u,
    SM_CMD_CFG4       = 12u,
    SM_CMD_PREACTIVE  = 14u,
    SM_CMD_CFG6       = 15u,
    SM_CMD_CFG3       = 16u,
};

static inline void dclk_pulse(void)
{
    hw_config.set_DCLK();
    hw_config.bus_delay();
    hw_config.reset_DCLK();
    hw_config.bus_delay();
}

void clear_rgb6() {
    hw_config.write_rgb6(
        (SM16380SC_RGB6) {0},
        0
    );
}

/* LE is high for exactly the final le_tail_clocks rising DCLK edges. */
static void shift_gray6(const SM16380SC_RGB6 rgb, unsigned le_tail_clocks)
{
    for (int bit = 15; bit >= 0; --bit) {
        hw_config.write_rgb6(rgb, (unsigned)bit);

        if ((unsigned)bit < le_tail_clocks) {
            hw_config.set_LE();
        } else {
            hw_config.reset_LE();
        }

        hw_config.bus_delay();
        dclk_pulse();
    }

    hw_config.reset_LE();
    clear_rgb6();
    hw_config.bus_delay();
}

/* Command clocks are additional clocks and never overlap grayscale payload. */
static void send_command(unsigned le_high_clocks)
{
    hw_config.reset_LE();
    hw_config.reset_DCLK();
    clear_rgb6();
    hw_config.bus_delay();
    
    hw_config.set_LE();
    for (unsigned i = 0; i < le_high_clocks; ++i) {
        dclk_pulse();
    }
    hw_config.reset_LE();
    hw_config.bus_delay();
}

static uint16_t make_cfg1(void)
{
    return (uint16_t)(0x8000u |
        (SM16380SC_CURRENT_GAIN & 0x3fu) |
        (((SM16380SC_SCAN_ROWS - 1u) & 0x3fu) << 7u) |
        ((SM16380SC_FMPWM_MODE & 0x03u) << 13u));
}

static void broadcast_config(uint16_t red, uint16_t green, uint16_t blue,
                             unsigned selector)
{
    const SM16380SC_RGB6 value = {
        .r1 = red,   .g1 = green, .b1 = blue,
        .r2 = red,   .g2 = green, .b2 = blue,
    };

    for (unsigned chip = 0; chip < SM16380SC_CHIPS_PER_LANE; ++chip) {
        const bool final_chip = chip == (SM16380SC_CHIPS_PER_LANE - 1u);
        shift_gray6(value, final_chip ? selector : 0u);
    }
}

static void write_config(uint16_t red, uint16_t green, uint16_t blue,
                         unsigned selector)
{
    send_command(SM_CMD_PREACTIVE);
    broadcast_config(red, green, blue, selector);
}

static void configure_all_registers(void)
{
    const uint16_t cfg1 = make_cfg1();

    write_config(cfg1,   cfg1,   cfg1,   SM_CMD_CFG1);
    write_config(0x0001, 0x0001, 0x0001, SM_CMD_CFG2);
    write_config(0x10a3, 0x1463, 0x1063, SM_CMD_CFG3);
    write_config(0x0000, 0x0000, 0x0000, SM_CMD_CFG4);
    write_config(0x0000, 0x0000, 0x0000, SM_CMD_CFG5);
    write_config(0x0005, 0x0019, 0x002e, SM_CMD_CFG6);
    write_config(0x0000, 0x0000, 0x0000, SM_CMD_CFG7);
}

void SM16380SC_Init(SM16380SC_HW_Config config)
{
    hw_config = config;

    clear_rgb6();
    hw_config.set_row(0u);
    hw_config.reset_LE();
    hw_config.reset_DCLK();

    /*
     * TIM1 runs continuously. Its repetition counter makes one update event
     * after exactly SM16380SC_GCLK_PER_ROW PWM periods, not after every GCLK.
     * RCR stores N-1. Generate an update before starting so ARR, CCR and the
     * repetition-counter preload all begin from a known boundary.
     */
    hw_config.setup_GCLK_timer();

    /* Original SM16380/SM16380SC seven-register initialization. */
    send_command(SM_CMD_VSYNC);
    configure_all_registers();
}

/* Test images are generated directly from logical display coordinates. */
typedef struct {
    uint16_t r;
    uint16_t g;
    uint16_t b;
} TestRGB;

/* Image-space function: x=0..127, y=0..31. No chip/lane knowledge here. */
static TestRGB test_pixel(unsigned x, unsigned y)
{
#if SM16380SC_TEST_PATTERN == SM16380SC_TEST_SOLID_RED
    (void)x;
    (void)y;
    return (TestRGB) {
        .r = SM16380SC_TEST_LEVEL,
    };
#elif SM16380SC_TEST_PATTERN == SM16380SC_TEST_RAINBOW
    const uint16_t full = 0xf000u;
    const unsigned wheel = x * 6u;
    const unsigned segment = wheel / SM16380SC_PANEL_WIDTH;
    const uint16_t fade = (uint16_t)(((wheel % SM16380SC_PANEL_WIDTH) *
                                      (uint32_t)full) /
                                     SM16380SC_PANEL_WIDTH);
    const uint16_t rise = fade;
    const uint16_t fall = (uint16_t)(full - fade);
    uint16_t r = 0u, g = 0u, b = 0u;

    /* Red -> yellow -> green -> cyan -> blue -> magenta -> red. */
    switch (segment) {
    case 0u: r = full; g = rise;                         break;
    case 1u: r = fall; g = full;                         break;
    case 2u:           g = full; b = rise;               break;
    case 3u:           g = fall; b = full;               break;
    case 4u: r = rise;           b = full;               break;
    default: r = full;           b = fall;               break;
    }

    (void)y;
    return (TestRGB) { .r = r, .g = g, .b = b };
#elif SM16380SC_TEST_PATTERN == SM16380SC_TEST_TV_PATTERN
    const uint16_t full = 0xf000u;
    const uint16_t dim = 0x3000u;
    TestRGB pixel = {0};

    if (x == 0u || x == (SM16380SC_PANEL_WIDTH - 1u) ||
        y == 0u || y == 31u) {
        pixel.r = pixel.g = pixel.b = full;
    } else if (y < 20u) {
        const unsigned bar = (x * 7u) / SM16380SC_PANEL_WIDTH;
        static const uint8_t colors[7] = { 7u, 6u, 3u, 2u, 5u, 4u, 1u };
        const uint8_t color = colors[bar > 6u ? 6u : bar];
        if ((color & 4u) != 0u) pixel.r = full;
        if ((color & 2u) != 0u) pixel.g = full;
        if ((color & 1u) != 0u) pixel.b = full;
    } else if (y < 25u) {
        const uint16_t level = (uint16_t)(((x * 8u) /
            SM16380SC_PANEL_WIDTH) * 0x2000u);
        pixel.r = pixel.g = pixel.b = level;
    } else {
        if ((((x >> 2u) ^ (y >> 1u)) & 1u) != 0u) {
            pixel.r = pixel.g = pixel.b = dim;
        }
        if ((x & 15u) == 0u) {
            pixel.r = full;
            pixel.g = (x & 16u) != 0u ? full : 0u;
            pixel.b = (x & 32u) != 0u ? full : 0u;
        }
    }

    return pixel;
#else
    const uint16_t level = y < SM16380SC_SCAN_ROWS ? 0x2000u : 0x0800u;
    TestRGB pixel = {0};

    switch ((x / 16u) & 3u) {
    case 0u: pixel.r = level; break;
    case 1u: pixel.g = level; break;
    case 2u: pixel.b = level; break;
    default:
        pixel.r = level;
        pixel.g = level;
        break;
    }

    if (x == y) {
        pixel.r = pixel.g = pixel.b = 0x3000u;
    }

    return pixel;
#endif
}

static TestRGB test_animation_pixel(unsigned x, unsigned y, unsigned frame)
{
    const unsigned offset = frame % SM16380SC_PANEL_WIDTH;
    const unsigned source_x =
        (x + SM16380SC_PANEL_WIDTH - offset) % SM16380SC_PANEL_WIDTH;

    return test_pixel(source_x, y);
}



void SM16380SC_UploadTestImage(void)
{
    for (unsigned row = 0; row < SM16380SC_SCAN_ROWS; ++row) {
        for (unsigned out = 0; out < 16u; ++out) {
            /*
             * Match the proven driver: logical chip sections are serialized
             * in increasing x order. The panel's internal cascade performs
             * the physical shift; reversing here scrambles 16-pixel blocks.
             */
            for (unsigned slot = 0; slot < SM16380SC_CHIPS_PER_LANE; ++slot) {
                const unsigned chip = slot;
                const bool final_chip = slot == (SM16380SC_CHIPS_PER_LANE - 1u);
                const unsigned x = chip * 16u + out;
                const TestRGB top = test_pixel(x, row);
                const TestRGB bottom = test_pixel(
                    x, row + SM16380SC_SCAN_ROWS);
                const SM16380SC_RGB6 pixel = {
                    .r1 = top.r, .g1 = top.g, .b1 = top.b,
                    .r2 = bottom.r, .g2 = bottom.g, .b2 = bottom.b,
                };

                shift_gray6(pixel, final_chip ? 1u : 0u);
            }
        }
    }

    /* Match the working reference's post-upload commit/reconfigure sequence. */
    send_command(SM_CMD_VSYNC);
    configure_all_registers();
}

void SM16380SC_UploadMovingTestImage(unsigned frame)
{
    for (unsigned row = 0; row < SM16380SC_SCAN_ROWS; ++row) {
        for (unsigned out = 0; out < 16u; ++out) {
            /*
             * Match the proven driver: logical chip sections are serialized
             * in increasing x order. The panel's internal cascade performs
             * the physical shift; reversing here scrambles 16-pixel blocks.
             */
            for (unsigned slot = 0; slot < SM16380SC_CHIPS_PER_LANE; ++slot) {
                const unsigned chip = slot;
                const bool final_chip = slot == (SM16380SC_CHIPS_PER_LANE - 1u);
                const unsigned x = chip * 16u + out;
                const TestRGB top = test_animation_pixel(x, row, frame);
                const TestRGB bottom = test_animation_pixel(
                    x, row + SM16380SC_SCAN_ROWS, frame);
                const SM16380SC_RGB6 pixel = {
                    .r1 = top.r, .g1 = top.g, .b1 = top.b,
                    .r2 = bottom.r, .g2 = bottom.g, .b2 = bottom.b,
                };

                shift_gray6(pixel, final_chip ? 1u : 0u);
            }
        }
    }

    /* Match the working reference's post-upload commit/reconfigure sequence. */
    send_command(SM_CMD_VSYNC);
    configure_all_registers();
}

void SM16380SC_RowPeriodElapsed(void)
{
    static unsigned row = 0u;

    row++;
    if (row >= SM16380SC_SCAN_ROWS) {
        row = 0u;
    }
    hw_config.set_row(row);
}

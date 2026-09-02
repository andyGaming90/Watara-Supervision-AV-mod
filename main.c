#include <pico/multicore.h>
#include <pico/stdlib.h>
#include <hardware/vreg.h>
#include <hardware/clocks.h>
#include "lib/lcd.h"
#include "lib/supervision.h"
#include "ntsc-tv-out.h"
#include <string.h>

#define PALETTE 1

uint8_t render_buffer_index = 0;
__aligned(4) uint8_t NTSC_BUFFER[320 * 240] = {0};
__aligned(4) uint8_t NTSC_BUFFER2[320 * 240] = {0};

// LCD rendering loop.
// Runs on CPU core 1, separated from the data capture loop.
void __time_critical_func(render_core)()
{
    ntsc_init();
    memset(NTSC_BUFFER, 0, sizeof(NTSC_BUFFER));
    ntsc_present_framebuffer(NTSC_BUFFER);

    ntsc_set_color(0, 0x70, 0x70, 0xc0);
    ntsc_set_color(1, 0x78, 0x40, 0x80);
    ntsc_set_color(2, 0x60, 0x2e, 0x62);
    ntsc_set_color(3, 0x28, 0x0d, 0x30);
    ntsc_set_color(4, 0x00, 0x00, 0x00);

    uint8_t *next_ntsc_buffer = NTSC_BUFFER2;

    while (true)
    {
        if (sync == 1)
        {
            const uint8_t *src = get_framebuffer(render_buffer_index);
            uint8_t *dst = next_ntsc_buffer;

            for (int i = 0; i < 320 * 240; i++)
                dst[i] = 4;

            for (int y = 0; y < 220; y++)
            {
                for (int x = 0; x < 220; x++)
                {
                    int src_x = (x * 160) / 220;
                    int src_y = (y * 160) / 220;

                    dst[(y + 8) * 320 + (x + 58)] =
                        src[src_y * 160 + src_x] & 0x03;
                }
            }

            ntsc_present_framebuffer(dst);
            next_ntsc_buffer = (dst == NTSC_BUFFER) ? NTSC_BUFFER2 : NTSC_BUFFER;
            sync = 0;
        }
    }
}

// Capture LCD data from the Supervision.
// See doc file "supervision_tech-kevtris.org-kevin_horton.txt" for details on the Supervision LCD signals.
void __not_in_flash_func(capture_data)()
{
    uint8_t clock_high_count = 0;
    uint8_t last_polarity_state = 1;
    uint8_t polarity_stable_count = 0;

    uint8_t field = 0;
    uint32_t clock_count = 0;
    uint8_t *buffer = get_framebuffer(1);

    // Initialize GPIOs to which the Supervision is connected.
    supervision_gpio_init();

    // Load palette with Supervision colors.
    lcd_set_predefined_palette(PALETTE);

    // Wait for a new frame to start.
    while (gpio_get(SV_PIN_FRAME_POLARITY))
    {
    }
    while (!gpio_get(SV_PIN_FRAME_POLARITY))
    {
    }

    while (true)
    {
        const uint32_t bus = gpio_get_all();
        const uint8_t clock = (bus >> SV_PIN_PIXEL_CLOCK) & 1;
        const uint8_t polarity = (bus >> SV_PIN_FRAME_POLARITY) & 1;

        // Filter pixel clock.
        if (clock)
        {
            if (clock_high_count < 5)
                clock_high_count++;
        }
        else
        {
            clock_high_count = 0;
        }

        // Accept pixel clock after two consecutive high samples.
        // Never write more than 6400 groups into one field.
        if (clock_high_count == 2 &&
            polarity == last_polarity_state &&
            clock_count++ < 6400)
        {
            if (field == 0)
            {
                *buffer++ = (bus >> SV_PIN_DATA0) & 1;
                *buffer++ = (bus >> SV_PIN_DATA1) & 1;
                *buffer++ = (bus >> SV_PIN_DATA2) & 1;
                *buffer++ = (bus >> SV_PIN_DATA3) & 1;
            }
            else
            {
                *buffer++ += (bus >> SV_PIN_DATA0) & 1;
                *buffer++ += (bus >> SV_PIN_DATA1) & 1;
                *buffer++ += (bus >> SV_PIN_DATA2) & 1;
                *buffer++ += (bus >> SV_PIN_DATA3) & 1;
            }
        }

        // Filter frame polarity.
        if (polarity == last_polarity_state)
        {
            polarity_stable_count = 0;
        }

        if (polarity != last_polarity_state &&
            polarity_stable_count < 2)
        {
            polarity_stable_count++;
        }

        if (polarity != last_polarity_state &&
            polarity_stable_count == 2)
        {
            last_polarity_state = polarity;
            clock_high_count = 0;
            clock_count = 0;

            if (field == 2)
            {
                if (!sync)
                {
                    render_buffer_index = !render_buffer_index;
                    sync = 1;
                }

                field = 0;
            }
            else
            {
                field++;
            }

            buffer = get_framebuffer(!render_buffer_index);
        }
    }
}
void main()
{
    // Overclock to 240 MHz.
    vreg_set_voltage(VREG_VOLTAGE_1_20);
    sleep_ms(10);
    set_sys_clock_khz(240000, true);
    sleep_ms(10);

    // Start rendering process on other CPU core.
    // This displays the intro screen because it is in the initial framebuffer.
    sleep_ms(50);
    multicore_launch_core1(render_core);
    sleep_ms(1000);

    // Start capturing data from the Supervision.
    capture_data();
}
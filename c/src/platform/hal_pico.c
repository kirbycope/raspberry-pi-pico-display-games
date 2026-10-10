// The Pico Display Pack: an ST7789 240x135 IPS panel on SPI0, four buttons
// and an RGB LED. Pins, panel offsets and the init sequence follow Pimoroni's
// own driver (pimoroni-pico, drivers/st7789 and libraries/pico_display).
//
// The USB serial port doubles as a remote control, one command per line:
//   press <buttons>, release <buttons>, tap <buttons> [ms]   e.g. "tap A"
//   seed <n>    make the random numbers repeatable, as in the simulator
//   ping        answers "pong"
// Anything else goes to main.c (attract, open, menu). c/tools/remote.py
// drives it from a computer.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/spi.h"
#include "hardware/watchdog.h"
#include "pico/flash.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include "../core/hal.h"

#define PIN_DC 16
#define PIN_CS 17
#define PIN_SCK 18
#define PIN_MOSI 19
#define PIN_BL 20
#define PIN_LED_R 6
#define PIN_LED_G 7
#define PIN_LED_B 8
#define PIN_A 12
#define PIN_B 13
#define PIN_X 14
#define PIN_Y 15

#define SPI_BAUD 62500000

#define MADCTL_ROW_ORDER 0x80
#define MADCTL_COL_ORDER 0x40
#define MADCTL_SWAP_XY 0x20
#define MADCTL_SCAN_ORDER 0x10

static int dma_ch = -1;
static bool transfer_open;
static bool saves_ok;

static void command(uint8_t cmd, size_t len, const uint8_t *data) {
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(spi0, &cmd, 1);
    if (data) {
        gpio_put(PIN_DC, 1);
        spi_write_blocking(spi0, data, len);
    }
    gpio_put(PIN_CS, 1);
}

#define CMD(c, ...) command(c, sizeof((const uint8_t[]){__VA_ARGS__}), (const uint8_t[]){__VA_ARGS__})

static void pwm_pin(uint pin) {
    pwm_config cfg = pwm_get_default_config();
    pwm_set_wrap(pwm_gpio_to_slice_num(pin), 65535);
    pwm_init(pwm_gpio_to_slice_num(pin), &cfg, true);
    gpio_set_function(pin, GPIO_FUNC_PWM);
}

// Gamma 2.8, the curve Pimoroni uses for both the backlight and the LED.
static uint16_t gamma16(uint8_t v) {
    static uint16_t table[256];
    static bool built;
    if (!built) {
        for (int i = 0; i < 256; i++) table[i] = (uint16_t)(powf((float)i / 255.0f, 2.8f) * 65535.0f + 0.5f);
        built = true;
    }
    return table[v];
}

static void button_pin(uint pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_up(pin);
}

static void panel_init(void) {
    command(0x01, 0, NULL); // SWRESET
    sleep_ms(150);
    command(0x35, 0, NULL);  // TEON
    CMD(0x3A, 0x05);         // COLMOD: 16 bits per pixel
    CMD(0xB2, 0x0c, 0x0c, 0x00, 0x33, 0x33); // PORCTRL
    CMD(0xC0, 0x2c);         // LCMCTRL
    CMD(0xC2, 0x01);         // VDVVRHEN
    CMD(0xC3, 0x12);         // VRHS
    CMD(0xC4, 0x20);         // VDVS
    CMD(0xD0, 0xa4, 0xa1);   // PWCTRL1
    CMD(0xC6, 0x0f);         // FRCTRL2
    CMD(0xB0, 0x00, 0xc0);   // RAMCTRL, avoids grey banding at low green
    // Settings for the 240x135 Display Pack panel.
    CMD(0xC3, 0x00);
    CMD(0xB7, 0x75);
    CMD(0xBB, 0x3D);
    CMD(0xD6, 0xa1);
    CMD(0xE0, 0x70, 0x04, 0x08, 0x09, 0x09, 0x05, 0x2A, 0x33, 0x41, 0x07, 0x13, 0x13, 0x29, 0x2f);
    CMD(0xE1, 0x70, 0x03, 0x09, 0x0A, 0x09, 0x06, 0x2B, 0x34, 0x41, 0x07, 0x12, 0x14, 0x28, 0x2E);
    command(0x21, 0, NULL); // INVON
    command(0x11, 0, NULL); // SLPOUT
    command(0x29, 0, NULL); // DISPON
    sleep_ms(100);
}

void hal_init(void) {
    stdio_init_all();

    spi_init(spi0, SPI_BAUD);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_DC);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);

    pwm_pin(PIN_BL);
    hal_backlight(0); // dark until the first frame has been sent
    pwm_pin(PIN_LED_R);
    pwm_pin(PIN_LED_G);
    pwm_pin(PIN_LED_B);
    hal_led(0, 0, 0);

    button_pin(PIN_A);
    button_pin(PIN_B);
    button_pin(PIN_X);
    button_pin(PIN_Y);

    panel_init();
    hal_set_orientation(ORIENT_LANDSCAPE, false);
    gfx_clear(C_BLACK);
    hal_present();
    hal_wait_present();
    hal_backlight(255);

    dma_ch = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config((uint)dma_ch);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_dreq(&c, spi_get_dreq(spi0, true));
    dma_channel_configure((uint)dma_ch, &c, &spi_get_hw(spi0)->dr, gfx_fb, 0, false);

    // The save sector is the last one in flash; a build that reaches into it
    // (a very large Bad Apple encode) runs without saving rather than
    // overwriting its own data.
    extern char __flash_binary_end;
    saves_ok = (uintptr_t)&__flash_binary_end <= XIP_BASE + PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE;
}

static uint8_t remote_down, remote_tap_mask;
static uint32_t remote_tap_until;
static bool seeded;
static uint32_t seed_state;
static char line[96], pending[96], handed_out[96];
static int line_len;
static bool has_pending;

static uint8_t parse_buttons(const char *s) {
    uint8_t b = 0;
    for (; *s; s++) switch (toupper((unsigned char)*s)) {
        case 'A': b |= BTN_A; break;
        case 'B': b |= BTN_B; break;
        case 'X': b |= BTN_X; break;
        case 'Y': b |= BTN_Y; break;
        }
    return b;
}

static void remote_line(const char *l) {
    char cmd[16] = {0}, arg[64] = {0};
    unsigned n = 0;
    if (sscanf(l, "%15s %63s %u", cmd, arg, &n) < 1) return;
    if (!strcmp(cmd, "press") || !strcmp(cmd, "hold")) remote_down |= parse_buttons(arg);
    else if (!strcmp(cmd, "release")) remote_down &= (uint8_t)~parse_buttons(arg);
    else if (!strcmp(cmd, "tap")) {
        remote_down |= parse_buttons(arg);
        remote_tap_mask |= parse_buttons(arg);
        remote_tap_until = hal_millis() + (n ? n : 80);
    } else if (!strcmp(cmd, "seed")) {
        seeded = true;
        seed_state = (uint32_t)strtoul(arg, NULL, 0);
        if (!seed_state) seed_state = 1;
    } else if (!strcmp(cmd, "ping")) {
        hal_remote_reply("pong");
        return;
    } else {
        snprintf(pending, sizeof pending, "%s", l);
        has_pending = true;
        return;
    }
    hal_remote_reply("ok");
}

static void remote_poll(void) {
    int c;
    while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            if (line_len) {
                line[line_len] = 0;
                remote_line(line);
                line_len = 0;
            }
        } else if (line_len < (int)sizeof line - 1) line[line_len++] = (char)c;
    }
    if (remote_tap_mask && (int32_t)(hal_millis() - remote_tap_until) >= 0) {
        remote_down &= (uint8_t)~remote_tap_mask;
        remote_tap_mask = 0;
    }
}

const char *hal_remote_command(void) {
    remote_poll();
    if (!has_pending) return NULL;
    has_pending = false;
    memcpy(handed_out, pending, sizeof handed_out);
    return handed_out;
}

void hal_remote_reply(const char *l) {
    printf("%s\n", l);
}

bool hal_running(void) { return true; }
uint32_t hal_millis(void) { return to_ms_since_boot(get_absolute_time()); }
void hal_sleep_ms(uint32_t ms) { sleep_ms(ms); }
uint32_t hal_random(void) {
    if (!seeded) return get_rand_32();
    // The simulator's xorshift, so a seeded run matches it.
    seed_state ^= seed_state << 13;
    seed_state ^= seed_state >> 17;
    seed_state ^= seed_state << 5;
    return seed_state;
}
void hal_reset(void) { watchdog_reboot(0, 0, 0); for (;;) tight_loop_contents(); }

uint8_t hal_buttons(void) {
    remote_poll();
    return hal_physical_buttons() | remote_down;
}

uint8_t hal_physical_buttons(void) {
    uint8_t b = 0;
    if (!gpio_get(PIN_A)) b |= BTN_A;
    if (!gpio_get(PIN_B)) b |= BTN_B;
    if (!gpio_get(PIN_X)) b |= BTN_X;
    if (!gpio_get(PIN_Y)) b |= BTN_Y;
    return b;
}

void hal_led(uint8_t r, uint8_t g, uint8_t b) {
    // The LED is wired active low.
    pwm_set_gpio_level(PIN_LED_R, (uint16_t)(65535 - gamma16(r)));
    pwm_set_gpio_level(PIN_LED_G, (uint16_t)(65535 - gamma16(g)));
    pwm_set_gpio_level(PIN_LED_B, (uint16_t)(65535 - gamma16(b)));
}

void hal_backlight(uint8_t level) { pwm_set_gpio_level(PIN_BL, gamma16(level)); }

void hal_set_orientation(orient_t o, bool flip) {
    hal_wait_present();
    uint16_t x0, x1, y0, y1;
    uint8_t madctl;
    if (o == ORIENT_LANDSCAPE) {
        x0 = 40, x1 = 40 + SCREEN_LONG - 1, y0 = 53, y1 = 53 + SCREEN_SHORT - 1;
        madctl = MADCTL_COL_ORDER | MADCTL_SWAP_XY | MADCTL_SCAN_ORDER;
    } else if (!flip) {
        // USB at the top, B and A above the screen, Y and X below it.
        x0 = 52, x1 = 52 + SCREEN_SHORT - 1, y0 = 40, y1 = 40 + SCREEN_LONG - 1;
        madctl = 0;
    } else {
        x0 = 53, x1 = 53 + SCREEN_SHORT - 1, y0 = 40, y1 = 40 + SCREEN_LONG - 1;
        madctl = MADCTL_COL_ORDER | MADCTL_ROW_ORDER;
    }
    CMD(0x2A, (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1); // CASET
    CMD(0x2B, (uint8_t)(y0 >> 8), (uint8_t)y0, (uint8_t)(y1 >> 8), (uint8_t)y1); // RASET
    command(0x36, 1, &madctl);
}

void hal_present(void) {
    hal_wait_present();
    uint8_t cmd = 0x2C; // RAMWR
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_CS, 0);
    spi_write_blocking(spi0, &cmd, 1);
    gpio_put(PIN_DC, 1);
    if (dma_ch < 0) {
        // Before DMA is set up (the first frame of hal_init) send it directly.
        // The panel wants each pixel high byte first.
        for (int i = 0; i < SCREEN_LONG * SCREEN_SHORT; i++) {
            uint8_t px[2] = {(uint8_t)(gfx_fb[i] >> 8), (uint8_t)gfx_fb[i]};
            spi_write_blocking(spi0, px, 2);
        }
        gpio_put(PIN_CS, 1);
        return;
    }
    // In 16-bit frames the SPI sends each halfword high byte first, which is
    // the order the panel wants, so the buffer goes out untouched.
    spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    dma_channel_set_read_addr((uint)dma_ch, gfx_fb, false);
    dma_channel_set_trans_count((uint)dma_ch, SCREEN_LONG * SCREEN_SHORT, true);
    transfer_open = true;
}

void hal_wait_present(void) {
    if (!transfer_open) return;
    dma_channel_wait_for_finish_blocking((uint)dma_ch);
    while (spi_is_busy(spi0)) tight_loop_contents();
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_put(PIN_CS, 1);
    transfer_open = false;
}

#define SAVE_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)

bool hal_load(void *data, size_t len) {
    if (!saves_ok || len > HAL_SAVE_SIZE) return false;
    memcpy(data, (const void *)(XIP_BASE + SAVE_OFFSET), len);
    return true;
}

static uint8_t save_page[HAL_SAVE_SIZE];

static void flash_write(void *param) {
    (void)param;
    flash_range_erase(SAVE_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(SAVE_OFFSET, save_page, HAL_SAVE_SIZE);
}

bool hal_store(const void *data, size_t len) {
    if (!saves_ok || len > HAL_SAVE_SIZE) return false;
    hal_wait_present();
    memset(save_page, 0xFF, sizeof save_page);
    memcpy(save_page, data, len);
    return flash_safe_execute(flash_write, NULL, 500) == PICO_OK;
}

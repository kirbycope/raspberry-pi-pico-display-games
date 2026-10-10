// A desktop stand-in for the Display Pack.
//
// Interactive: an SDL2 window at three times size; the A, B, X and Y keys are
// the buttons and the strip under the screen shows the RGB LED.
//
// Scripted: PDG_SCRIPT=<file> runs headless on a virtual clock, so a run is
// exact and as fast as the machine allows. One command per line:
//   wait <ms>            let the app run
//   hold <buttons>       press and keep down, e.g. "hold AB"
//   release <buttons>
//   tap <buttons> [ms]   press for ms (default 80) then release
//   shot <file.bmp>      save the screen as it was last presented
//   quit
// PDG_APP=<name> opens that app at start, PDG_SAVE=<file> keeps the save data
// in a file (scripted runs otherwise start from nothing), PDG_SEED=<n> seeds
// the random numbers, PDG_SHOT_SCALE=<n> enlarges the screenshots.
// PDG_RECORD=<file> appends every presented frame to a file for the trailer
// tools: per frame a 16-byte header (ms, width, height, buttons, LED r g b)
// then width*height RGB565 pixels, little-endian. PDG_RECORD_FROM and
// PDG_RECORD_TO (ms) limit it to a window and PDG_RECORD_EVERY=<n> keeps
// every nth frame, for a quick look over a long run.
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../core/hal.h"

#ifdef PDG_SDL
#include <SDL.h>
#endif

#define SCALE 3
#define LED_STRIP 6

static bool running = true;
static uint8_t keys_down, script_down;
static uint8_t led[3];
static orient_t cur_orient = ORIENT_LANDSCAPE;
static uint32_t rng_state = 0x12345678u;

static FILE *script;
static uint32_t virtual_ms, script_next_ms;
static uint8_t script_release_mask;
static uint32_t script_release_at;
static int shot_scale = 1;
static FILE *record;
static uint32_t record_from, record_to = 0xFFFFFFFFu, record_every = 1, record_count;
static uint8_t presented[SCREEN_LONG * SCREEN_SHORT * 2];
static int presented_w = SCREEN_LONG, presented_h = SCREEN_SHORT;

static const char *save_path;
static uint8_t memory_save[HAL_SAVE_SIZE];
static bool memory_save_valid;

#ifdef PDG_SDL
static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
#endif

const char *sim_start_app(void) { return getenv("PDG_APP"); }

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

static void write_bmp(const char *path) {
    int w = presented_w * shot_scale, h = presented_h * shot_scale;
    int stride = (w * 3 + 3) & ~3;
    uint32_t size = 54 + (uint32_t)(stride * h);
    uint8_t hdr[54] = {'B', 'M'};
    uint32_t v[] = {size, 0, 54, 40, (uint32_t)w, (uint32_t)h};
    memcpy(hdr + 2, &v[0], 4);
    memcpy(hdr + 10, &v[2], 4);
    memcpy(hdr + 14, &v[3], 4);
    memcpy(hdr + 18, &v[4], 4);
    memcpy(hdr + 22, &v[5], 4);
    hdr[26] = 1;
    hdr[28] = 24;
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "sim: cannot write %s\n", path); return; }
    fwrite(hdr, 1, 54, f);
    uint8_t *row = calloc(1, (size_t)stride);
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            int sx = x / shot_scale, sy = y / shot_scale;
            uint16_t c = (uint16_t)(presented[(sy * presented_w + sx) * 2] | presented[(sy * presented_w + sx) * 2 + 1] << 8);
            row[x * 3 + 0] = rgb_b(c);
            row[x * 3 + 1] = rgb_g(c);
            row[x * 3 + 2] = rgb_r(c);
        }
        fwrite(row, 1, (size_t)stride, f);
    }
    free(row);
    fclose(f);
    printf("sim: %6u ms shot %s led=%d,%d,%d\n", virtual_ms, path, led[0], led[1], led[2]);
}

// Runs script commands until one asks for time to pass.
static void script_step(void) {
    if (script_release_mask && virtual_ms >= script_release_at) {
        script_down &= (uint8_t)~script_release_mask;
        script_release_mask = 0;
        // Let at least one frame see the button up, so two taps in a row are
        // two presses rather than one long one.
        if (script_next_ms <= virtual_ms) script_next_ms = virtual_ms + 1;
    }
    char line[512];
    while (running && virtual_ms >= script_next_ms && !script_release_mask) {
        if (!fgets(line, sizeof line, script)) { running = false; break; }
        char cmd[32] = {0}, arg[480] = {0};
        unsigned n = 0;
        if (sscanf(line, "%31s %479s %u", cmd, arg, &n) < 1 || cmd[0] == '#') continue;
        if (!strcmp(cmd, "wait")) script_next_ms = virtual_ms + (uint32_t)atoi(arg);
        else if (!strcmp(cmd, "hold")) script_down |= parse_buttons(arg);
        else if (!strcmp(cmd, "release")) script_down &= (uint8_t)~parse_buttons(arg);
        else if (!strcmp(cmd, "tap")) {
            script_down |= parse_buttons(arg);
            script_release_mask = parse_buttons(arg);
            script_release_at = virtual_ms + (n ? n : 80);
            script_next_ms = script_release_at;
        } else if (!strcmp(cmd, "shot")) write_bmp(arg);
        else if (!strcmp(cmd, "quit")) running = false;
        else fprintf(stderr, "sim: unknown command %s\n", cmd);
    }
}

#ifdef PDG_SDL
static void window_resize(void) {
    int w = (cur_orient == ORIENT_LANDSCAPE ? SCREEN_LONG : SCREEN_SHORT) * SCALE;
    int h = (cur_orient == ORIENT_LANDSCAPE ? SCREEN_SHORT : SCREEN_LONG) * SCALE;
    SDL_SetWindowSize(window, w, h + LED_STRIP * SCALE);
    if (texture) SDL_DestroyTexture(texture);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w / SCALE, h / SCALE);
}

static void pump_events(void) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) running = false;
        if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
            uint8_t b = 0;
            switch (e.key.keysym.sym) {
            case SDLK_a: b = BTN_A; break;
            case SDLK_b: b = BTN_B; break;
            case SDLK_x: b = BTN_X; break;
            case SDLK_y: b = BTN_Y; break;
            case SDLK_ESCAPE: running = false; break;
            }
            if (e.type == SDL_KEYDOWN) keys_down |= b;
            else keys_down &= (uint8_t)~b;
        }
    }
}
#endif

void hal_init(void) {
    const char *seed = getenv("PDG_SEED");
    const char *path = getenv("PDG_SCRIPT");
    const char *scale = getenv("PDG_SHOT_SCALE");
    const char *rec = getenv("PDG_RECORD");
    if (rec && !(record = fopen(rec, "wb"))) { fprintf(stderr, "sim: cannot write %s\n", rec); exit(1); }
    if (getenv("PDG_RECORD_FROM")) record_from = (uint32_t)strtoul(getenv("PDG_RECORD_FROM"), NULL, 0);
    if (getenv("PDG_RECORD_TO")) record_to = (uint32_t)strtoul(getenv("PDG_RECORD_TO"), NULL, 0);
    if (getenv("PDG_RECORD_EVERY")) record_every = (uint32_t)strtoul(getenv("PDG_RECORD_EVERY"), NULL, 0);
    if (!record_every) record_every = 1;
    if (scale) shot_scale = atoi(scale) > 0 ? atoi(scale) : 1;
    save_path = getenv("PDG_SAVE");
    if (path) {
        script = fopen(path, "r");
        if (!script) { fprintf(stderr, "sim: cannot open script %s\n", path); exit(1); }
        rng_state = seed ? (uint32_t)strtoul(seed, NULL, 0) : 1u;
        return;
    }
    rng_state = seed ? (uint32_t)strtoul(seed, NULL, 0) : (uint32_t)time(NULL);
    if (!save_path) save_path = "pdg_save.bin";
#ifdef PDG_SDL
    SDL_Init(SDL_INIT_VIDEO);
    window = SDL_CreateWindow("Pico Display Games", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              SCREEN_LONG * SCALE, (SCREEN_SHORT + LED_STRIP) * SCALE, 0);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    window_resize();
#else
    fprintf(stderr, "sim: built without SDL2, so only PDG_SCRIPT runs work\n");
    exit(1);
#endif
}

bool hal_running(void) { return running; }

uint32_t hal_millis(void) {
    if (script) return virtual_ms;
#ifdef PDG_SDL
    return SDL_GetTicks();
#else
    return 0;
#endif
}

void hal_sleep_ms(uint32_t ms) {
    if (script) {
        // Advance in small steps so taps and waits land between frames.
        uint32_t end = virtual_ms + ms;
        while (virtual_ms < end && running) {
            uint32_t step = end - virtual_ms;
            virtual_ms += step;
            script_step();
        }
        if (ms == 0) script_step();
        return;
    }
#ifdef PDG_SDL
    pump_events();
    SDL_Delay(ms);
#endif
}

uint8_t hal_buttons(void) {
    if (script) {
        script_step();
        return script_down;
    }
#ifdef PDG_SDL
    pump_events();
#endif
    return keys_down;
}

void hal_led(uint8_t r, uint8_t g, uint8_t b) { led[0] = r, led[1] = g, led[2] = b; }
void hal_backlight(uint8_t level) { (void)level; }
uint32_t hal_random(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
void hal_reset(void) { running = false; }

void hal_set_orientation(orient_t o, bool flip) {
    (void)flip;
    if (o == cur_orient) return;
    cur_orient = o;
#ifdef PDG_SDL
    if (window) window_resize();
#endif
}

static void record_frame(void) {
    uint8_t buttons = script ? script_down : keys_down;
    uint32_t ms = hal_millis();
    if (ms < record_from || ms > record_to || record_count++ % record_every) return;
    uint8_t hdr[16] = {(uint8_t)ms, (uint8_t)(ms >> 8), (uint8_t)(ms >> 16), (uint8_t)(ms >> 24),
                       (uint8_t)gfx_w, (uint8_t)(gfx_w >> 8), (uint8_t)gfx_h, (uint8_t)(gfx_h >> 8),
                       buttons, led[0], led[1], led[2]};
    fwrite(hdr, 1, sizeof hdr, record);
    fwrite(gfx_fb, 2, (size_t)(gfx_w * gfx_h), record);
}

void hal_present(void) {
    if (record) record_frame();
    presented_w = gfx_w;
    presented_h = gfx_h;
    memcpy(presented, gfx_fb, sizeof presented);
#ifdef PDG_SDL
    if (!window) return;
    SDL_UpdateTexture(texture, NULL, gfx_fb, gfx_w * 2);
    SDL_SetRenderDrawColor(renderer, led[0], led[1], led[2], 255);
    SDL_RenderClear(renderer);
    SDL_Rect dst = {0, 0, gfx_w * SCALE, gfx_h * SCALE};
    SDL_RenderCopy(renderer, texture, NULL, &dst);
    SDL_RenderPresent(renderer);
#endif
}

void hal_wait_present(void) {}

bool hal_load(void *data, size_t len) {
    if (!save_path) {
        if (memory_save_valid) memcpy(data, memory_save, len);
        return memory_save_valid;
    }
    FILE *f = fopen(save_path, "rb");
    if (!f) return false;
    size_t n = fread(data, 1, len, f);
    fclose(f);
    return n == len;
}

bool hal_store(const void *data, size_t len) {
    if (!save_path) {
        memcpy(memory_save, data, len);
        memory_save_valid = true;
        return true;
    }
    FILE *f = fopen(save_path, "wb");
    if (!f) return false;
    fwrite(data, 1, len, f);
    fclose(f);
    return true;
}

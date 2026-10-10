// Pico Quest's procedural art: the three zone backdrops, the camp, the
// framed panels and a sprite drawer that scales in quarter steps.
#include "../core/gfx.h"
#include "rpg.h"

uint32_t rpg_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint16_t rpg_mix(uint16_t a, uint16_t b, int alpha) {
    if (alpha <= 0) return a;
    if (alpha >= 32) return b;
    uint32_t bg = (a | ((uint32_t)a << 16)) & 0x07E0F81Fu;
    uint32_t fg = (b | ((uint32_t)b << 16)) & 0x07E0F81Fu;
    bg += ((fg - bg) * (uint32_t)alpha) >> 5;
    bg &= 0x07E0F81Fu;
    return (uint16_t)(bg | (bg >> 16));
}

static void blend_px(int x, int y, uint16_t c, int alpha) {
    if ((unsigned)x < (unsigned)gfx_w && (unsigned)y < (unsigned)gfx_h)
        gfx_fb[y * gfx_w + x] = rpg_mix(gfx_fb[y * gfx_w + x], c, alpha);
}

static void blend_rows(int y0, int h, uint16_t c, int alpha) {
    for (int y = y0; y < y0 + h; y++)
        for (int x = 0; x < gfx_w; x++) blend_px(x, y, c, alpha);
}

void rpg_glow(int cx, int cy, int r, uint16_t c, int alpha) {
    int rr = r * r;
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++) {
            int d = x * x + y * y;
            if (d < rr) blend_px(cx + x, cy + y, c, alpha * (rr - d) / rr);
        }
}

// Triangle wave, -16..16 over a period of 64.
static int tri(int x) {
    x &= 63;
    return x < 32 ? x - 16 : 48 - x;
}

// ---- forest ----

static void cloud(int x, int y) {
    const uint16_t shade = rgb(205, 222, 240), white = rgb(250, 252, 255);
    gfx_circle(x + 1, y + 2, 6, shade);
    gfx_circle(x + 10, y, 8, shade);
    gfx_circle(x + 19, y + 2, 6, shade);
    gfx_circle(x, y, 6, white);
    gfx_circle(x + 9, y - 2, 8, white);
    gfx_circle(x + 18, y, 5, white);
    gfx_rect(x - 6, y + 3, 32, 6, shade);
    gfx_rect(x - 5, y + 2, 29, 3, white);
}

// A leafy tree: trunk, then canopy shadows, body and sunlit highlights.
static void tree(int cx, int base, int size, uint32_t seed) {
    const uint16_t trunk = rgb(116, 74, 44), trunk_dark = rgb(78, 48, 32), trunk_light = rgb(150, 100, 60);
    const uint16_t leaf_dark = rgb(28, 92, 52), leaf = rgb(52, 140, 62), leaf_light = rgb(112, 196, 78),
                   leaf_glint = rgb(168, 226, 104);
    int cy = base - size * 2;
    int tw = size / 3 < 3 ? 3 : size / 3;
    gfx_rect(cx - tw / 2, cy, tw, base - cy, trunk);
    gfx_rect(cx + tw / 2 - tw / 3, cy, tw / 3 + 1, base - cy, trunk_dark);
    gfx_vline(cx - tw / 2 + 1, cy, base - cy, trunk_light);
    // roots
    gfx_hline(cx - tw / 2 - 2, base - 1, tw + 4, trunk_dark);
    gfx_hline(cx - tw / 2 - 1, base - 2, tw + 2, trunk);

    gfx_circle(cx, cy + size / 3, size, leaf_dark);
    gfx_circle(cx - size * 3 / 4, cy + size * 2 / 3, size * 2 / 3, leaf_dark);
    gfx_circle(cx + size * 3 / 4, cy + size * 2 / 3, size * 2 / 3, leaf_dark);
    gfx_circle(cx - size / 5, cy, size * 4 / 5, leaf);
    gfx_circle(cx - size * 2 / 3, cy + size / 2, size / 2, leaf);
    gfx_circle(cx + size / 2, cy + size / 4, size / 2, leaf);
    gfx_circle(cx - size / 3, cy - size / 3, size / 2, leaf_light);
    gfx_circle(cx + size / 3, cy - size / 6, size / 4, leaf_light);
    // Dithered leaf texture.
    for (int i = 0; i < size * 3; i++) {
        uint32_t h = rpg_hash(seed * 977u + (uint32_t)i);
        int x = cx - size + (int)(h % (uint32_t)(2 * size));
        int y = cy - size + (int)((h >> 8) % (uint32_t)(2 * size));
        uint16_t at = gfx_get(x, y);
        if (at == leaf_light) gfx_pixel(x, y, leaf_glint);
        else if (at == leaf) gfx_pixel(x, y, leaf_light), gfx_pixel(x + 1, y + 1, leaf_dark);
        else if (at == leaf_dark) gfx_pixel(x, y, leaf);
    }
}

static void forest(int y0, int h, uint32_t t) {
    int hz = y0 + h * 46 / 100;
    gfx_vgradient(0, y0, gfx_w, hz - y0 + 4, rgb(96, 168, 240), rgb(206, 236, 250));
    // Clouds drift slowly.
    int drift = (int)(t / 180u);
    cloud((20 + drift) % 190 - 30, y0 + 12);
    cloud((120 + drift * 2 / 3) % 190 - 30, y0 + 24);

    // Two far tree lines, hazy blue-green then deeper.
    for (int i = 0; i < 14; i++) {
        int r = 5 + (int)(rpg_hash((uint32_t)i + 11u) % 5u);
        gfx_circle(i * 11 - 3, hz - 2, r, rgb(118, 176, 160));
    }
    gfx_rect(0, hz - 2, gfx_w, 6, rgb(118, 176, 160));
    for (int i = 0; i < 11; i++) {
        int r = 7 + (int)(rpg_hash((uint32_t)i + 40u) % 5u);
        gfx_circle(i * 14 + 4, hz + 5, r, rgb(62, 128, 96));
    }
    gfx_rect(0, hz + 5, gfx_w, 6, rgb(62, 128, 96));

    // Grass, darker towards the trees, with a dithered seam.
    int gy = hz + 10, bottom = y0 + h;
    const uint16_t g_dark = rgb(56, 132, 56), g = rgb(84, 172, 66), g_light = rgb(126, 204, 84);
    gfx_rect(0, gy, gfx_w, bottom - gy, g);
    for (int y = gy; y < gy + 8; y++)
        for (int x = 0; x < gfx_w; x++) {
            int band = y - gy;
            if (band < 3 ? ((x + y) & 1) == 0 || band == 0 : (band < 6 ? ((x + 2 * y) & 3) == 0 : (x * 7 + y) % 9 == 0))
                gfx_pixel(x, y, g_dark);
        }
    // A sunlit dirt clearing for the monster to stand in.
    int cx = gfx_w / 2, cy = bottom - 11;
    for (int y = -10; y <= 10; y++)
        for (int x = -60; x <= 60; x++) {
            int d = x * x * 100 / (60 * 60) + y * y * 100 / (10 * 10);
            if (d > 100) continue;
            uint16_t c = d > 70 ? (((x + y) & 1) ? rgb(150, 160, 70) : g) : d > 40 ? rgb(176, 148, 92) : rgb(194, 166, 108);
            if (d <= 40 && ((x * 3 + y * 5) % 11 == 0)) c = rgb(158, 128, 80);
            gfx_pixel(cx + x, cy + y, c);
        }
    // Tufts and flowers.
    for (int i = 0; i < 46; i++) {
        uint32_t r = rpg_hash((uint32_t)i * 31u + 7u);
        int x = (int)(r % (uint32_t)gfx_w), y = gy + 9 + (int)((r >> 8) % (uint32_t)(bottom - gy - 10));
        int dx = x - cx, dy = y - cy;
        if (dx * dx * 100 / 3600 + dy * dy * 100 / 100 <= 100) continue;
        if (i % 5 == 0) {
            static const uint16_t petals[] = {rgb(250, 90, 90), rgb(255, 230, 90), rgb(250, 250, 250), rgb(200, 120, 250)};
            gfx_pixel(x, y + 1, g_dark);
            gfx_pixel(x, y, petals[(r >> 20) & 3]);
            gfx_pixel(x - 1, y, petals[(r >> 20) & 3]);
            gfx_pixel(x + 1, y, petals[(r >> 20) & 3]);
            gfx_pixel(x, y - 1, petals[(r >> 20) & 3]);
            gfx_pixel(x, y, rgb(255, 240, 160));
        } else {
            gfx_pixel(x, y, g_dark);
            gfx_pixel(x - 1, y - 1, g_dark);
            gfx_pixel(x + 1, y - 2, g_light);
            gfx_pixel(x + 1, y - 1, g_light);
        }
    }
    // Trees framing the clearing, small ones behind, big ones in front.
    tree(16, hz + 14, 8, 1);
    tree(118, hz + 13, 9, 2);
    tree(-2, bottom - 18, 17, 3);
    tree(138, bottom - 22, 16, 4);
}

// ---- ruins ----

static void pillar(int x, int w, int top, int bottom, uint32_t seed) {
    const uint16_t stone = rgb(176, 164, 156), dark = rgb(118, 106, 112), light = rgb(222, 212, 198),
                   mortar = rgb(130, 118, 120), moss = rgb(92, 140, 70);
    for (int i = 0; i < w; i++) {
        // A broken, jagged top.
        int jag = (int)(rpg_hash(seed * 131u + (uint32_t)(i / 2)) % 7u);
        int y = top + jag;
        uint16_t c = i < 2 ? light : i >= w - 4 ? dark : stone;
        gfx_vline(x + i, y, bottom - y, c);
        gfx_pixel(x + i, y, i >= w - 4 ? mortar : light);
    }
    for (int y = top + 12; y < bottom - 6; y += 11) {
        gfx_hline(x, y, w, mortar);
        gfx_hline(x, y + 1, w - 4, light);
    }
    // Fluting.
    for (int i = 4; i < w - 4; i += 4) gfx_vline(x + i, top + 8, bottom - top - 14, rgb(150, 138, 134));
    // Base block.
    gfx_rect(x - 3, bottom - 7, w + 6, 7, stone);
    gfx_hline(x - 3, bottom - 7, w + 6, light);
    gfx_rect(x + w - 1, bottom - 7, 4, 7, dark);
    // Hanging moss.
    for (int i = 0; i < w; i += 3) {
        int len = (int)(rpg_hash(seed + (uint32_t)i) % 9u);
        gfx_vline(x + i, top + 6, len, moss);
    }
}

static void ruins(int y0, int h, uint32_t t) {
    int hz = y0 + h * 52 / 100, bottom = y0 + h;
    gfx_vgradient(0, y0, gfx_w, (hz - y0) * 2 / 3, rgb(64, 42, 112), rgb(196, 96, 120));
    gfx_vgradient(0, y0 + (hz - y0) * 2 / 3, gfx_w, hz - y0 - (hz - y0) * 2 / 3 + 2, rgb(196, 96, 120), rgb(252, 168, 92));
    // Low sun with a soft halo.
    rpg_glow(96, hz - 10, 26, rgb(255, 200, 120), 18);
    gfx_circle(96, hz - 10, 11, rgb(255, 226, 150));
    gfx_circle(95, hz - 11, 8, rgb(255, 244, 196));
    // A few early stars.
    for (int i = 0; i < 10; i++) {
        uint32_t r = rpg_hash((uint32_t)i + 300u);
        int x = (int)(r % (uint32_t)gfx_w), y = y0 + 2 + (int)((r >> 8) % 26u);
        if (((t / 400u) + (uint32_t)i) % 7 != 0) gfx_pixel(x, y, rgb(255, 230, 250));
    }
    // Far mesas.
    for (int x = 0; x < gfx_w; x++) {
        int m = 8 + tri(x + 20) / 3 + (x > 70 && x < 110 ? 7 : 0) + tri(x * 3) / 8;
        gfx_vline(x, hz - m, m + 2, rgb(110, 62, 110));
    }
    // A broken arch, far away.
    const uint16_t far = rgb(138, 84, 120);
    gfx_rect(24, hz - 26, 5, 26, far);
    gfx_rect(48, hz - 22, 5, 22, far);
    for (int x = 24; x < 53; x++) {
        int d = x - 38;
        int top = hz - 30 + d * d / 22;
        if (x < 44) gfx_vline(x, top, 4, far);
    }
    // Stone floor in rows that widen towards the viewer.
    const uint16_t sand = rgb(196, 152, 112), sand_dark = rgb(160, 118, 92), line = rgb(128, 92, 80);
    gfx_rect(0, hz, gfx_w, bottom - hz, sand);
    int row = 0;
    for (int y = hz + 2, step = 3; y < bottom; y += step, step++, row++) {
        gfx_hline(0, y, gfx_w, line);
        int off = (row & 1) * 9;
        for (int x = off; x < gfx_w; x += 18 + row * 2) gfx_vline(x, y, step, line);
        for (int x = 0; x < gfx_w; x++)
            if (((x + y) & 1) && (rpg_hash((uint32_t)(x * 7 + y * 131)) & 7) == 0) gfx_pixel(x, y + 1, sand_dark);
    }
    gfx_vgradient(0, hz, gfx_w, 3, rgb(150, 92, 100), sand);
    // Pillars framing the scene, and rubble.
    pillar(8, 16, y0 + 18, bottom - 8, 1);
    pillar(116, 14, y0 + 44, bottom - 12, 2);
    gfx_rect(100, bottom - 14, 12, 7, rgb(160, 148, 142));
    gfx_hline(100, bottom - 14, 12, rgb(222, 212, 198));
    gfx_rect(26, bottom - 9, 8, 5, rgb(150, 138, 134));
    gfx_hline(26, bottom - 9, 8, rgb(210, 200, 190));
    // Warm light over the floor.
    rpg_glow(gfx_w / 2, bottom - 10, 40, rgb(255, 210, 150), 8);
}

// ---- lair ----

static void lair(int y0, int h, uint32_t t) {
    int bottom = y0 + h;
    gfx_vgradient(0, y0, gfx_w, h, rgb(36, 8, 14), rgb(112, 26, 18));
    // Back wall: rock blobs with glowing cracks.
    for (int i = 0; i < 18; i++) {
        uint32_t r = rpg_hash((uint32_t)i + 900u);
        gfx_circle((int)(r % (uint32_t)gfx_w), y0 + 20 + (int)((r >> 8) % (uint32_t)(h / 2)), 8 + (int)((r >> 16) % 8u),
                   i & 1 ? rgb(58, 16, 20) : rgb(72, 22, 22));
    }
    int pulse = 18 + tri((int)(t / 40u)) / 2;
    const uint16_t crack = rgb(255, 110, 30);
    int cx[] = {14, 104, 60};
    for (int k = 0; k < 3; k++) {
        int x = cx[k], y = y0 + 26 + k * 9;
        for (int s = 0; s < 6; s++) {
            int nx = x + (int)(rpg_hash((uint32_t)(k * 10 + s)) % 7u) - 3, ny = y + 5;
            gfx_line(x, y, nx, ny, crack);
            x = nx, y = ny;
        }
        rpg_glow(cx[k], y0 + 38 + k * 9, 12, crack, pulse / 3);
    }
    // Stalactites.
    for (int i = 0; i < 12; i++) {
        uint32_t r = rpg_hash((uint32_t)i + 700u);
        int x = i * 12 + (int)(r % 6u), len = 8 + (int)((r >> 8) % 18u), w = 3 + (int)((r >> 16) % 4u);
        for (int j = 0; j < len; j++) {
            int ww = w * (len - j) / len;
            gfx_hline(x - ww, y0 + j, 2 * ww + 1, rgb(28, 6, 10));
            gfx_pixel(x - ww, y0 + j, rgb(90, 30, 26));
        }
    }
    // Lava lake with moving waves.
    int lava_y = bottom - 26;
    rpg_glow(gfx_w / 2, lava_y, 70, rgb(255, 120, 30), pulse);
    for (int x = 0; x < gfx_w; x++) {
        int wave = tri(x * 2 + (int)(t / 30u)) / 6 + tri(x * 5 - (int)(t / 50u)) / 10;
        int top = lava_y + wave;
        gfx_vline(x, top, bottom - top, rgb(236, 72, 20));
        gfx_vline(x, top, 2, rgb(255, 196, 64));
        if ((rpg_hash((uint32_t)x + (t / 120u) * 131u) & 15) == 0) gfx_pixel(x, top + 4 + (x & 7), rgb(255, 240, 150));
        if (((x + (int)(t / 60u)) % 23) < 3) gfx_pixel(x, top + 2, rgb(255, 150, 40));
    }
    // A dark rock ledge in the middle for the monster.
    int ly = bottom - 17;
    for (int y = -9; y <= 9; y++)
        for (int x = -58; x <= 58; x++) {
            int d = x * x * 100 / (58 * 58) + y * y * 100 / 81;
            if (d > 100) continue;
            uint16_t c = y < -6 ? rgb(140, 56, 36) : ((x + y) & 1) && d > 60 ? rgb(46, 18, 20) : rgb(66, 28, 26);
            gfx_pixel(gfx_w / 2 + x, ly + y, c);
        }
    // Embers drift up.
    for (int i = 0; i < 14; i++) {
        uint32_t r = rpg_hash((uint32_t)i * 17u + 5u);
        int speed = 12 + (int)(r % 20u);
        int y = bottom - 20 - (int)((t * (uint32_t)speed / 1000u + (r >> 8)) % (uint32_t)(h - 24));
        int x = (int)((r >> 16) % (uint32_t)gfx_w) + tri((int)(t / 50u) + i * 9) / 6;
        gfx_pixel(x, y, (i & 1) ? rgb(255, 200, 60) : rgb(255, 120, 40));
        if (i % 3 == 0) gfx_pixel(x, y + 1, rgb(200, 60, 20));
    }
}

void rpg_draw_backdrop(int zone, int y0, int h, uint32_t t, bool night) {
    if (zone == 0) forest(y0, h, t);
    else if (zone == 1) ruins(y0, h, t);
    else lair(y0, h, t);
    if (!night) return;
    blend_rows(y0, h, rgb(8, 12, 44), zone == 2 ? 12 : 20);
    if (zone != 2)
        for (int i = 0; i < 24; i++) {
            uint32_t r = rpg_hash((uint32_t)i + 77u);
            int x = (int)(r % (uint32_t)gfx_w), y = y0 + 1 + (int)((r >> 8) % (uint32_t)(h / 3));
            uint16_t c = ((t / 300u + (uint32_t)i) % 9) ? rgb(220, 226, 255) : rgb(120, 130, 190);
            uint16_t sky = gfx_get(x, y);
            if (rgb_b(sky) > rgb_r(sky)) gfx_pixel(x, y, c); // only on open sky
        }
}

// ---- UI pieces ----

void rpg_draw_panel(int x, int y, int w, int h) {
    gfx_round_rect(x, y, w, h, 3, rgb(14, 10, 30));
    gfx_round_rect(x + 1, y + 1, w - 2, h - 2, 2, rgb(240, 214, 140));
    gfx_vgradient(x + 2, y + 2, w - 4, h - 4, rgb(52, 62, 150), rgb(24, 26, 78));
    gfx_hline(x + 3, y + 2, w - 6, rgb(98, 112, 200));
}

void rpg_draw_bar(int x, int y, int w, int h, int value, int max, uint16_t hi, uint16_t lo) {
    gfx_rect(x, y, w, h, rgb(12, 10, 24));
    gfx_rect(x + 1, y + 1, w - 2, h - 2, rgb(54, 40, 60));
    if (max <= 0) return;
    if (value < 0) value = 0;
    if (value > max) value = max;
    int fill = (w - 2) * value / max;
    if (value > 0 && fill < 1) fill = 1;
    if (fill <= 0) return;
    gfx_vgradient(x + 1, y + 1, fill, h - 2, hi, lo);
    gfx_hline(x + 1, y + 1, fill, rpg_mix(hi, C_WHITE, 14));
}

void rpg_draw_heart(int x, int y) {
    static const char *const rows[] = {".##.##.", "#o####d", "#o####d", ".#####.", "..###..", "...#..."};
    for (int j = 0; j < 6; j++)
        for (int i = 0; i < 7; i++) {
            char ch = rows[j][i];
            if (ch == '.') continue;
            gfx_pixel(x + i, y + j, ch == 'o' ? rgb(255, 190, 200) : ch == 'd' ? rgb(170, 20, 50) : rgb(236, 40, 70));
        }
}

void rpg_draw_star(int x, int y, uint16_t c) {
    static const char *const rows[] = {"...#...", "..###..", "#######", ".#####.", "..###..", ".##.##.", "#.....#"};
    for (int j = 0; j < 7; j++)
        for (int i = 0; i < 7; i++)
            if (rows[j][i] == '#') gfx_pixel(x + i, y + j, c);
}

void rpg_draw_flame(int cx, int base, int size, uint32_t t) {
    // Three nested tongues that flicker at different rates.
    static const uint16_t cols[3] = {rgb(230, 50, 20), rgb(255, 150, 30), rgb(255, 236, 130)};
    for (int k = 0; k < 3; k++) {
        int s = size * (3 - k) / 3;
        int hgt = s * 2 + tri((int)(t / (uint32_t)(30 + k * 13)) + k * 20) * s / 64;
        for (int j = 0; j < hgt; j++) {
            int w = s * (hgt - j) / hgt;
            int sway = tri((int)(t / 25u) + j * 4 + k * 11) * j / (hgt * 8 + 1);
            if (j < hgt / 3) w = s - (hgt / 3 - j) * s / (hgt / 3 + 1) / 2;
            gfx_hline(cx - w + sway, base - j, 2 * w + 1, cols[k]);
        }
    }
}

void rpg_draw_campfire(int cx, int base, uint32_t t) {
    rpg_glow(cx, base - 6, 34, rgb(255, 150, 60), 14 + tri((int)(t / 35u)) / 4);
    // Ring of stones and crossed logs.
    for (int i = -3; i <= 3; i++) gfx_circle(cx + i * 5, base + 1 + (i * i) / 4, 2, i & 1 ? rgb(110, 110, 120) : rgb(150, 150, 160));
    gfx_line(cx - 10, base, cx + 8, base - 5, rgb(96, 58, 36));
    gfx_line(cx - 10, base - 1, cx + 8, base - 6, rgb(130, 82, 48));
    gfx_line(cx + 10, base, cx - 8, base - 5, rgb(96, 58, 36));
    gfx_line(cx + 10, base - 1, cx - 8, base - 6, rgb(130, 82, 48));
    rpg_draw_flame(cx, base - 3, 7, t);
    // Sparks.
    for (int i = 0; i < 5; i++) {
        uint32_t r = rpg_hash((uint32_t)i + 50u);
        int y = base - 14 - (int)((t / 25u + (r & 63u)) % 30u);
        int x = cx - 4 + (int)(r % 9u) + tri((int)(t / 40u) + i * 13) / 8;
        gfx_pixel(x, y, rgb(255, 210, 90));
    }
}

void rpg_draw_tent(int x, int base) {
    const int w = 44, h = 30;
    for (int j = 0; j < h; j++) {
        int half = w / 2 * (j + 1) / h;
        int cx = x + w / 2;
        gfx_hline(cx - half, base - h + j, half, rgb(214, 190, 140));
        gfx_hline(cx, base - h + j, half + 1, rgb(166, 140, 104));
        if (j > h / 3) {
            int door = (j - h / 3) * 7 / (h - h / 3);
            gfx_hline(cx - door, base - h + j, 2 * door + 1, rgb(46, 30, 34));
        }
        if ((j & 3) == 3) gfx_pixel(cx - half + 2, base - h + j, rgb(240, 220, 170));
    }
    gfx_line(x + w / 2, base - h - 3, x + w / 2, base - h + 1, rgb(110, 76, 50));
    gfx_line(x + w / 2, base - h - 3, x + w / 2 + 5, base - h - 1, rgb(220, 60, 60));
}

void rpg_draw_scaled(const sprite_t *s, int x, int y, int scale4, bool solid, uint16_t solid_colour, int dissolve) {
    int w = s->w * scale4 / 4, h = s->h * scale4 / 4;
    for (int j = 0; j < h; j++) {
        int dy = y + j;
        if (dy < 0 || dy >= gfx_h) continue;
        int sy = j * 4 / scale4;
        const uint8_t *row = &s->pixels[sy * s->w];
        uint16_t *out = &gfx_fb[dy * gfx_w];
        for (int i = 0; i < w; i++) {
            int dx = x + i;
            if (dx < 0 || dx >= gfx_w) continue;
            int sx = i * 4 / scale4;
            uint8_t idx = row[sx];
            if (!idx) continue;
            if (dissolve && (int)(rpg_hash((uint32_t)(sy * 256 + sx)) & 255u) < dissolve) continue;
            out[dx] = solid ? solid_colour : s->palette[idx];
        }
    }
}

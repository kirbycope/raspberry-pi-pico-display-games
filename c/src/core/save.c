#include "save.h"

#include <stddef.h>
#include <string.h>

#include "hal.h"

#define SAVE_MAGIC 0x50444731u // "PDG1"

_Static_assert(sizeof(save_t) <= HAL_SAVE_SIZE, "save_t must fit the save sector");

save_t g_save;
static uint32_t stored_check;

static uint32_t checksum(const save_t *s) {
    // FNV-1a over everything but the check field itself.
    const uint8_t *p = (const uint8_t *)s;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < offsetof(save_t, check); i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

void save_reset(void) {
    memset(&g_save, 0, sizeof g_save);
    g_save.magic = SAVE_MAGIC;
    g_save.check = checksum(&g_save);
}

void save_load(void) {
    if (!hal_load(&g_save, sizeof g_save) || g_save.magic != SAVE_MAGIC || g_save.check != checksum(&g_save))
        save_reset();
    stored_check = g_save.check;
}

void save_commit(void) {
    g_save.check = checksum(&g_save);
    if (g_save.check == stored_check) return;
    if (hal_store(&g_save, sizeof g_save)) stored_check = g_save.check;
}

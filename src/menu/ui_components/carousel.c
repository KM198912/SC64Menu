/**
 * @file carousel.c
 * @brief Implementation of the carousel UI component.
 * @ingroup ui_components
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../ui_components.h"
#include "constants.h"
#include "../rom_info.h"

#define CAROUSEL_POOL_SIZE 7
#define CAROUSEL_CENTER_INDEX 3
#define CAROUSEL_Y_POS (DISPLAY_CENTER_Y - 40)
#define CAROUSEL_SPACING 140

typedef struct {
    int32_t entry_index;
    component_boxart_t *boxart;
    char game_code[5];
} carousel_slot_t;

static carousel_slot_t pool[CAROUSEL_POOL_SIZE];
static char *last_directory = NULL;

static void carousel_pool_init() {
    for (int i = 0; i < CAROUSEL_POOL_SIZE; i++) {
        pool[i].entry_index = -1;
        pool[i].boxart = NULL;
        pool[i].game_code[0] = '\0';
    }
}

static void carousel_pool_free() {
    for (int i = 0; i < CAROUSEL_POOL_SIZE; i++) {
        if (pool[i].boxart) {
            ui_components_boxart_free(pool[i].boxart);
            pool[i].boxart = NULL;
        }
        pool[i].entry_index = -1;
    }
}

static void resolve_game_code(menu_t *menu, int32_t entry_index, char *out_code) {
    if (entry_index < 0 || entry_index >= menu->browser.entries) {
        strcpy(out_code, "####");
        return;
    }

    entry_t *entry = &menu->browser.list[entry_index];
    if (entry->type != ENTRY_TYPE_ROM) {
        strcpy(out_code, "####");
        return;
    }

    path_t *rom_path = path_clone_push(menu->browser.directory, entry->name);
    rom_info_t info;
    if (rom_config_load(rom_path, &info) == ROM_OK) {
        memcpy(out_code, info.game_code, 4);
        out_code[4] = '\0';
    } else {
        strcpy(out_code, "####");
    }
    path_free(rom_path);
}

void ui_components_carousel_draw(menu_t *menu) {
    if (!is_memory_expanded()) {
        ui_components_file_list_draw(menu->browser.list, menu->browser.entries, menu->browser.selected);
        return;
    }

    char *current_dir = path_get(menu->browser.directory);
    if (!last_directory || strcmp(last_directory, current_dir) != 0) {
        carousel_pool_free();
        carousel_pool_init();
        if (last_directory) free(last_directory);
        last_directory = strdup(current_dir);
    }

    int32_t selected = menu->browser.selected;
    float current_offset = menu->browser.carousel_offset;
    
    // 1. Update pool and identify missing slots
    bool decoder_busy = false;
    int32_t pending_slots[CAROUSEL_POOL_SIZE];
    int pending_count = 0;

    for (int i = 0; i < CAROUSEL_POOL_SIZE; i++) {
        int32_t target_entry_index = selected + (i - CAROUSEL_CENTER_INDEX);
        
        if (target_entry_index < 0 || target_entry_index >= menu->browser.entries) {
             if (pool[i].boxart) {
                ui_components_boxart_free(pool[i].boxart);
                pool[i].boxart = NULL;
            }
            pool[i].entry_index = -1;
            continue;
        }

        // Check if index changed
        if (pool[i].entry_index != target_entry_index) {
            if (pool[i].boxart) ui_components_boxart_free(pool[i].boxart);
            pool[i].boxart = NULL;
            pool[i].entry_index = target_entry_index;
            resolve_game_code(menu, target_entry_index, pool[i].game_code);
        }

        if (pool[i].boxart) {
            if (pool[i].boxart->loading) {
                decoder_busy = true;
            }
        } else if (strcmp(pool[i].game_code, "####") != 0 && strcmp(pool[i].game_code, "") != 0) {
            // This slot exists but has no boxart object yet. Add to pending queue.
            pending_slots[pending_count++] = i;
        }
    }

    // 2. If decoder is idle, start loading the most important pending slot (closest to center)
    if (!decoder_busy && pending_count > 0) {
        int best_pending_idx = -1;
        int min_dist = 100;

        for (int p = 0; p < pending_count; p++) {
            int i = pending_slots[p];
            int dist = abs(i - CAROUSEL_CENTER_INDEX);
            if (dist < min_dist) {
                min_dist = dist;
                best_pending_idx = i;
            }
        }

        if (best_pending_idx != -1) {
            int i = best_pending_idx;
            pool[i].boxart = ui_components_boxart_init(
                menu->storage_prefix, 
                pool[i].game_code, 
                menu->browser.list[pool[i].entry_index].name, 
                IMAGE_BOXART_FRONT
            );
            // If init failed (e.g. busy at the very last second), it stays NULL and we try again next frame
        }
    }

    rdpq_mode_push();
    rdpq_set_mode_standard();

    for (int i = 0; i < CAROUSEL_POOL_SIZE; i++) {
        if (pool[i].entry_index == -1) continue;

        // Visual offset is based on the floating point carousel_offset
        float visual_position_offset = (float)pool[i].entry_index - current_offset;
        
        float x = DISPLAY_CENTER_X + (visual_position_offset * CAROUSEL_SPACING);
        float dist = fabsf(visual_position_offset);
        float scale = 1.0f - (dist * 0.2f);
        float opacity = 1.0f - (dist * 0.3f);
        
        if (scale < 0.1f) continue; // Off-screen or too small
        if (opacity < 0.0f) opacity = 0.0f;

        if (pool[i].boxart && pool[i].boxart->image) {
            surface_t *surf = pool[i].boxart->image;
            float sw = surf->width * scale;
            float sh = surf->height * scale;

            rdpq_set_prim_color(RGBA32(255, 255, 255, (uint8_t)(opacity * 255)));
            rdpq_tex_blit(surf, x - (sw / 2), CAROUSEL_Y_POS - (sh / 2), &(rdpq_blitparms_t){
                .scale_x = scale,
                .scale_y = scale,
            });
        } else {
            // Draw stylized Procedural Cartridge/Folder
            entry_t *entry = &menu->browser.list[pool[i].entry_index];
            bool is_dir = (entry->type == ENTRY_TYPE_DIR);
            
            int pw = BOXART_WIDTH * scale;
            int ph = BOXART_HEIGHT * scale;
            
            // 1. Shell
            color_t shell_color = is_dir ? RGBA32(180, 150, 0, (uint8_t)(opacity * 255)) : RGBA32(60, 60, 60, (uint8_t)(opacity * 255));
            ui_components_box_draw(x - (pw / 2), CAROUSEL_Y_POS - (ph / 2), x + (pw / 2), CAROUSEL_Y_POS + (ph / 2), shell_color);
            
            // 2. Label Area
            int lw = pw * 0.85f;
            int lh = ph * 0.70f;
            color_t label_color = is_dir ? RGBA32(230, 200, 50, (uint8_t)(opacity * 255)) : RGBA32(120, 120, 120, (uint8_t)(opacity * 255));
            ui_components_box_draw(x - (lw / 2), CAROUSEL_Y_POS - (lh / 2) + (5 * scale), x + (lw / 2), CAROUSEL_Y_POS + (lh / 2), label_color);

            // 3. Title on Label (Shortened if needed)
            char preview_name[12];
            strncpy(preview_name, entry->pretty_name ? entry->pretty_name : entry->name, 11);
            preview_name[11] = '\0';

            rdpq_set_prim_color(RGBA32(255, 255, 255, (uint8_t)(opacity * 255)));
            rdpq_textparms_t text_parms = {
                .width = lw - (4 * scale),
                .height = lh - (4 * scale),
                .align = ALIGN_CENTER,
                .valign = VALIGN_CENTER,
                .wrap = WRAP_ELLIPSES,
            };
            rdpq_text_print(&text_parms, FNT_DEFAULT, x - (lw / 2) + (2 * scale), CAROUSEL_Y_POS - (lh / 2) + (5 * scale), preview_name);
        }
    }
    rdpq_mode_pop();

    // Draw selected game full title at bottom
    if (menu->browser.entry) {
        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "\n\n\n\n\n\n\n\n\n\n\n\n"
            "^%02X%s^00",
            STL_DEFAULT,
            menu->browser.entry->pretty_name ? menu->browser.entry->pretty_name : menu->browser.entry->name
        );
    }
}

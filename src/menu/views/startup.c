#include "utils/fs.h"
#include "views.h"
#include "../ui_components.h"


static void draw (menu_t *menu, surface_t *d) {
    rdpq_attach_clear(d, NULL);

    if (menu->settings.rom_autoload_enabled && menu->startup_timer > 0) {
        ui_components_background_draw();
        
        ui_components_dialog_draw(400, 150);

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_TOP,
            "AUTOLOADING ROM\n"
        );

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_CENTER,
            "\n%s\n\n",
            menu->settings.rom_autoload_filename ? menu->settings.rom_autoload_filename : "UNKNOWN ROM"
        );

        float progress = (60.0f - (float)menu->startup_timer) / 60.0f;
        ui_components_progressbar_draw(120, 240, 520, 250, progress);

        ui_components_main_text_draw(
            STL_DEFAULT,
            ALIGN_CENTER, VALIGN_BOTTOM,
            "HOLD START TO CANCEL (%d)",
            menu->startup_timer / 30
        );
    }

    rdpq_detach_show();
}


void view_startup_init (menu_t *menu) {
    menu->startup_timer = 0;

#ifdef FEATURE_AUTOLOAD_ROM_ENABLED
    if (menu->settings.rom_autoload_enabled) {
        menu->startup_timer = 60; // ~2 seconds interactive window
        return; // stay in startup mode to show interactive UI
    }
#endif
    
    if (menu->settings.first_run) {
        menu->settings.first_run = false;
        settings_save(&menu->settings);
        menu->next_mode = MENU_MODE_CREDITS;
    }
    else {
        menu->next_mode = MENU_MODE_BROWSER;
    }
}

void view_startup_display (menu_t *menu, surface_t *display) {
    if (menu->settings.rom_autoload_enabled && menu->startup_timer > 0) {
        // Poll for bypass
        JOYPAD_PORT_FOREACH (port) {
            joypad_buttons_t b_held = joypad_get_buttons_held(port);

            if (b_held.start) {
                menu->settings.rom_autoload_enabled = false;
                menu->startup_timer = 0;
                menu->next_mode = MENU_MODE_BROWSER;
                break;
            }
        }

        if (menu->startup_timer > 0) {
            menu->startup_timer--;
            if (menu->startup_timer == 0) {
                // Timeout reached, trigger autoload
                if (menu->load.rom_path) path_free(menu->load.rom_path);
                if (menu->browser.directory) path_free(menu->browser.directory);

                menu->browser.directory = path_init(menu->storage_prefix, menu->settings.rom_autoload_path);
                menu->load.rom_path = path_clone_push(menu->browser.directory, menu->settings.rom_autoload_filename);
                menu->load_pending.rom_file = true;
                menu->next_mode = MENU_MODE_LOAD_ROM;
            }
        }
    }

    draw(menu, display);
}

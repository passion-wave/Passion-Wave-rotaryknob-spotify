// SPDX-FileCopyrightText: 2026 Passion Wave
// SPDX-License-Identifier: MIT
#include "pw_ui.h"
#include "sdkconfig.h"
#include "pw_app.h"
#include "pw_board.h"
#include "pw_weather.h"
#include "pw_assets.h"
#include "pw_spotify.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
LV_FONT_DECLARE(pw_font_de_14);
LV_FONT_DECLARE(pw_font_de_18);
LV_FONT_DECLARE(pw_font_de_24);
LV_FONT_DECLARE(pw_font_de_32);

#define COLOR_BG 0x07090B
#define COLOR_PANEL 0x111519
#define COLOR_TEXT 0xF2F1EE
#define COLOR_MUTED 0x899297
#define COLOR_AQUA 0x68B8BA
#define DRAW_BUFFER_BYTES (360 * 20 * 2)

typedef enum { PAGE_MUSIC, PAGE_WEATHER, PAGE_DEVICE } page_t;
typedef enum { ACTION_OPEN_SETUP, ACTION_CLOSE_SETUP, ACTION_OPEN_LAN } action_t;
static const char *TAG = "pw_ui";
static QueueHandle_t completed_queue, action_queue, action_result_queue;
static TaskHandle_t ui_task_handle, control_task_handle;
static SemaphoreHandle_t ui_started;
static esp_err_t startup_result = ESP_ERR_NO_MEM;
static lv_display_t *display;
static lv_indev_t *pointer;
static lv_obj_t *pages[3], *nav[3], *setup_panel, *qr;
static lv_obj_t *clock_label, *network_label, *device_detail, *brightness_label;
static lv_obj_t *weather_temperature, *weather_condition, *weather_metrics, *weather_days, *weather_source;
static lv_obj_t *setup_title, *setup_detail, *setup_next_text, *setup_timer, *setup_hint, *feedback_label;
static pw_app_view_t app_view;
static pw_weather_snapshot_t weather; // Avoid a ~7 KiB snapshot on a task stack.
static pw_board_input_t input;
static page_t active_page = PAGE_DEVICE;
static int setup_step;
static bool setup_visible, flush_pending, flush_was_last, first_frame_done;
static bool suppress_touch, long_press_fired, long_press_cancelled;
static bool display_dark, initialized;
static uint8_t output_brightness = 255;
static uint16_t press_x, press_y;
static int64_t press_started, last_activity, last_view_refresh, feedback_expires;
static char qr_payload[200]; // Contains device-only AP credentials; never log/export.
static void *draw_buffer1, *draw_buffer2;
static void refresh_view(void);
static void show_page(page_t page);
typedef enum { VISUAL_NONE, VISUAL_PHOTO, VISUAL_AVATAR } visual_t;
static visual_t active_visual;
static bool visual_automatic, assets_available;
static lv_obj_t *visual_panel, *visual_image, *visual_title, *visual_subtitle;
static lv_obj_t *visual_caption, *visual_status, *visual_hint, *clock_hands[2], *clock_dot, *second_dot;
static lv_point_precise_t hand_points[2][2];
static lv_image_dsc_t image_descriptors[2];
static int displayed_slot = -1, requested_asset = PW_ASSET_NONE;
static uint32_t asset_generation = 1;
static int64_t asset_requested_at;
static void open_visual(visual_t kind, bool automatic);
static void close_visual(void);
static void refresh_visual(void);
static void service_visual(void);
static pw_spotify_snapshot_t spotify_view; /* Several KiB: never a task-stack copy. */
static lv_obj_t *music_title, *music_artist, *music_state, *music_output, *music_favorites;
static lv_obj_t *music_controls[3], *music_progress;
typedef enum { PICKER_NONE, PICKER_DEVICES, PICKER_FAVORITES } picker_t;
static picker_t picker_kind;
static lv_obj_t *picker_panel, *picker_title, *picker_hint, *picker_rows[3], *picker_prev, *picker_next;
static char picker_ids[3][PW_SPOTIFY_DEVICE_ID_BYTES];
static pw_app_favorite_t picker_favorites[3];
static size_t picker_offset, picker_total;
static uint32_t picker_session, picker_revision, picker_snapshot_revision;
static pw_spotify_command_t picker_target;
static int volume_target = -1;
static uint32_t volume_session, volume_generation;
static int64_t volume_requested_at, music_notice_until;
static int64_t picker_refresh_at;
static char music_notice[96];
static void refresh_music(void);
static void refresh_picker(void);
static void close_picker(void);

static uint32_t tick_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void label_text(lv_obj_t *label, const char *text) {
    if (strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int width,
                            const lv_font_t *font, uint32_t color, const char *text) {
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    return label;
}
static lv_obj_t *make_panel(lv_obj_t *parent) {
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, 360, 360);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    return panel;
}
static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int width, int height,
                             const char *text, lv_event_cb_t callback, void *user) {
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_radius(button, 18, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x292F33), 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &pw_font_de_18, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_center(label);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, user);
    return button;
}
static void acknowledge(void) {
    static int64_t last_haptic;
    last_activity = esp_timer_get_time();
    if (app_view.haptic && last_activity - last_haptic >= 55000) {
        (void)pw_board_haptic(1);
        last_haptic = last_activity;
    }
}
static void nav_clicked(lv_event_t *event) {
    acknowledge();
    show_page((page_t)(uintptr_t)lv_event_get_user_data(event));
}
static void queue_action(action_t action) {
    if (xQueueSend(action_queue, &action, 0) == pdTRUE) {
        acknowledge();
        lv_obj_remove_flag(feedback_label, LV_OBJ_FLAG_HIDDEN);
        feedback_expires = esp_timer_get_time() + 3000000;
        label_text(feedback_label, action == ACTION_CLOSE_SETUP ? "Wird geschlossen …" : "Einrichtung startet …");
    } else {
        label_text(feedback_label, "Einen Moment bitte");
    }
}
static void open_clicked(lv_event_t *event) { (void)event; queue_action(ACTION_OPEN_SETUP); }
#ifdef CONFIG_PW_LAN_HTTP_LAB
static void lan_clicked(lv_event_t *event) { (void)event; queue_action(ACTION_OPEN_LAN); }
#endif
static void setup_next_clicked(lv_event_t *event) {
    (void)event;
    acknowledge();
    setup_step = 1 - setup_step;
    qr_payload[0] = '\0';
    refresh_view();
}
static void close_clicked(lv_event_t *event) { (void)event; queue_action(ACTION_CLOSE_SETUP); }
static void visual_clicked(lv_event_t *event) {
    acknowledge();
    open_visual((visual_t)(uintptr_t)lv_event_get_user_data(event), false);
}

static void enabled_button(lv_obj_t *button, bool enabled) {
    if (enabled) lv_obj_remove_state(button, LV_STATE_DISABLED);
    else lv_obj_add_state(button, LV_STATE_DISABLED);
    lv_obj_set_style_opa(button, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
}
static void bounded_button_label(lv_obj_t *button, int width, const lv_font_t *font) {
    lv_obj_t *label = lv_obj_get_child(button, 0);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(label, font->line_height);
    lv_obj_center(label);
}
static void music_feedback(const char *message) {
    snprintf(music_notice, sizeof music_notice, "%s", message);
    music_notice_until = esp_timer_get_time() + 3500000;
}
static void command_feedback(esp_err_t result) {
    if (result == ESP_OK) { acknowledge(); music_feedback("Anfrage gesendet …"); }
    else if (result == ESP_ERR_NOT_SUPPORTED) music_feedback("Aktion noch nicht verfügbar");
    else if (result == ESP_ERR_INVALID_STATE) music_feedback("Ausgabe bitte erneut wählen");
    else music_feedback("Spotify gerade nicht bereit");
}
static pw_spotify_command_t music_command(pw_spotify_command_kind_t kind) {
    pw_spotify_command_t command = {.kind = kind, .session = spotify_view.session,
                                    .selection_generation = spotify_view.selection_generation};
    snprintf(command.device_id, sizeof command.device_id, "%s", spotify_view.selected_device_id);
    return command;
}
static bool playing_here(void) {
    return spotify_view.playback_known && spotify_view.playing &&
           spotify_view.selected_device_id[0] &&
           !strcmp(spotify_view.active_device_id, spotify_view.selected_device_id);
}
static void playback_clicked(lv_event_t *event) {
    const uintptr_t action = (uintptr_t)lv_event_get_user_data(event);
    pw_spotify_command_t command = music_command(action == 0 ? PW_SPOTIFY_PREVIOUS :
        action == 2 ? PW_SPOTIFY_NEXT : playing_here() ? PW_SPOTIFY_PAUSE : PW_SPOTIFY_PLAY);
    uint32_t request_id;
    command_feedback(pw_spotify_submit(&command, &request_id));
    refresh_music();
}
static void close_picker(void) {
    picker_kind = PICKER_NONE;
    lv_obj_add_flag(picker_panel, LV_OBJ_FLAG_HIDDEN);
    memset(picker_ids, 0, sizeof picker_ids);
    memset(picker_favorites, 0, sizeof picker_favorites);
}
static void picker_close_clicked(lv_event_t *event) { (void)event; acknowledge(); close_picker(); }
static void picker_refresh_clicked(lv_event_t *event) {
    (void)event; acknowledge();
    if (picker_kind == PICKER_DEVICES) {
        (void)pw_spotify_refresh();
        picker_refresh_at = esp_timer_get_time() + 2000000;
        label_text(picker_hint, "Geräte werden angefragt …");
    } else refresh_picker();
}
static void picker_page_clicked(lv_event_t *event) {
    if ((uintptr_t)lv_event_get_user_data(event)) {
        if (picker_offset + 3 < picker_total) picker_offset += 3;
    } else if (picker_offset >= 3) picker_offset -= 3;
    acknowledge(); refresh_picker();
}
static void picker_row_clicked(lv_event_t *event) {
    unsigned row = (unsigned)(uintptr_t)lv_event_get_user_data(event);
    if (row >= 3 || !picker_ids[row][0]) return;
    esp_err_t result;
    if (picker_kind == PICKER_DEVICES) result = pw_spotify_select_device(picker_ids[row], picker_session);
    else {
        uint32_t request_id;
        result = pw_app_play_favorite(picker_ids[row], picker_revision, &picker_target, &request_id);
    }
    command_feedback(result);
    if (result == ESP_OK) {
        if (picker_kind == PICKER_DEVICES) music_feedback("Ausgabe gewählt · Play zum Start");
        close_picker();
    }
    refresh_music();
}
static void refresh_picker(void) {
    if (picker_kind == PICKER_NONE) return;
    memset(picker_ids, 0, sizeof picker_ids);
    if (picker_kind == PICKER_DEVICES) {
        picker_total = spotify_view.device_count < PW_SPOTIFY_MAX_DEVICES ? spotify_view.device_count : PW_SPOTIFY_MAX_DEVICES;
        if (picker_offset >= picker_total)
            picker_offset = picker_total ? ((picker_total - 1) / 3) * 3 : 0;
        picker_snapshot_revision = spotify_view.revision;
        label_text(picker_title, "Deine Ausgabe");
    } else {
        memset(picker_favorites, 0, sizeof picker_favorites);
        picker_total = pw_app_get_favorites(picker_offset, picker_favorites, 3, &picker_revision);
        label_text(picker_title, "Deine Favoriten");
    }
    for (unsigned row = 0; row < 3; ++row) {
        if (picker_offset + row >= picker_total) { lv_obj_add_flag(picker_rows[row], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(picker_rows[row], LV_OBJ_FLAG_HIDDEN);
        bool allowed;
        char label[180];
        if (picker_kind == PICKER_DEVICES) {
            const pw_spotify_device_t *device = &spotify_view.devices[picker_offset + row];
            snprintf(picker_ids[row], sizeof picker_ids[row], "%s", device->id);
            snprintf(label, sizeof label, "%s%s", device->restricted ? "Gesperrt: " : "", device->name);
            allowed = device->id[0] && !device->restricted;
        } else {
            memcpy(picker_ids[row], picker_favorites[row].id, sizeof picker_favorites[row].id);
            picker_ids[row][sizeof picker_favorites[row].id - 1] = 0;
            snprintf(label, sizeof label, "%s%s", picker_favorites[row].playable ? "" : "Später: ", picker_favorites[row].name);
            allowed = picker_favorites[row].playable && spotify_view.can_play;
        }
        label_text(lv_obj_get_child(picker_rows[row], 0), label);
        enabled_button(picker_rows[row], allowed);
    }
    char hint[96];
    if (!picker_total) snprintf(hint, sizeof hint, "%s", picker_kind == PICKER_DEVICES ? "Lautsprecher in Spotify öffnen" : "Auf der Webseite hinzufügen");
    else snprintf(hint, sizeof hint, "%u–%u von %u", (unsigned)picker_offset + 1,
                  (unsigned)((picker_offset + 3 < picker_total) ? picker_offset + 3 : picker_total), (unsigned)picker_total);
    label_text(picker_hint, hint);
    enabled_button(picker_prev, picker_offset >= 3);
    enabled_button(picker_next, picker_offset + 3 < picker_total);
}
static void service_picker_refresh(void) {
    const bool linked = spotify_view.enabled && spotify_view.linked;
    if (picker_kind != PICKER_NONE &&
        (app_view.setup_open || !linked || picker_session != spotify_view.session)) {
        close_picker();
        return;
    }
    if (picker_kind != PICKER_DEVICES) return;
    const int64_t now = esp_timer_get_time();
    const bool due = picker_refresh_at && now >= picker_refresh_at;
    const bool changed = picker_snapshot_revision != spotify_view.revision;
    if ((due || changed) && !input.touch_pressed && now - last_activity > 750000) {
        /* Async HTTPS may finish after the initial two-second feedback timer.
         * Follow later snapshots too, but never rebind a row under a touching
         * finger or before its queued release/click has been consumed. */
        picker_refresh_at = 0;
        refresh_picker();
    }
}
static void picker_open_clicked(lv_event_t *event) {
    acknowledge(); close_visual();
    picker_kind = (picker_t)(uintptr_t)lv_event_get_user_data(event);
    picker_offset = 0;
    picker_session = spotify_view.session;
    picker_target = music_command(PW_SPOTIFY_PLAY);
    refresh_picker();
    lv_obj_remove_flag(picker_panel, LV_OBJ_FLAG_HIDDEN);
    if (picker_kind == PICKER_DEVICES) {
        (void)pw_spotify_refresh();
        picker_refresh_at = esp_timer_get_time() + 2000000;
    }
}
static const char *spotify_state_text(void) {
    switch (spotify_view.state) {
        case PW_SPOTIFY_DISABLED: return "Noch nicht verfügbar";
        case PW_SPOTIFY_UNLINKED: return "Spotify per USB verbinden";
        case PW_SPOTIFY_WAITING_NETWORK: return "WLAN verbinden";
        case PW_SPOTIFY_WAITING_CLOCK: return "Uhrzeit wird eingestellt …";
        case PW_SPOTIFY_AUTHORIZING: return "Spotify wird verbunden …";
        case PW_SPOTIFY_REAUTH_REQUIRED: return "Spotify erneut verbinden";
        case PW_SPOTIFY_RATE_LIMITED: return "Spotify braucht kurz Pause";
        case PW_SPOTIFY_SUSPENDED: return "Update wird vorbereitet";
        case PW_SPOTIFY_DISCONNECTING: return "Spotify wird getrennt …";
        case PW_SPOTIFY_ERROR: return "Spotify gerade nicht bereit";
        case PW_SPOTIFY_READY: break;
    }
    if (!spotify_view.selected_device_id[0]) return "Ausgabe wählen";
    if (!spotify_view.selected_present) return "Ausgabe nicht erreichbar";
    if (spotify_view.selected_restricted) return "Ausgabe eingeschränkt";
    if (spotify_view.last_command_state == PW_SPOTIFY_COMMAND_QUEUED) return "Anfrage wird gesendet …";
    if (spotify_view.last_command_state == PW_SPOTIFY_COMMAND_UNCERTAIN) return "Bestätigung fehlt · bitte prüfen";
    if (spotify_view.last_command_state == PW_SPOTIFY_COMMAND_REJECTED) return "Aktion wurde abgelehnt";
    if (spotify_view.last_command_state == PW_SPOTIFY_COMMAND_STALE) return "Ausgabe bitte erneut wählen";
    if (!spotify_view.playback_known) return "Noch keine Wiedergabe bestätigt";
    if (strcmp(spotify_view.active_device_id, spotify_view.selected_device_id)) return "Spielt auf anderer Ausgabe";
    return spotify_view.playing ? "Spielt" : "Pausiert";
}
static void refresh_music(void) {
    pw_spotify_get_snapshot(&spotify_view);
    label_text(lv_obj_get_child(music_output, 0), spotify_view.selected_device_name[0] ? spotify_view.selected_device_name : "Ausgabe wählen");
    const bool linked = spotify_view.enabled && spotify_view.linked;
    label_text(music_title, spotify_view.playback_known && spotify_view.title[0] ? spotify_view.title :
               !spotify_view.enabled ? "Spotify" : linked ? "Deine Musik" : "Spotify verbinden");
    label_text(music_artist, spotify_view.playback_known ? spotify_view.artist : !spotify_view.enabled ? "Produktfreigabe noch offen" : "Labor · Einrichtung über USB");
    label_text(music_state, esp_timer_get_time() < music_notice_until ? music_notice : spotify_state_text());
    enabled_button(music_output, linked);
    enabled_button(music_favorites, linked && spotify_view.selected_present);
    enabled_button(music_controls[0], linked && spotify_view.can_previous);
    enabled_button(music_controls[1], linked && (playing_here() ? spotify_view.can_pause : spotify_view.can_play));
    enabled_button(music_controls[2], linked && spotify_view.can_next);
    label_text(lv_obj_get_child(music_controls[1], 0), playing_here() ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (spotify_view.playback_known && spotify_view.position_known && spotify_view.duration_ms) {
        lv_obj_remove_flag(music_progress, LV_OBJ_FLAG_HIDDEN);
        uint32_t permille = (uint32_t)((uint64_t)spotify_view.position_ms * 1000 / spotify_view.duration_ms);
        lv_bar_set_value(music_progress, permille > 1000 ? 1000 : permille, LV_ANIM_OFF);
    } else lv_obj_add_flag(music_progress, LV_OBJ_FLAG_HIDDEN);
    if (volume_target >= 0 && (spotify_view.session != volume_session ||
        spotify_view.selection_generation != volume_generation || !spotify_view.can_volume ||
        (spotify_view.selected_volume_known && spotify_view.selected_volume == volume_target) ||
        esp_timer_get_time() - volume_requested_at > 3000000)) volume_target = -1;
    service_picker_refresh();
}
static void music_rotate(int delta) {
    if (!spotify_view.can_volume || !spotify_view.selected_volume_known) {
        music_feedback("Lautstärke am Lautsprecher ändern"); return;
    }
    if (volume_target < 0 || volume_session != spotify_view.session || volume_generation != spotify_view.selection_generation)
        volume_target = spotify_view.selected_volume;
    if (delta > 100) delta = 100;
    if (delta < -100) delta = -100;
    volume_target += delta;
    if (volume_target < 0) volume_target = 0;
    if (volume_target > 100) volume_target = 100;
    pw_spotify_command_t command = music_command(PW_SPOTIFY_VOLUME);
    command.volume = (uint8_t)volume_target;
    uint32_t request_id;
    esp_err_t result = pw_spotify_submit(&command, &request_id);
    if (result == ESP_OK) {
        acknowledge(); volume_session = command.session; volume_generation = command.selection_generation;
        volume_requested_at = esp_timer_get_time();
        char notice[64]; snprintf(notice, sizeof notice, "%d %% angefragt", volume_target); music_feedback(notice);
    } else { volume_target = -1; command_feedback(result); }
}
static void draw_complete(void *context, esp_err_t result) {
    (void)context;
    // Board display worker never enters LVGL. There is exactly one outstanding
    // LVGL flush; its small queue cannot fill in the normal protocol.
    (void)xQueueSend(completed_queue, &result, 0);
}
static void consume_completion(esp_err_t result) {
    flush_pending = false;
    lv_display_flush_ready(display);
    if (result == ESP_OK && flush_was_last) first_frame_done = true;
    if (result != ESP_OK) ESP_LOGE(TAG, "display flush failed: %s", esp_err_to_name(result));
}
static void flush_wait(lv_display_t *target) {
    (void)target;
    // LVGL 9.2 can wait inside lv_timer_handler before returning to our loop.
    // This dedicated hook consumes the driver completion on the LVGL owner task.
    // Never release a source buffer merely because a timeout elapsed.
    while (flush_pending) {
        esp_err_t result;
        if (xQueueReceive(completed_queue, &result, pdMS_TO_TICKS(1100)) == pdTRUE) consume_completion(result);
        else ESP_LOGE(TAG, "display completion overdue; retaining draw buffer");
    }
}
static void flush_display(lv_display_t *target, const lv_area_t *area, uint8_t *pixels) {
    flush_was_last = lv_display_flush_is_last(target);
    flush_pending = true;
    const esp_err_t result = pw_board_draw_bitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1,
                                                (const uint16_t *)pixels, draw_complete, NULL);
    if (result != ESP_OK) consume_completion(result);
}
static void read_touch(lv_indev_t *device, lv_indev_data_t *data) {
    (void)device;
    data->point.x = input.touch_x;
    data->point.y = input.touch_y;
    data->state = input.touch_pressed && !suppress_touch ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void show_page(page_t page) {
    active_page = page;
    for (int i = 0; i < 3; ++i) {
        if (i == page) lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(nav[i], lv_color_hex(i == page ? 0x224044 : COLOR_PANEL), 0);
        lv_obj_set_style_border_color(nav[i], lv_color_hex(i == page ? COLOR_AQUA : 0x292F33), 0);
    }
}

static void create_ui(void) {
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 3; ++i) pages[i] = make_panel(screen);
    make_label(pages[PAGE_MUSIC], 70, 48, 120, &pw_font_de_18, COLOR_TEXT, "Spotify");
    music_favorites = make_button(pages[PAGE_MUSIC], 193, 43, 104, 32, "Favoriten", picker_open_clicked, (void *)(uintptr_t)PICKER_FAVORITES);
    bounded_button_label(music_favorites, 94, &pw_font_de_14);
    music_output = make_button(pages[PAGE_MUSIC], 60, 80, 240, 34, "Ausgabe wählen", picker_open_clicked, (void *)(uintptr_t)PICKER_DEVICES);
    bounded_button_label(music_output, 222, &pw_font_de_14);
    music_title = make_label(pages[PAGE_MUSIC], 40, 122, 280, &pw_font_de_24, COLOR_TEXT, "Spotify");
    lv_obj_set_height(music_title, 54); lv_label_set_long_mode(music_title, LV_LABEL_LONG_DOT);
    music_artist = make_label(pages[PAGE_MUSIC], 40, 180, 280, &pw_font_de_14, COLOR_MUTED, "");
    lv_obj_set_height(music_artist, 17); lv_label_set_long_mode(music_artist, LV_LABEL_LONG_DOT);
    music_state = make_label(pages[PAGE_MUSIC], 33, 203, 294, &pw_font_de_14, COLOR_AQUA, "Noch nicht verfügbar");
    lv_obj_set_height(music_state, 18); lv_label_set_long_mode(music_state, LV_LABEL_LONG_DOT);
    music_progress = lv_bar_create(pages[PAGE_MUSIC]);
    lv_obj_set_pos(music_progress, 70, 221); lv_obj_set_size(music_progress, 220, 3);
    lv_bar_set_range(music_progress, 0, 1000);
    lv_obj_set_style_bg_color(music_progress, lv_color_hex(COLOR_AQUA), LV_PART_INDICATOR);
    lv_obj_add_flag(music_progress, LV_OBJ_FLAG_HIDDEN);
    music_controls[0] = make_button(pages[PAGE_MUSIC], 65, 231, 58, 38, LV_SYMBOL_PREV, playback_clicked, (void *)0);
    music_controls[1] = make_button(pages[PAGE_MUSIC], 133, 231, 94, 38, LV_SYMBOL_PLAY, playback_clicked, (void *)1);
    music_controls[2] = make_button(pages[PAGE_MUSIC], 237, 231, 58, 38, LV_SYMBOL_NEXT, playback_clicked, (void *)2);

    make_label(pages[PAGE_WEATHER], 85, 54, 190, &pw_font_de_24, COLOR_TEXT, "Wetter");
    lv_obj_t *outfit_button = make_button(pages[PAGE_WEATHER], 30, 94, 62, 38, "Outfit", visual_clicked, (void *)(uintptr_t)VISUAL_AVATAR);
    lv_obj_set_style_text_font(lv_obj_get_child(outfit_button, 0), &pw_font_de_14, 0);
    lv_obj_t *photo_button = make_button(pages[PAGE_WEATHER], 268, 94, 62, 38, "Foto", visual_clicked, (void *)(uintptr_t)VISUAL_PHOTO);
    lv_obj_set_style_text_font(lv_obj_get_child(photo_button, 0), &pw_font_de_14, 0);
    weather_temperature = make_label(pages[PAGE_WEATHER], 45, 93, 270, &pw_font_de_32, COLOR_TEXT, "—");
    weather_condition = make_label(pages[PAGE_WEATHER], 42, 137, 276, &pw_font_de_18, COLOR_AQUA, "Noch nicht eingerichtet");
    weather_metrics = make_label(pages[PAGE_WEATHER], 35, 172, 290, &pw_font_de_18, COLOR_TEXT, "Standort auf der Webseite wählen");
    weather_days = make_label(pages[PAGE_WEATHER], 30, 221, 300, &pw_font_de_14, COLOR_MUTED, "");
    weather_source = make_label(pages[PAGE_WEATHER], 70, 247, 220, &pw_font_de_14, COLOR_MUTED, "");

    make_label(pages[PAGE_DEVICE], 65, 50, 230, &pw_font_de_24, COLOR_TEXT, "Dein PassionWave");
    network_label = make_label(pages[PAGE_DEVICE], 45, 94, 270, &pw_font_de_18, COLOR_AQUA, "WLAN prüfen …");
    device_detail = make_label(pages[PAGE_DEVICE], 35, 124, 290, &pw_font_de_18, COLOR_MUTED, "");
    brightness_label = make_label(pages[PAGE_DEVICE], 35, 166, 290, &pw_font_de_24, COLOR_TEXT, "Helligkeit");
    make_label(pages[PAGE_DEVICE], 50, 198, 260, &pw_font_de_18, COLOR_MUTED, "Helligkeit am Ring ändern");
#ifdef CONFIG_PW_LAN_HTTP_LAB
    lv_obj_t *wifi_setup = make_button(pages[PAGE_DEVICE], 52, 227, 122, 40, "WLAN-Setup", open_clicked, NULL);
    lv_obj_t *lan_setup = make_button(pages[PAGE_DEVICE], 182, 227, 126, 40, "Web freigeben", lan_clicked, NULL);
    lv_obj_set_style_text_font(lv_obj_get_child(wifi_setup, 0), &pw_font_de_14, 0);
    lv_obj_set_style_text_font(lv_obj_get_child(lan_setup, 0), &pw_font_de_14, 0);
#else
    make_button(pages[PAGE_DEVICE], 65, 227, 230, 40, "Einrichtung öffnen", open_clicked, NULL);
#endif

    clock_label = make_label(screen, 105, 20, 150, &pw_font_de_18, COLOR_MUTED, "PassionWave");
    nav[0] = make_button(screen, 70, 278, 68, 44, "Musik", nav_clicked, (void *)(uintptr_t)PAGE_MUSIC);
    nav[1] = make_button(screen, 142, 278, 76, 44, "Wetter", nav_clicked, (void *)(uintptr_t)PAGE_WEATHER);
    nav[2] = make_button(screen, 222, 278, 68, 44, "Gerät", nav_clicked, (void *)(uintptr_t)PAGE_DEVICE);
    feedback_label = make_label(screen, 45, 124, 270, &pw_font_de_18, COLOR_AQUA, "");
    lv_obj_set_style_bg_color(feedback_label, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(feedback_label, LV_OPA_COVER, 0);
    // Feedback only shown when needed; starts above normal content.
    lv_obj_add_flag(feedback_label, LV_OBJ_FLAG_HIDDEN);

    picker_panel = make_panel(screen);
    picker_title = make_label(picker_panel, 60, 51, 224, &pw_font_de_24, COLOR_TEXT, "Deine Ausgabe");
    make_button(picker_panel, 279, 70, 36, 34, LV_SYMBOL_CLOSE, picker_close_clicked, NULL);
    for (unsigned i = 0; i < 3; ++i) {
        picker_rows[i] = make_button(picker_panel, 52, 112 + i * 47, 256, 42, "", picker_row_clicked, (void *)(uintptr_t)i);
        bounded_button_label(picker_rows[i], 238, &pw_font_de_18);
    }
    picker_hint = make_label(picker_panel, 42, 258, 276, &pw_font_de_14, COLOR_MUTED, "");
    picker_prev = make_button(picker_panel, 86, 283, 58, 36, LV_SYMBOL_LEFT, picker_page_clicked, (void *)0);
    lv_obj_t *picker_refresh = make_button(picker_panel, 153, 283, 54, 36, "Neu", picker_refresh_clicked, NULL);
    bounded_button_label(picker_refresh, 44, &pw_font_de_14);
    picker_next = make_button(picker_panel, 216, 283, 58, 36, LV_SYMBOL_RIGHT, picker_page_clicked, (void *)1);
    lv_obj_add_flag(picker_panel, LV_OBJ_FLAG_HIDDEN);

    visual_panel = make_panel(screen);
    visual_image = lv_image_create(visual_panel);
    lv_obj_set_pos(visual_image, -4, -4); // Source is 368x368; static overscan, never scale.
    lv_obj_add_flag(visual_image, LV_OBJ_FLAG_HIDDEN);
    for (int i = 0; i < 2; ++i) {
        clock_hands[i] = lv_line_create(visual_panel);
        lv_obj_set_pos(clock_hands[i], 0, 0);
        lv_obj_set_style_line_width(clock_hands[i], i == 0 ? 7 : 5, 0);
        lv_obj_set_style_line_rounded(clock_hands[i], true, 0);
        lv_obj_add_flag(clock_hands[i], LV_OBJ_FLAG_HIDDEN);
    }
    clock_dot = lv_obj_create(visual_panel);
    lv_obj_remove_style_all(clock_dot);
    lv_obj_set_size(clock_dot, 9, 9);
    lv_obj_set_pos(clock_dot, 176, 176);
    lv_obj_set_style_radius(clock_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(clock_dot, LV_OPA_COVER, 0);
    second_dot = lv_obj_create(visual_panel);
    lv_obj_remove_style_all(second_dot);
    lv_obj_set_size(second_dot, 5, 5);
    lv_obj_set_style_radius(second_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(second_dot, LV_OPA_COVER, 0);
    visual_title = make_label(visual_panel, 70, 30, 220, &pw_font_de_24, COLOR_TEXT, "");
    visual_subtitle = make_label(visual_panel, 50, 62, 260, &pw_font_de_18, COLOR_MUTED, "");
    visual_caption = make_label(visual_panel, 64, 284, 232, &pw_font_de_14, COLOR_TEXT, "");
    lv_obj_set_height(visual_caption, 36);
    lv_label_set_long_mode(visual_caption, LV_LABEL_LONG_DOT);
    visual_hint = make_label(visual_panel, 75, 323, 210, &pw_font_de_14, COLOR_MUTED, "Tippen: zurück");
    visual_status = make_label(visual_panel, 50, 155, 260, &pw_font_de_18, COLOR_TEXT, "");
    lv_obj_add_flag(visual_panel, LV_OBJ_FLAG_HIDDEN);

    setup_panel = make_panel(screen);
    setup_title = make_label(setup_panel, 64, 36, 232, &pw_font_de_24, COLOR_TEXT, "WLAN verbinden");
    setup_hint = make_label(setup_panel, 52, 69, 256, &pw_font_de_18, COLOR_MUTED, "Mit dem Handy scannen");
    lv_obj_t *quiet = lv_obj_create(setup_panel);
    lv_obj_remove_style_all(quiet);
    lv_obj_set_pos(quiet, 92, 90);
    lv_obj_set_size(quiet, 176, 176);
    lv_obj_set_style_bg_color(quiet, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(quiet, LV_OPA_COVER, 0);
    lv_obj_remove_flag(quiet, LV_OBJ_FLAG_SCROLLABLE);
    qr = lv_qrcode_create(quiet);
    lv_qrcode_set_size(qr, 144);
    lv_qrcode_set_dark_color(qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(qr, lv_color_hex(0xFFFFFF));
    lv_obj_set_pos(qr, 16, 16);
    // Quiet zone is outside the code canvas; no border can obscure its modules.
    setup_detail = make_label(setup_panel, 42, 270, 276, &pw_font_de_18, COLOR_TEXT, "");
    lv_obj_t *next = make_button(setup_panel, 130, 297, 100, 42, "Weiter", setup_next_clicked, NULL);
    setup_next_text = lv_obj_get_child(next, 0);
    make_button(setup_panel, 278, 112, 40, 40, LV_SYMBOL_CLOSE, close_clicked, NULL);
    setup_timer = make_label(setup_panel, 85, 14, 190, &pw_font_de_14, COLOR_MUTED, "Einrichtung");
    lv_obj_add_flag(setup_panel, LV_OBJ_FLAG_HIDDEN);
    show_page(PAGE_DEVICE);
}

static const char *condition_name(const char *symbol) {
    if (strstr(symbol, "thunder")) return "Gewitter möglich";
    if (strstr(symbol, "sleet")) return "Schneeregen";
    if (strstr(symbol, "snow")) return "Schnee";
    if (strstr(symbol, "heavyrain")) return "Starker Regen";
    if (strstr(symbol, "lightrain")) return "Leichter Regen";
    if (strstr(symbol, "rain")) return "Regen";
    if (strstr(symbol, "fog")) return "Nebel";
    if (strstr(symbol, "partlycloudy")) return "Wolkig";
    if (strstr(symbol, "fair")) return "Leicht bewölkt";
    if (strstr(symbol, "cloudy")) return "Bewölkt";
    if (strstr(symbol, "clearsky")) return strstr(symbol, "night") ? "Klar" : "Sonnig";
    return "Vorhersage";
}
static void release_visual_image(void) {
    lv_obj_add_flag(visual_image, LV_OBJ_FLAG_HIDDEN);
    if (displayed_slot >= 0) {
        lv_image_cache_drop(&image_descriptors[displayed_slot]);
        pw_assets_release((uint8_t)displayed_slot);
        displayed_slot = -1;
    }
}
static void close_visual(void) {
    if (active_visual == VISUAL_NONE) return;
    lv_obj_add_flag(visual_panel, LV_OBJ_FLAG_HIDDEN);
    release_visual_image();
    pw_assets_cancel();
    requested_asset = PW_ASSET_NONE;
    active_visual = VISUAL_NONE;
    visual_automatic = false;
}
static void open_visual(visual_t kind, bool automatic) {
    close_visual();
    active_visual = kind;
    visual_automatic = automatic;
    lv_obj_remove_flag(visual_panel, LV_OBJ_FLAG_HIDDEN);
    refresh_visual();
}
static void clock_visibility(bool visible) {
    lv_obj_t *objects[] = {clock_hands[0], clock_hands[1], clock_dot, second_dot};
    for (unsigned i = 0; i < 4; ++i) {
        if (visible) lv_obj_remove_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(objects[i], LV_OBJ_FLAG_HIDDEN);
    }
}
static bool dark_photo(int id) {
    const char *name = pw_assets_name(id);
    return strstr(name, "clear-night") || strstr(name, "exceptional") || strstr(name, "hail") ||
           strstr(name, "lightning") || strstr(name, "pouring") || strstr(name, "rainy") || strstr(name, "windy-variant");
}
static void update_photo_clock(int id, time_t now) {
    const bool valid = now > 1700000000;
    clock_visibility(valid && displayed_slot >= 0 && id != PW_ASSET_NONE);
    struct tm local;
    localtime_r(&now, &local);
    const bool dark = id == PW_ASSET_NONE || dark_photo(id);
    const uint32_t strong = dark ? COLOR_TEXT : COLOR_PANEL;
    const uint32_t soft = dark ? COLOR_MUTED : 0x596268;
    const float radians = 0.0174532925f;
    float angles[2] = {(((local.tm_hour % 12) + local.tm_min / 60.0f) * 30 - 90) * radians,
                       ((local.tm_min + local.tm_sec / 60.0f) * 6 - 90) * radians};
    const int lengths[2] = {74, 112};
    for (int i = 0; i < 2; ++i) {
        hand_points[i][0] = (lv_point_precise_t){180, 180};
        hand_points[i][1] = (lv_point_precise_t){180 + (int)lroundf(cosf(angles[i]) * lengths[i]),
                                               180 + (int)lroundf(sinf(angles[i]) * lengths[i])};
        lv_line_set_points(clock_hands[i], hand_points[i], 2);
        lv_obj_set_style_line_color(clock_hands[i], lv_color_hex(i ? strong : soft), 0);
    }
    float angle = (local.tm_sec * 6 - 90) * radians;
    lv_obj_set_pos(second_dot, 178 + (int)lroundf(cosf(angle) * 145), 178 + (int)lroundf(sinf(angle) * 145));
    lv_obj_set_style_bg_color(clock_dot, lv_color_hex(strong), 0);
    lv_obj_set_style_bg_color(second_dot, lv_color_hex(strong), 0);
    lv_obj_set_style_text_color(visual_title, lv_color_hex(strong), 0);
    lv_obj_set_style_text_color(visual_subtitle, lv_color_hex(strong), 0);
    lv_obj_set_style_text_color(visual_caption, lv_color_hex(strong), 0);
    lv_obj_set_style_text_color(visual_hint, lv_color_hex(strong), 0);
}
static void request_picture(int wanted) {
    const int64_t now = esp_timer_get_time();
    if (wanted == PW_ASSET_NONE) {
        release_visual_image();
        if (requested_asset != PW_ASSET_NONE) pw_assets_cancel();
        requested_asset = PW_ASSET_NONE;
        label_text(visual_status, active_visual == VISUAL_AVATAR ?
                   "Keine passende Vorhersage\nNoch keine Empfehlung" : "Kein aktuelles Wetterbild");
        lv_obj_remove_flag(visual_status, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (!assets_available) {
        label_text(visual_status, "Bildspeicher nicht verfügbar");
        lv_obj_remove_flag(visual_status, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (wanted != requested_asset || (displayed_slot < 0 && now - asset_requested_at > 10000000)) {
        release_visual_image();
        requested_asset = wanted;
        if (++asset_generation == 0) ++asset_generation;
        asset_requested_at = now;
        const esp_err_t error = pw_assets_request(wanted, asset_generation);
        label_text(visual_status, error == ESP_OK ? "Bild wird vorbereitet …" : "Bild nicht verfügbar");
        lv_obj_remove_flag(visual_status, LV_OBJ_FLAG_HIDDEN);
    }
    if (displayed_slot < 0 && now - asset_requested_at > 2000000)
        label_text(visual_status, "Bild gerade nicht verfügbar");
}
static void refresh_visual(void) {
    if (active_visual == VISUAL_NONE) return;
    if (app_view.setup_open) { close_visual(); return; }
    const time_t now = time(NULL);
    int wanted = PW_ASSET_NONE;
    char text[180];
    if (active_visual == VISUAL_AVATAR) {
        clock_visibility(false);
        lv_obj_set_style_text_color(visual_title, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_color(visual_subtitle, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_color(visual_caption, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_style_text_color(visual_hint, lv_color_hex(COLOR_MUTED), 0);
        label_text(visual_title, "Dein Wetteroutfit");
        lv_obj_set_style_text_font(visual_subtitle, &pw_font_de_18, 0);
        pw_weather_avatar_t outfit;
        pw_weather_avatar_resolve(&weather, now, app_view.avatar_blond, &outfit);
        if (outfit.valid && outfit.asset_index < PW_ASSET_AVATAR_COUNT) {
            wanted = outfit.asset_index;
            struct tm start, end;
            time_t start_time = outfit.start_utc, end_time = outfit.end_utc;
            localtime_r(&start_time, &start); localtime_r(&end_time, &end);
            snprintf(text, sizeof(text), "%02d:%02d–%02d:%02d%s", start.tm_hour, start.tm_min,
                     end.tm_hour, end.tm_min, outfit.partial ? " · teils unbekannt" : "");
            label_text(visual_subtitle, text);
            if (outfit.partial) lv_obj_set_style_text_font(visual_subtitle, &pw_font_de_14, 0);
            snprintf(text, sizeof(text), "%s\n%s", outfit.scene_label, outfit.outfit_label);
            label_text(visual_caption, text);
        } else {
            label_text(visual_subtitle, "Vorhersage fehlt oder ist zu alt");
            label_text(visual_caption, "");
        }
    } else {
        if (weather.status == PW_WEATHER_READY && weather.current_valid &&
            (weather.current.valid & PW_WEATHER_SYMBOL)) wanted = pw_assets_weather_id(weather.current.symbol);
        label_text(visual_title, "Wetter");
        lv_obj_set_style_text_font(visual_subtitle, &pw_font_de_18, 0);
        label_text(visual_subtitle, "Prognose · MET Norway");
        if (wanted != PW_ASSET_NONE) {
            if (weather.current.valid & PW_WEATHER_TEMPERATURE)
                snprintf(text, sizeof(text), "%s · %.0f °C", condition_name(weather.current.symbol),
                         (double)weather.current.temperature_c);
            else snprintf(text, sizeof(text), "%s", condition_name(weather.current.symbol));
            label_text(visual_caption, text);
        } else label_text(visual_caption, "Keine aktuelle Vorhersage");
        update_photo_clock(wanted, now);
    }
    request_picture(wanted);
}
static void service_visual(void) {
    if (active_visual == VISUAL_NONE) return;
    pw_asset_frame_t frame;
    if (!pw_assets_acquire(&frame)) return;
    if (frame.asset_id != requested_asset || frame.generation != asset_generation) {
        pw_assets_release(frame.slot);
        return;
    }
    release_visual_image();
    lv_image_dsc_t *image = &image_descriptors[frame.slot];
    *image = (lv_image_dsc_t){
        .header = {.magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_RGB565,
                   .w = PW_ASSET_WIDTH, .h = PW_ASSET_HEIGHT, .stride = PW_ASSET_WIDTH * 2},
        .data_size = PW_ASSET_WIDTH * PW_ASSET_HEIGHT * 2, .data = (const uint8_t *)frame.rgb565,
    };
    lv_image_cache_drop(image);
    lv_image_set_src(visual_image, image);
    displayed_slot = frame.slot;
    lv_obj_remove_flag(visual_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(visual_status, LV_OBJ_FLAG_HIDDEN);
    if (active_visual == VISUAL_PHOTO) update_photo_clock(frame.asset_id, time(NULL));
}
static void visual_schedule(int64_t now) {
    const time_t epoch = time(NULL);
    struct tm local = {0};
    const bool valid_time = epoch > 1700000000 && localtime_r(&epoch, &local) != NULL;
    const bool morning = app_view.avatar_enabled && valid_time && local.tm_hour >= 6 && local.tm_hour < 10;
    if (app_view.setup_open || picker_kind != PICKER_NONE) { close_visual(); return; }
    if (active_visual == VISUAL_AVATAR && visual_automatic && !morning) close_visual();
    if (active_visual == VISUAL_PHOTO && visual_automatic && strcmp(app_view.screensaver_mode, "weather_photo")) close_visual();
    if (morning && now - last_activity >= 30000000 &&
        (active_visual == VISUAL_NONE || (active_visual == VISUAL_PHOTO && visual_automatic))) {
        open_visual(VISUAL_AVATAR, true);
    } else if (active_visual == VISUAL_NONE && now - last_activity >= 60000000 &&
               !strcmp(app_view.screensaver_mode, "weather_photo") && weather.status == PW_WEATHER_READY &&
               weather.current_valid && (weather.current.valid & PW_WEATHER_SYMBOL) &&
               pw_assets_weather_id(weather.current.symbol) != PW_ASSET_NONE) {
        open_visual(VISUAL_PHOTO, true);
    }
}
static void refresh_weather(void) {
    pw_weather_get_snapshot(&weather);
    char text[160];
    if (weather.current_valid && (weather.current.valid & PW_WEATHER_TEMPERATURE)) {
        snprintf(text, sizeof(text), "%.0f °C", (double)weather.current.temperature_c);
        label_text(weather_temperature, text);
        label_text(weather_condition, (weather.current.valid & PW_WEATHER_SYMBOL) ? condition_name(weather.current.symbol) : "Vorhersage");
        char wind[40] = "Wind —", rain[40] = "Regen —";
        if (weather.current.valid & PW_WEATHER_WIND)
            snprintf(wind, sizeof(wind), "Wind %.0f km/h", (double)weather.current.wind_mps * 3.6);
        if (weather.current.valid & PW_WEATHER_PRECIPITATION)
            snprintf(rain, sizeof(rain), "Regen %.1f mm/h", (double)weather.current.precipitation_mm);
        snprintf(text, sizeof(text), "%s\n%s", wind, rain);
        label_text(weather_metrics, text);
        char day1[60] = "", day2[60] = "";
        if (weather.day_count > 1 && weather.days[1].temperature_valid)
            snprintf(day1, sizeof(day1), "Morgen %.0f–%.0f°", (double)weather.days[1].temperature_min_c, (double)weather.days[1].temperature_max_c);
        if (weather.day_count > 2 && weather.days[2].temperature_valid)
            snprintf(day2, sizeof(day2), "Danach %.0f–%.0f°", (double)weather.days[2].temperature_min_c, (double)weather.days[2].temperature_max_c);
        snprintf(text, sizeof(text), "%s   %s", day1, day2);
        label_text(weather_days, text);
        label_text(weather_source, weather.status != PW_WEATHER_READY ? "Letzter Stand · MET" : "Prognose · MET Norway");
    } else {
        label_text(weather_temperature, "—");
        const char *state = "Wetter wird geladen";
        if (!app_view.weather_enabled || weather.status == PW_WEATHER_UNCONFIGURED) state = "Standort noch offen";
        else if (weather.status == PW_WEATHER_WAITING_NETWORK) state = "WLAN verbinden";
        else if (weather.status == PW_WEATHER_WAITING_CLOCK) state = "Uhrzeit wird geladen";
        else if (weather.status == PW_WEATHER_ERROR) state = "Wetter gerade nicht verfügbar";
        else if (weather.status == PW_WEATHER_SUSPENDED) state = "Wetter pausiert kurz";
        label_text(weather_condition, state);
        label_text(weather_metrics, !app_view.weather_enabled ? "Standort auf der Webseite wählen" : "Wir versuchen es automatisch erneut.");
        label_text(weather_days, "");
        label_text(weather_source, "");
    }
}

static bool wifi_escape(char *output, size_t capacity, const char *input_text) {
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)input_text; *p; ++p) {
        if (*p == ';' || *p == ',' || *p == ':' || *p == '\\' || *p == '"') {
            if (used + 2 >= capacity) return false;
            output[used++] = '\\';
        } else if (used + 1 >= capacity) return false;
        output[used++] = (char)*p;
    }
    output[used] = '\0';
    return true;
}
static void refresh_setup(void) {
    if (!app_view.setup_open && !app_view.lan_open) {
        if (setup_visible) {
            lv_obj_add_flag(setup_panel, LV_OBJ_FLAG_HIDDEN);
            setup_visible = false;
            setup_step = 0;
            memset(qr_payload, 0, sizeof(qr_payload));
            lv_qrcode_update(qr, "", 0); // Remove credential pattern when setup closes.
        }
        return;
    }
    if (!setup_visible) {
        setup_visible = true;
        setup_step = 0;
        lv_obj_remove_flag(setup_panel, LV_OBJ_FLAG_HIDDEN);
        last_activity = esp_timer_get_time();
    }
    char data[sizeof(qr_payload)], text[80];
    if (app_view.lan_open && !app_view.setup_open) {
        snprintf(data, sizeof(data), "http://%s/#content", app_view.ip);
        label_text(setup_title, "Web freigeben");
        label_text(setup_hint, "Pilot · unverschlüsselt");
        snprintf(text, sizeof(text), "%s%s", app_view.lan_code[0] ? "Code: " : "Verbunden", app_view.lan_code);
        label_text(setup_detail, text);
        lv_obj_add_flag(lv_obj_get_parent(setup_next_text), LV_OBJ_FLAG_HIDDEN);
        snprintf(text, sizeof(text), "Heimnetz · %lu:%02lu", (unsigned long)app_view.lan_seconds_left / 60, (unsigned long)app_view.lan_seconds_left % 60);
        label_text(setup_timer, text);
        if (strcmp(data, qr_payload) && lv_qrcode_update(qr, data, strlen(data)) == LV_RESULT_OK)
            snprintf(qr_payload, sizeof(qr_payload), "%s", data);
        memset(data, 0, sizeof(data)); return;
    }
    label_text(setup_hint, "Mit dem Handy scannen");
    lv_obj_remove_flag(lv_obj_get_parent(setup_next_text), LV_OBJ_FLAG_HIDDEN);
    if (setup_step == 0) {
        char ssid[70], password[50];
        if (!wifi_escape(ssid, sizeof(ssid), app_view.setup_ssid) ||
            !wifi_escape(password, sizeof(password), app_view.setup_password)) return;
        const int count = snprintf(data, sizeof(data), "WIFI:T:WPA;S:%s;P:%s;;", ssid, password);
        if (count < 0 || count >= sizeof(data)) return;
        label_text(setup_title, "WLAN verbinden");
        label_text(setup_detail, app_view.setup_ssid);
        label_text(setup_next_text, "Weiter");
        memset(password, 0, sizeof(password));
    } else {
        snprintf(data, sizeof(data), "http://192.168.4.1/");
        label_text(setup_title, "Webseite öffnen");
        label_text(setup_detail, "192.168.4.1");
        label_text(setup_next_text, "Zurück");
    }
    snprintf(text, sizeof(text), "%d von 2 · %lu:%02lu", setup_step + 1, (unsigned long)app_view.setup_seconds_left / 60,
             (unsigned long)app_view.setup_seconds_left % 60);
    label_text(setup_timer, text);
    if (strcmp(data, qr_payload)) {
        const lv_result_t result = lv_qrcode_update(qr, data, strlen(data));
        if (result == LV_RESULT_OK) snprintf(qr_payload, sizeof(qr_payload), "%s", data);
        else label_text(setup_detail, "QR nicht verfügbar");
    }
    memset(data, 0, sizeof(data));
}
static void refresh_view(void) {
    pw_app_get_view(&app_view);
    char text[160];
    time_t now = time(NULL);
    if (now > 1700000000) {
        struct tm local;
        localtime_r(&now, &local);
        strftime(text, sizeof(text), "%H:%M", &local);
        label_text(clock_label, text);
    }
    label_text(network_label, app_view.connected ? "Mit WLAN verbunden" : app_view.connecting ? "WLAN wird verbunden …" : "Noch nicht im WLAN");
    snprintf(text, sizeof(text), "%s", app_view.connected ? app_view.ip : "Einrichtung unten öffnen");
    label_text(device_detail, text);
    snprintf(text, sizeof(text), "Helligkeit %u %%", app_view.brightness);
    label_text(brightness_label, text);
    refresh_weather();
    refresh_music();
    refresh_setup();
    refresh_visual();
}

static void process_input(int64_t now) {
    static bool previous_pressed;
    if (pw_board_poll_input(&input) != ESP_OK) return;
    if (!input.touch_pressed) {
        press_started = 0;
        long_press_fired = false;
        long_press_cancelled = false;
        suppress_touch = false;
    } else {
        last_activity = now;
        if (!previous_pressed && !display_dark && active_visual != VISUAL_NONE) {
            close_visual();
            suppress_touch = true;
            lv_indev_wait_release(pointer);
        }
        if (!previous_pressed) {
            press_started = now;
            press_x = input.touch_x; press_y = input.touch_y;
            if (display_dark) {
                suppress_touch = true;
                lv_indev_wait_release(pointer);
            }
        }
        int dx = (int)input.touch_x - press_x, dy = (int)input.touch_y - press_y;
        if (dx * dx + dy * dy > 400) long_press_cancelled = true;
        if (!setup_visible && !suppress_touch && !long_press_fired && !long_press_cancelled &&
            press_started && now - press_started >= 3000000) {
            long_press_fired = true;
            suppress_touch = true;
            lv_indev_wait_release(pointer);
            queue_action(ACTION_OPEN_SETUP);
        }
    }
    previous_pressed = input.touch_pressed;
    if (input.rotation_delta) {
        last_activity = now;
        if (active_visual == VISUAL_PHOTO && visual_automatic) close_visual();
        else if (!display_dark && !setup_visible && active_visual == VISUAL_NONE && picker_kind == PICKER_NONE && active_page == PAGE_MUSIC) music_rotate(input.rotation_delta);
        else if (!display_dark && !setup_visible && active_visual == VISUAL_NONE && picker_kind == PICKER_NONE && active_page == PAGE_DEVICE) {
            // Limit the value passed to application arithmetic; the app clamps
            // the brightness range and coalesces persistent writes.
            const int delta = input.rotation_delta > 100 ? 100 : input.rotation_delta < -100 ? -100 : input.rotation_delta;
            pw_app_adjust_brightness(delta);
            acknowledge();
            refresh_view();
        }
    }
    visual_schedule(now);
    const int64_t inactive = now - last_activity;
    uint8_t brightness = app_view.brightness < 5 ? 5 : app_view.brightness;
    if (setup_visible && brightness < 30) brightness = 30; // Keep QR easy to scan.
    const bool morning_visible = active_visual == VISUAL_AVATAR && visual_automatic;
    if (!setup_visible && !morning_visible && inactive > 300000000) brightness = 0;
    else if (!setup_visible && inactive > 60000000) brightness = brightness > 15 ? 15 : brightness;
    display_dark = brightness == 0;
    if (first_frame_done && output_brightness != brightness) {
        if (pw_board_set_brightness(brightness) == ESP_OK) output_brightness = brightness;
    }
}
static void control_worker(void *context) {
    (void)context;
    action_t action;
    for (;;) {
        if (xQueueReceive(action_queue, &action, portMAX_DELAY) != pdTRUE) continue;
        esp_err_t result = ESP_OK;
        if (action == ACTION_OPEN_SETUP) result = pw_app_open_setup();
        else if (action == ACTION_OPEN_LAN) result = pw_app_open_lan_lab();
        else pw_app_close_setup();
        (void)xQueueSend(action_result_queue, &result, 0);
    }
}
static void ui_worker(void *context) {
    (void)context;
    lv_init();
    lv_tick_set_cb(tick_ms);
    display = lv_display_create(PW_BOARD_WIDTH, PW_BOARD_HEIGHT);
    if (!display) {
        ESP_LOGE(TAG, "LVGL display allocation failed");
        ui_task_handle = NULL; xSemaphoreGive(ui_started); vTaskDelete(NULL); return;
    }
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, draw_buffer1, draw_buffer2, DRAW_BUFFER_BYTES, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush_display);
    lv_display_set_flush_wait_cb(display, flush_wait);
    pointer = lv_indev_create();
    if (!pointer) {
        ESP_LOGE(TAG, "LVGL input allocation failed");
        lv_display_delete(display); display = NULL;
        ui_task_handle = NULL; xSemaphoreGive(ui_started); vTaskDelete(NULL); return;
    }
    lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, read_touch);
    lv_indev_set_display(pointer, display);
    lv_indev_set_long_press_time(pointer, 3000);
    assets_available = pw_assets_init() == ESP_OK;
    create_ui();
    refresh_view();
    if (spotify_view.enabled && spotify_view.linked) show_page(PAGE_MUSIC);
    else if (app_view.weather_enabled) show_page(PAGE_WEATHER);
    last_activity = esp_timer_get_time();
    startup_result = ESP_OK;
    xSemaphoreGive(ui_started);
    for (;;) {
        const int64_t now = esp_timer_get_time();
        esp_err_t result;
        if (flush_pending && xQueueReceive(completed_queue, &result, 0) == pdTRUE) consume_completion(result);
        if (xQueueReceive(action_result_queue, &result, 0) == pdTRUE) {
            if (result != ESP_OK) {
                label_text(feedback_label, "Bitte erneut öffnen");
                lv_obj_remove_flag(feedback_label, LV_OBJ_FLAG_HIDDEN);
                feedback_expires = now + 3000000;
            } else lv_obj_add_flag(feedback_label, LV_OBJ_FLAG_HIDDEN);
        }
        if (feedback_expires && now >= feedback_expires) {
            lv_obj_add_flag(feedback_label, LV_OBJ_FLAG_HIDDEN);
            feedback_expires = 0;
        }
        process_input(now);
        service_visual();
        if (now - last_view_refresh >= 500000) {
            refresh_view();
            last_view_refresh = now;
        }
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms > 5) delay_ms = 5;
        TickType_t ticks = pdMS_TO_TICKS(delay_ms);
        vTaskDelay(ticks > 0 ? ticks : 1);
    }
}

esp_err_t pw_ui_init(void) {
    if (initialized) return ESP_ERR_INVALID_STATE;
    ui_started = xSemaphoreCreateBinary();
    completed_queue = xQueueCreate(2, sizeof(esp_err_t));
    action_queue = xQueueCreate(2, sizeof(action_t));
    action_result_queue = xQueueCreate(2, sizeof(esp_err_t));
    draw_buffer1 = heap_caps_malloc(DRAW_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    draw_buffer2 = heap_caps_malloc(DRAW_BUFFER_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!draw_buffer1) draw_buffer1 = heap_caps_malloc(DRAW_BUFFER_BYTES, MALLOC_CAP_8BIT);
    if (!draw_buffer2) draw_buffer2 = heap_caps_malloc(DRAW_BUFFER_BYTES, MALLOC_CAP_8BIT);
    if (!ui_started || !completed_queue || !action_queue || !action_result_queue || !draw_buffer1 || !draw_buffer2) goto fail;
    if (xTaskCreate(control_worker, "pw_ui_control", 4096, NULL, 3, &control_task_handle) != pdPASS) goto fail;
    if (xTaskCreate(ui_worker, "pw_ui", 8192, NULL, 4, &ui_task_handle) != pdPASS) goto fail;
    initialized = true;
    // Startup-only wait: callers must not report a live UI merely because task
    // creation succeeded. Runtime never depends on this semaphore.
    if (xSemaphoreTake(ui_started, pdMS_TO_TICKS(5000)) != pdTRUE) return ESP_ERR_TIMEOUT;
    return startup_result;
fail:
    if (ui_started) { vSemaphoreDelete(ui_started); ui_started = NULL; }
    if (control_task_handle) { vTaskDelete(control_task_handle); control_task_handle = NULL; }
    if (completed_queue) { vQueueDelete(completed_queue); completed_queue = NULL; }
    if (action_queue) { vQueueDelete(action_queue); action_queue = NULL; }
    if (action_result_queue) { vQueueDelete(action_result_queue); action_result_queue = NULL; }
    heap_caps_free(draw_buffer1); heap_caps_free(draw_buffer2);
    draw_buffer1 = draw_buffer2 = NULL;
    return ESP_ERR_NO_MEM;
}

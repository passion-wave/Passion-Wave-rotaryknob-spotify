// SPDX-License-Identifier: MIT
#include "pw_app.h"
#include "pw_spotify.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct { bool hidden, enabled; char label[200]; } lv_obj_t;
typedef struct { void *user_data; } lv_event_t;
#define LV_OBJ_FLAG_HIDDEN 1
typedef enum { PICKER_NONE, PICKER_DEVICES, PICKER_FAVORITES } picker_t;
static picker_t picker_kind;
static lv_obj_t objects[9];
static lv_obj_t *picker_panel = &objects[0], *picker_title = &objects[1], *picker_hint = &objects[2];
static lv_obj_t *picker_rows[3] = {&objects[3], &objects[4], &objects[5]};
static lv_obj_t *picker_prev = &objects[6], *picker_next = &objects[7];
static char picker_ids[3][PW_SPOTIFY_DEVICE_ID_BYTES];
static pw_app_favorite_t picker_favorites[3];
static size_t picker_offset, picker_total;
static uint32_t picker_session, picker_revision, picker_snapshot_revision;
static pw_spotify_command_t picker_target;
static int64_t picker_refresh_at, last_activity, clock_us;
static pw_spotify_snapshot_t spotify_view;
static pw_app_view_t app_view;
static struct { bool touch_pressed; } input;
static unsigned checks, renders, favorite_reads, selected_count;
static char selected_id[PW_SPOTIFY_DEVICE_ID_BYTES];
static uint32_t selected_session, provider_session;
static esp_err_t select_result;
#define CHECK(x) do { checks++; assert(x); } while (0)
static void refresh_picker(void);
static void refresh_music(void) {}
static int64_t esp_timer_get_time(void) { return clock_us; }
static void *lv_event_get_user_data(lv_event_t *event) { return event->user_data; }
static void lv_obj_add_flag(lv_obj_t *obj, int flag) { (void)flag; obj->hidden = true; }
static void lv_obj_remove_flag(lv_obj_t *obj, int flag) { (void)flag; obj->hidden = false; }
static lv_obj_t *lv_obj_get_child(lv_obj_t *obj, int index) { (void)index; return obj; }
static void label_text(lv_obj_t *obj, const char *text) {
    snprintf(obj->label, sizeof obj->label, "%s", text);
    if (obj == picker_title) renders++;
}
static void enabled_button(lv_obj_t *obj, bool enabled) { obj->enabled = enabled; }
static void acknowledge(void) { last_activity = clock_us; }
static void command_feedback(esp_err_t result) { (void)result; }
static void music_feedback(const char *text) { (void)text; }
esp_err_t pw_spotify_select_device(const char *id, uint32_t session) {
    selected_count++;
    snprintf(selected_id, sizeof selected_id, "%s", id);
    selected_session = session;
    return session == provider_session ? select_result : ESP_ERR_INVALID_STATE;
}
size_t pw_app_get_favorites(size_t offset, pw_app_favorite_t *items, size_t capacity,
                            uint32_t *revision) {
    favorite_reads++;
    *revision = 14;
    for (size_t i = 0; i < capacity && offset + i < 5; i++) {
        snprintf(items[i].id, sizeof items[i].id, "favorite-%u", (unsigned)(offset + i));
        snprintf(items[i].name, sizeof items[i].name, "Saved %u", (unsigned)(offset + i));
        items[i].playable = true;
    }
    return 5;
}
esp_err_t pw_app_play_favorite(const char *id, uint32_t revision,
                              const pw_spotify_command_t *target, uint32_t *request_id) {
    (void)id; (void)revision; (void)target; (void)request_id;
    return ESP_ERR_INVALID_STATE;
}
#include "production.inc"

static void reset(void) {
    memset(objects, 0, sizeof objects);
    memset(&spotify_view, 0, sizeof spotify_view);
    memset(&app_view, 0, sizeof app_view);
    memset(picker_ids, 0, sizeof picker_ids);
    memset(picker_favorites, 0, sizeof picker_favorites);
    spotify_view.enabled = spotify_view.linked = spotify_view.can_play = true;
    spotify_view.session = picker_session = provider_session = 7;
    spotify_view.revision = 10;
    picker_kind = PICKER_DEVICES;
    picker_offset = picker_total = 0;
    picker_snapshot_revision = 0;
    picker_refresh_at = last_activity = 0;
    clock_us = 1000000;
    input.touch_pressed = false;
    renders = favorite_reads = selected_count = 0;
    select_result = ESP_OK;
    selected_id[0] = 0;
}
static void devices(unsigned count) {
    memset(spotify_view.devices, 0, sizeof spotify_view.devices);
    spotify_view.device_count = count;
    for (unsigned i = 0; i < count; i++) {
        snprintf(spotify_view.devices[i].id, sizeof spotify_view.devices[i].id, "device-%u", i);
        snprintf(spotify_view.devices[i].name, sizeof spotify_view.devices[i].name, "Speaker %u", i);
    }
    spotify_view.revision++;
}
static void test_delayed_discovery(void) {
    reset();
    refresh_picker();
    picker_refresh_at = 3000000;
    clock_us = 3100000;
    service_picker_refresh();
    CHECK(picker_refresh_at == 0 && picker_total == 0 && renders == 2);
    clock_us = 9000000; /* HTTPS completes long after the old one-shot timer. */
    devices(2);
    service_picker_refresh();
    CHECK(picker_total == 2 && renders == 3 && !picker_rows[0]->hidden);
    CHECK(!strcmp(picker_ids[0], "device-0"));
    service_picker_refresh();
    CHECK(renders == 3); /* Unchanged snapshots do not rebind rows. */
}
static void test_touch_identity_and_pending_release(void) {
    reset(); devices(1); refresh_picker();
    input.touch_pressed = true;
    last_activity = clock_us = 2000000;
    strcpy(spotify_view.devices[0].id, "replacement");
    spotify_view.revision++;
    service_picker_refresh();
    CHECK(!strcmp(picker_ids[0], "device-0"));
    input.touch_pressed = false;
    clock_us += 1000;
    service_picker_refresh(); /* A release is sampled before LVGL handles its click. */
    CHECK(!strcmp(picker_ids[0], "device-0"));
    lv_event_t click = {.user_data = (void *)(uintptr_t)0};
    select_result = ESP_ERR_INVALID_STATE; /* Old device vanished in provider. */
    picker_row_clicked(&click);
    CHECK(selected_count == 1 && selected_session == 7 && !strcmp(selected_id, "device-0"));
    CHECK(picker_kind == PICKER_DEVICES);
    clock_us = last_activity + 750001;
    service_picker_refresh();
    CHECK(!strcmp(picker_ids[0], "replacement"));
}
static void test_session_and_setup_invalidation(void) {
    reset(); devices(1); refresh_picker();
    input.touch_pressed = true;
    provider_session = spotify_view.session = 8;
    service_picker_refresh();
    CHECK(picker_kind == PICKER_NONE && picker_panel->hidden && !picker_ids[0][0]);
    lv_event_t click = {.user_data = (void *)(uintptr_t)0};
    picker_row_clicked(&click);
    CHECK(selected_count == 0);
    reset(); devices(1); refresh_picker();
    provider_session = 8; /* Provider changes account before UI's next snapshot. */
    picker_row_clicked(&click);
    CHECK(selected_session == 7 && picker_kind == PICKER_DEVICES);
    reset(); devices(1); refresh_picker(); app_view.setup_open = true;
    service_picker_refresh(); CHECK(picker_kind == PICKER_NONE);
    reset(); devices(1); refresh_picker(); spotify_view.linked = false;
    service_picker_refresh(); CHECK(picker_kind == PICKER_NONE);
}
static void test_pagination_and_favorites(void) {
    reset(); devices(8); picker_offset = 6; refresh_picker();
    CHECK(picker_offset == 6 && !strcmp(picker_ids[0], "device-6"));
    devices(4); service_picker_refresh();
    CHECK(picker_offset == 3 && !strcmp(picker_ids[0], "device-3"));
    CHECK(picker_rows[1]->hidden && !picker_next->enabled);
    devices(0); service_picker_refresh();
    CHECK(picker_offset == 0 && picker_total == 0 && !picker_ids[0][0]);
    reset(); picker_kind = PICKER_FAVORITES; refresh_picker();
    CHECK(favorite_reads == 1 && !strcmp(picker_ids[0], "favorite-0"));
    lv_event_t next = {.user_data = (void *)(uintptr_t)1};
    picker_page_clicked(&next);
    CHECK(picker_offset == 3 && !strcmp(picker_ids[0], "favorite-3"));
    unsigned prior = favorite_reads;
    devices(1); clock_us += 10000000; picker_refresh_at = 1;
    service_picker_refresh();
    CHECK(favorite_reads == prior && picker_offset == 3 && !strcmp(picker_ids[0], "favorite-3"));
}
int main(void) {
    test_delayed_discovery();
    test_touch_identity_and_pending_release();
    test_session_and_setup_invalidation();
    test_pagination_and_favorites();
    printf("Picker: %u assertions passed; production functions, simulated LVGL/provider/time\n", checks);
    return 0;
}

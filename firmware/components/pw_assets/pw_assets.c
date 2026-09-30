// SPDX-FileCopyrightText: 2026 Passion Wave
// SPDX-License-Identifier: MIT
#include "pw_assets_internal.h"
#include "pw_asset_decode.h"
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum { SLOT_FREE, SLOT_DECODING, SLOT_READY, SLOT_DISPLAY } slot_state_t;
typedef struct {
    uint16_t *pixels;
    slot_state_t state;
    int id;
    uint32_t generation, sequence;
} slot_t;
static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static slot_t slots[2];
static uint8_t *workspace;
static TaskHandle_t worker_task;
static pw_assets_stats_t stats = {.last_failed_id = PW_ASSET_NONE};
static int desired_id = PW_ASSET_NONE;
static uint32_t desired_generation, desired_sequence, attempted_sequence;
static bool ready;

static bool cancelled(void *context) {
    const slot_t *slot = context;
    portENTER_CRITICAL(&lock);
    const bool result = desired_id != slot->id || desired_sequence != slot->sequence;
    portEXIT_CRITICAL(&lock);
    return result;
}
static void decode_worker(void *unused) {
    (void)unused;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        for (;;) {
            int target = -1;
            portENTER_CRITICAL(&lock);
            if (desired_id != PW_ASSET_NONE && desired_sequence != attempted_sequence) {
                for (int i = 0; i < 2; ++i) if (slots[i].state == SLOT_FREE) { target = i; break; }
            }
            if (target >= 0) {
                slots[target].state = SLOT_DECODING;
                slots[target].id = desired_id;
                slots[target].generation = desired_generation;
                slots[target].sequence = desired_sequence;
                stats.decoding = true;
            }
            portEXIT_CRITICAL(&lock);
            if (target < 0) break;
            slot_t *slot = &slots[target];
            const pw_asset_source_t *source = &pw_asset_sources[slot->id];
            const int64_t start = esp_timer_get_time();
            const bool success = pw_asset_decode(source->jpeg, source->bytes, slot->pixels,
                PW_ASSET_WIDTH * PW_ASSET_HEIGHT, workspace, PW_ASSET_DECODE_WORKSPACE, cancelled, slot);
            const uint32_t elapsed = (uint32_t)((esp_timer_get_time() - start) / 1000);
            portENTER_CRITICAL(&lock);
            stats.decoding = false;
            if (elapsed > stats.maximum_decode_ms) stats.maximum_decode_ms = elapsed;
            if (desired_sequence != slot->sequence || desired_id != slot->id) {
                slot->state = SLOT_FREE;
                stats.cancelled++;
            } else {
                attempted_sequence = slot->sequence;
                if (success) { slot->state = SLOT_READY; stats.completed++; }
                else { slot->state = SLOT_FREE; stats.failed++; stats.last_failed_id = slot->id; }
            }
            portEXIT_CRITICAL(&lock);
        }
    }
}

esp_err_t pw_assets_init(void) {
    if (ready) return ESP_OK;
    for (unsigned i = 0; i < 2; ++i) {
        slots[i].pixels = heap_caps_malloc(PW_ASSET_WIDTH * PW_ASSET_HEIGHT * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!slots[i].pixels) goto fail;
    }
    workspace = heap_caps_malloc(PW_ASSET_DECODE_WORKSPACE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!workspace) goto fail;
    if (xTaskCreatePinnedToCore(decode_worker, "pw_asset_decode", 8192, NULL, 1, &worker_task, 0) != pdPASS) goto fail;
    ready = true;
    stats.ready = true;
    return ESP_OK;
fail:
    for (unsigned i = 0; i < 2; ++i) { heap_caps_free(slots[i].pixels); slots[i].pixels = NULL; }
    heap_caps_free(workspace); workspace = NULL;
    return ESP_ERR_NO_MEM;
}
esp_err_t pw_assets_request(int id, uint32_t generation) {
    if (!ready) return ESP_ERR_INVALID_STATE;
    if (id < 0 || id >= PW_ASSET_COUNT || !generation) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&lock);
    if (desired_id != id || desired_generation != generation) {
        desired_id = id;
        desired_generation = generation;
        if (++desired_sequence == 0) ++desired_sequence;
        for (unsigned i = 0; i < 2; ++i) if (slots[i].state == SLOT_READY) slots[i].state = SLOT_FREE;
    }
    portEXIT_CRITICAL(&lock);
    xTaskNotifyGive(worker_task);
    return ESP_OK;
}
void pw_assets_cancel(void) {
    if (!ready) return;
    portENTER_CRITICAL(&lock);
    desired_id = PW_ASSET_NONE;
    if (++desired_sequence == 0) ++desired_sequence;
    for (unsigned i = 0; i < 2; ++i) if (slots[i].state == SLOT_READY) slots[i].state = SLOT_FREE;
    portEXIT_CRITICAL(&lock);
    xTaskNotifyGive(worker_task);
}
bool pw_assets_acquire(pw_asset_frame_t *frame) {
    if (!ready || !frame) return false;
    bool found = false;
    portENTER_CRITICAL(&lock);
    for (unsigned i = 0; i < 2; ++i) {
        if (slots[i].state == SLOT_READY && slots[i].sequence == desired_sequence && slots[i].id == desired_id) {
            slots[i].state = SLOT_DISPLAY;
            *frame = (pw_asset_frame_t){slots[i].id, slots[i].generation, (uint8_t)i, slots[i].pixels};
            found = true;
            break;
        }
    }
    portEXIT_CRITICAL(&lock);
    return found;
}
void pw_assets_release(uint8_t slot) {
    if (!ready || slot >= 2) return;
    portENTER_CRITICAL(&lock);
    if (slots[slot].state == SLOT_DISPLAY) slots[slot].state = SLOT_FREE;
    portEXIT_CRITICAL(&lock);
    xTaskNotifyGive(worker_task);
}
void pw_assets_get_stats(pw_assets_stats_t *output) {
    if (!output) return;
    portENTER_CRITICAL(&lock);
    *output = stats;
    portEXIT_CRITICAL(&lock);
}

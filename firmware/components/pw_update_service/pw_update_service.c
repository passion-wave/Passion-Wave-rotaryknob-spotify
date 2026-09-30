// SPDX-License-Identifier: MIT
// Deliberately implements authenticated paired STAGING, not an unqualified activation path.
#include "pw_update_service.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "pw_update_wire.h"
#include "pw_weather.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    nvs_handle_t nvs;
    bool journal_open, ota_open;
    esp_ota_handle_t ota;
    const esp_partition_t *staged[2];
    uint32_t image_bytes[2];
} flash_context_t;
static flash_context_t flash;
static SemaphoreHandle_t mutex;
static QueueHandle_t outgoing, acknowledgements;
static pw_update_stage_t *stage;
static pw_stage_config_t local_config;
static pw_update_service_view_t status;
static uint32_t transport_session, command_sequence, peer_nonce, local_health;
static int64_t peer_seen, transfer_deadline;
static bool boot_journal_present;
static pw_update_upload_read_t upload_read;
static pw_update_upload_reply_t upload_reply;
static void *upload_context;
static uint32_t upload_bytes;

__attribute__((weak)) const pw_stage_config_t *pw_update_service_provisioned_config(void) {
    return NULL;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put32(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; i++)
        p[i] = (uint8_t)(value >> (i * 8));
}
static int journal_load(void *context, uint8_t out[PW_STAGE_JOURNAL_BYTES]) {
    flash_context_t *f = context;
    if (!f->journal_open)
        return -1;
    nvs_type_t type;
    esp_err_t e = nvs_find_key(f->nvs, "stage", &type);
    if (e == ESP_ERR_NVS_NOT_FOUND)
        return 0;
    boot_journal_present = true;
    if (e != ESP_OK || type != NVS_TYPE_BLOB)
        return -1;
    size_t length = PW_STAGE_JOURNAL_BYTES;
    e = nvs_get_blob(f->nvs, "stage", out, &length);
    return e == ESP_OK && length == PW_STAGE_JOURNAL_BYTES ? 1 : -1;
}
static bool journal_save(void *context, const uint8_t data[PW_STAGE_JOURNAL_BYTES]) {
    flash_context_t *f = context;
    return f->journal_open &&
           nvs_set_blob(f->nvs, "stage", data, PW_STAGE_JOURNAL_BYTES) == ESP_OK &&
           nvs_commit(f->nvs) == ESP_OK;
}
static bool image_begin(void *context, pw_update_role_t role, uint32_t bytes) {
    flash_context_t *f = context;
    if ((unsigned)role > 1 || !f->staged[role] || !bytes || bytes > f->staged[role]->size ||
        f->staged[role] == esp_ota_get_running_partition())
        return false;
    f->image_bytes[role] = bytes;
    if (role == PW_UPDATE_COMPANION) {
        uint32_t erase_bytes = (bytes + 4095u) & ~4095u;
        return erase_bytes <= f->staged[role]->size &&
               esp_partition_erase_range(f->staged[role], 0, erase_bytes) == ESP_OK;
    }
    if (f->ota_open)
        return false;
    f->ota_open = esp_ota_begin(f->staged[role], bytes, &f->ota) == ESP_OK;
    return f->ota_open;
}
static bool image_write(void *context, pw_update_role_t role, uint32_t offset, const uint8_t *data,
                        size_t bytes) {
    flash_context_t *f = context;
    if ((unsigned)role > 1 || offset > f->image_bytes[role] ||
        bytes > f->image_bytes[role] - offset)
        return false;
    if (role == PW_UPDATE_COMPANION)
        return esp_partition_write(f->staged[role], offset, data, bytes) == ESP_OK;
    return f->ota_open && esp_ota_write(f->ota, data, bytes) == ESP_OK;
}
static bool image_finish(void *context, pw_update_role_t role) {
    flash_context_t *f = context;
    if (role == PW_UPDATE_COMPANION)
        return true; /* Full ESP32 verification belongs on ESP32. */
    if (!f->ota_open)
        return false;
    f->ota_open = false;
    return esp_ota_end(f->ota) == ESP_OK; /* Includes full ESP image verification. */
}
static bool image_read(void *context, pw_update_role_t role, uint32_t offset, uint8_t *data,
                       size_t bytes) {
    flash_context_t *f = context;
    return (unsigned)role <= 1 && f->staged[role] && offset <= f->image_bytes[role] &&
           bytes <= f->image_bytes[role] - offset &&
           esp_partition_read(f->staged[role], offset, data, bytes) == ESP_OK;
}
static void image_abort(void *context) {
    flash_context_t *f = context;
    if (f->ota_open) {
        esp_ota_abort(f->ota);
        f->ota_open = false;
    }
}
static void snapshot(const char *reason) {
    pw_stage_view_t view;
    pw_update_stage_view(stage, &view);
    xSemaphoreTake(mutex, portMAX_DELAY);
    status.job = view;
    if (view.phase == PW_STAGE_LOCKED)
        status.upload_enabled = false;
    if (reason)
        strlcpy(status.reason, reason, sizeof status.reason);
    xSemaphoreGive(mutex);
}
esp_err_t pw_update_service_init(void) {
    mutex = xSemaphoreCreateMutex();
    outgoing = xQueueCreate(1, sizeof(pw_frame_t));
    acknowledgements = xQueueCreate(4, sizeof(pw_frame_t));
    if (!mutex || !outgoing || !acknowledgements)
        return ESP_ERR_NO_MEM;
    status.job.phase = PW_STAGE_LOCKED;
    strlcpy(status.reason, "trust_not_provisioned", sizeof status.reason);
    flash.journal_open =
        nvs_open_from_partition("journal", "pw_ota", NVS_READWRITE, &flash.nvs) == ESP_OK;
    flash.staged[0] = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "pair_stage");
    flash.staged[1] = esp_ota_get_next_update_partition(NULL);
    const pw_stage_config_t *provisioned = pw_update_service_provisioned_config();
    bool qualified = provisioned && provisioned->policy_qualified && flash.journal_open &&
                     flash.staged[0] && flash.staged[1] &&
                     flash.staged[1] != esp_ota_get_running_partition();
    if (qualified) {
        local_config = *provisioned;
        qualified = local_config.public_key && local_config.public_key_bytes &&
                    local_config.key_id &&
                    local_config.policy.roles[0].app_slot_bytes <= flash.staged[0]->size &&
                    local_config.policy.roles[1].app_slot_bytes == flash.staged[1]->size;
        /* The actual local app version, not a self-asserted manifest version. */
        local_config.policy.roles[1].running_version = esp_app_get_description()->version;
    }
    pw_stage_ops_t ops = {.context = &flash,
                          .load = journal_load,
                          .save = journal_save,
                          .begin = image_begin,
                          .write = image_write,
                          .finish = image_finish,
                          .read = image_read,
                          .abort = image_abort};
    stage = pw_update_stage_create(qualified ? &local_config : NULL, &ops);
    if (!stage)
        return ESP_ERR_NO_MEM;
    snapshot(NULL);
    status.upload_enabled = qualified && status.job.phase != PW_STAGE_LOCKED;
    status.activation_enabled = false; /* No provisionable escape hatch. */
    if (!flash.journal_open)
        strlcpy(status.reason, "journal_unavailable", sizeof status.reason);
    else if (status.upload_enabled)
        strlcpy(status.reason, "activation_not_qualified", sizeof status.reason);
    return ESP_OK;
}
void pw_update_service_get_view(pw_update_service_view_t *out) {
    if (!out)
        return;
    if (!mutex) {
        memset(out, 0, sizeof *out);
        out->job.phase = PW_STAGE_LOCKED;
        strlcpy(out->reason, "uninitialized", sizeof out->reason);
        return;
    }
    xSemaphoreTake(mutex, portMAX_DELAY);
    *out = status;
    xSemaphoreGive(mutex);
}
void pw_update_service_transport_session(uint32_t nonce) {
    if (!mutex || !nonce)
        return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (!transport_session)
        transport_session = nonce;
    xSemaphoreGive(mutex);
}
void pw_update_service_receive(const pw_frame_t *frame) {
    if (!frame || !mutex || frame->role != PW_ROLE_COMPANION)
        return;
    if (frame->kind == PW_MSG_OTA_ACK && frame->length == PW_OTA_ACK_BYTES)
        xQueueSend(acknowledgements, frame, 0);
    else if (frame->kind == PW_MSG_OTA_BOOT_REPORT && frame->length == PW_OTA_BOOT_REPORT_BYTES &&
             frame->payload[0] == PW_UPDATE_WIRE_VERSION &&
             le32(frame->payload + 18) == frame->session && frame->session) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        peer_nonce = frame->session;
        peer_seen = esp_timer_get_time();
        status.peer_phase = frame->payload[17];
        xSemaphoreGive(mutex);
    }
}
bool pw_update_service_take_frame(pw_frame_t *out) {
    return outgoing && out && xQueueReceive(outgoing, out, 0) == pdTRUE;
}
bool pw_update_service_may_finalize_boot(void) {
    if (!mutex || !stage)
        return false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool clear = flash.journal_open && !boot_journal_present && !status.busy && !status.job.total &&
                 (status.job.phase == PW_STAGE_IDLE || status.job.phase == PW_STAGE_LOCKED);
    xSemaphoreGive(mutex);
    return clear;
}
void pw_update_service_local_health(uint32_t flags) {
    if (mutex) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        local_health = flags;
        xSemaphoreGive(mutex);
    }
}
/* Stop/wait with identical bytes/sequence on retry; CRC/ACK is not a new trust source. */
static bool command(pw_frame_t *request, uint32_t nonce, uint32_t expected_offset,
                    uint8_t expected_phase, bool check_digest) {
    if (command_sequence == UINT32_MAX || esp_timer_get_time() >= transfer_deadline)
        return false;
    request->role = PW_ROLE_S3;
    request->session = transport_session;
    request->sequence = ++command_sequence;
    for (unsigned retry = 0; retry < 5; retry++) {
        if (esp_timer_get_time() >= transfer_deadline)
            return false;
        if (xQueueSend(outgoing, request, pdMS_TO_TICKS(500)) != pdTRUE)
            return false;
        int64_t until = esp_timer_get_time() + 2000000;
        while (esp_timer_get_time() < until) {
            pw_frame_t ack;
            if (xQueueReceive(acknowledgements, &ack, pdMS_TO_TICKS(100)) != pdTRUE)
                continue;
            const uint8_t *p = ack.payload;
            if (ack.role != PW_ROLE_COMPANION || ack.kind != PW_MSG_OTA_ACK ||
                ack.length != PW_OTA_ACK_BYTES || ack.session != nonce ||
                p[0] != PW_UPDATE_WIRE_VERSION || memcmp(p + 1, request->payload + 1, 16) ||
                p[17] != request->kind || le32(p + 20) != request->sequence ||
                le32(p + 28) != nonce)
                continue;
            if (p[18] != PW_OTA_OK || p[19] != expected_phase || le32(p + 24) != expected_offset)
                return false;
            if (check_digest && (memcmp(p + 32, status.job.manifest_sha256, 32) ||
                                 le32(p + 64) != status.job.image_bytes[0]))
                return false;
            return true;
        }
    }
    return false;
}
static bool transfer_companion(void) {
    transfer_deadline = esp_timer_get_time() + 900000000LL;
    size_t manifest_size, signature_size;
    const uint8_t *metadata = pw_update_stage_metadata(stage, &manifest_size, &signature_size);
    if (!metadata || !pw_update_stage_peer_begin(stage))
        return false;
    snapshot(NULL);
    xSemaphoreTake(mutex, portMAX_DELAY);
    uint32_t nonce = peer_nonce;
    bool recent =
        peer_seen && esp_timer_get_time() - peer_seen < 6000000 && status.peer_phase == PW_OTA_IDLE;
    pw_stage_view_t job = status.job;
    xSemaphoreGive(mutex);
    if (!recent || !nonce)
        return false;
    pw_frame_t request = {.kind = PW_MSG_OTA_BEGIN, .length = 20};
    request.payload[0] = PW_UPDATE_WIRE_VERSION;
    memcpy(request.payload + 1, job.transaction, 16);
    request.payload[17] = (uint8_t)manifest_size;
    request.payload[18] = (uint8_t)(manifest_size >> 8);
    request.payload[19] = (uint8_t)signature_size;
    if (!command(&request, nonce, 0, PW_OTA_META, false))
        return false;
    for (size_t offset = 0; offset < manifest_size + signature_size;) {
        size_t count = manifest_size + signature_size - offset;
        if (count > 173)
            count = 173;
        request.kind = PW_MSG_OTA_META_CHUNK;
        request.length = 19 + count;
        request.payload[17] = (uint8_t)offset;
        request.payload[18] = (uint8_t)(offset >> 8);
        memcpy(request.payload + 19, metadata + offset, count);
        if (!command(&request, nonce, offset + count, PW_OTA_META, false))
            return false;
        offset += count;
    }
    request.kind = PW_MSG_OTA_META_VERIFY;
    request.length = PW_OTA_BASE_BYTES;
    if (!command(&request, nonce, 0, PW_OTA_RECEIVING, true))
        return false;
    for (uint32_t offset = 0; offset < job.image_bytes[0];) {
        size_t count = job.image_bytes[0] - offset;
        if (count > 171)
            count = 171;
        request.kind = PW_MSG_OTA_IMAGE_CHUNK;
        request.length = 21 + count;
        put32(request.payload + 17, offset);
        if (!image_read(&flash, PW_UPDATE_COMPANION, offset, request.payload + 21, count) ||
            !command(&request, nonce, offset + count, PW_OTA_RECEIVING, true))
            return false;
        offset += count;
        pw_update_stage_peer_progress(stage, offset);
        snapshot(NULL);
    }
    request.kind = PW_MSG_OTA_IMAGE_VERIFY;
    request.length = PW_OTA_BASE_BYTES;
    if (!command(&request, nonce, job.image_bytes[0], PW_OTA_VERIFIED, true))
        return false;
    return pw_update_stage_peer_verified(stage);
}
static void abort_peer_staging(void) {
    pw_stage_view_t job;
    pw_update_stage_view(stage, &job);
    xSemaphoreTake(mutex, portMAX_DELAY);
    uint32_t nonce = peer_nonce;
    xSemaphoreGive(mutex);
    if (!nonce)
        return;
    pw_frame_t request = {.kind = PW_MSG_OTA_ABORT, .length = PW_OTA_BASE_BYTES};
    request.payload[0] = PW_UPDATE_WIRE_VERSION;
    memcpy(request.payload + 1, job.transaction, 16);
    transfer_deadline = esp_timer_get_time() + 3000000;
    /* Receiver permits this only on its old, non-pending slot. Failure is not
     * hidden: the local job remains failed and the peer may need recovery. */
    (void)command(&request, nonce, 0, PW_OTA_IDLE, false);
}
static void upload_worker(void *unused) {
    (void)unused;
    uint8_t transaction[16];
    esp_fill_random(transaction, sizeof transaction);
    uint8_t *buffer = malloc(PW_STAGE_CHUNK_BYTES);
    pw_weather_set_suspended(true);
    bool ok = buffer && pw_update_stage_begin(stage, upload_bytes, transaction);
    uint32_t received = 0;
    snapshot(NULL);
    int64_t deadline = esp_timer_get_time() + 1800000000LL;
    while (ok && received < upload_bytes) {
        size_t need = upload_bytes - received;
        if (need > PW_STAGE_CHUNK_BYTES)
            need = PW_STAGE_CHUNK_BYTES;
        int count = upload_read(upload_context, buffer, need);
        if (count <= 0 || (size_t)count > need || esp_timer_get_time() > deadline) {
            ok = false;
            break;
        }
        ok = pw_update_stage_feed(stage, buffer, (size_t)count);
        received += count;
        snapshot(NULL);
    }
    free(buffer);
    if (ok)
        ok = pw_update_stage_finish(stage);
    if (!ok) {
        pw_stage_view_t v;
        pw_update_stage_view(stage, &v);
        if (v.phase != PW_STAGE_FAILED && v.phase != PW_STAGE_LOCKED)
            pw_update_stage_abort(stage, PW_UPDATE_SIZE);
        snapshot("upload_rejected");
    }
    /* Free the HTTP request now; the UART job continues independently. */
    upload_reply(upload_context, ok, ok ? NULL : pw_update_result_name(status.job.error));
    upload_context = NULL;
    if (ok && !transfer_companion()) {
        abort_peer_staging();
        pw_update_stage_abort(stage, PW_UPDATE_STATE);
        snapshot("companion_staging_failed");
    } else if (ok)
        snapshot("activation_not_qualified");
    pw_weather_set_suspended(false);
    xSemaphoreTake(mutex, portMAX_DELAY);
    status.busy = false;
    xSemaphoreGive(mutex);
    vTaskDelete(NULL);
}
esp_err_t pw_update_service_upload(uint32_t bytes, bool usb_power_confirmed,
                                   pw_update_upload_read_t read, pw_update_upload_reply_t reply,
                                   void *context) {
    if (!mutex || !stage || !read || !reply || !usb_power_confirmed)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool eligible = status.upload_enabled && !status.busy && transport_session && peer_nonce &&
                    peer_seen && esp_timer_get_time() - peer_seen < 6000000 &&
                    status.peer_phase == PW_OTA_IDLE;
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY)
        eligible = false;
    if (!eligible) {
        xSemaphoreGive(mutex);
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t maximum = 14u + PW_UPDATE_MANIFEST_MAX + 72u +
                       (uint64_t)local_config.policy.roles[0].app_slot_bytes +
                       local_config.policy.roles[1].app_slot_bytes;
    if (bytes < 598 || bytes > maximum) {
        xSemaphoreGive(mutex);
        return ESP_ERR_INVALID_SIZE;
    }
    status.busy = true;
    upload_bytes = bytes;
    upload_read = read;
    upload_reply = reply;
    upload_context = context;
    xQueueReset(outgoing);
    xQueueReset(acknowledgements);
    if (xTaskCreate(upload_worker, "pw_update", 12288, NULL, 3, NULL) != pdPASS) {
        status.busy = false;
        upload_context = NULL;
        xSemaphoreGive(mutex);
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(mutex);
    return ESP_OK;
}

// SPDX-License-Identifier: MIT
#include "pw_spotify_model.h"
#include "mbedtls/base64.h"
#include "mbedtls/sha256.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void pw_spotify_wipe(void *p, size_t n) {
    volatile unsigned char *v = p;
    while (n--)
        *v++ = 0;
}
static const cJSON *field(const cJSON *j, const char *k) {
    return cJSON_GetObjectItemCaseSensitive(j, k);
}
static const char *str(const cJSON *j) {
    return cJSON_IsString(j) ? j->valuestring : NULL;
}
static bool number(const cJSON *j, double min, double max) {
    return cJSON_IsNumber(j) && isfinite(j->valuedouble) && j->valuedouble >= min &&
           j->valuedouble <= max && floor(j->valuedouble) == j->valuedouble;
}
bool pw_spotify_ascii(const char *s, size_t cap, bool empty) {
    if (!s)
        return false;
    size_t n = strnlen(s, cap);
    if (n == cap || (!n && !empty))
        return false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)s[i] < 33 || (unsigned char)s[i] > 126)
            return false;
    return true;
}
bool pw_spotify_uri(const char *s) {
    if (!s)
        return false;
    size_t p = !strncmp(s, "spotify:playlist:", 17) ? 17
               : !strncmp(s, "spotify:track:", 14)  ? 14
                                                    : 0;
    if (!p || strlen(s) != p + 22)
        return false;
    for (size_t i = p; i < p + 22; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= '0' && s[i] <= '9')))
            return false;
    return true;
}
bool pw_spotify_encode(const char *s, char *out, size_t cap) {
    static const char h[] = "0123456789ABCDEF";
    size_t p = 0;
    if (!s || !out || !cap)
        return false;
    for (; *s; s++) {
        unsigned char c = *s;
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '-' || c == '_' || c == '.' || c == '~';
        if (p + (plain ? 1 : 3) >= cap) {
            out[0] = 0;
            return false;
        }
        if (plain)
            out[p++] = c;
        else {
            out[p++] = '%';
            out[p++] = h[c >> 4];
            out[p++] = h[c & 15];
        }
    }
    out[p] = 0;
    return true;
}
/* Validate UTF-8 before truncation; never emit a partial codepoint/control character. */
bool pw_spotify_text(char *out, size_t cap, const char *s) {
    if (!out || !cap || !s)
        return false;
    size_t p = 0;
    bool full = false;
    const unsigned char *a = (const unsigned char *)s;
    while (*a) {
        size_t n;
        uint32_t cp;
        if (*a < 0x80) {
            n = 1;
            cp = *a;
        } else if (*a >= 0xC2 && *a <= 0xDF) {
            n = 2;
            cp = *a & 31;
        } else if (*a >= 0xE0 && *a <= 0xEF) {
            n = 3;
            cp = *a & 15;
        } else if (*a >= 0xF0 && *a <= 0xF4) {
            n = 4;
            cp = *a & 7;
        } else
            return false;
        for (size_t i = 1; i < n; i++) {
            if (!a[i] || (a[i] & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (a[i] & 63);
        }
        if ((n == 3 && cp < 0x800) || (n == 4 && cp < 0x10000) || (cp >= 0xD800 && cp <= 0xDFFF) ||
            cp > 0x10FFFF)
            return false;
        if (cp < 32 || (cp >= 127 && cp <= 159)) {
            if (!full && p + 1 < cap)
                out[p++] = ' ';
            else
                full = true;
        } else if (!full && p + n < cap) {
            memcpy(out + p, a, n);
            p += n;
        } else
            full = true;
        a += n;
    }
    out[p] = 0;
    return true;
}
static bool unique(const cJSON *j) {
    if (cJSON_IsObject(j))
        for (const cJSON *a = j->child; a; a = a->next) {
            if (!a->string)
                return false;
            for (const cJSON *b = a->next; b; b = b->next)
                if (!b->string || !strcmp(a->string, b->string))
                    return false;
        }
    for (const cJSON *a = j->child; a; a = a->next)
        if (!unique(a))
            return false;
    return true;
}
cJSON *pw_spotify_json(const char *s, size_t n) {
    if (!s || !n || n > 65536 || memchr(s, 0, n))
        return NULL;
    bool quoted = false;
    unsigned depth = 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (quoted && c == '\\') {
            if (++i >= n)
                return NULL;
            if (s[i] == 'u' && i + 4 < n && !memcmp(s + i + 1, "0000", 4))
                return NULL;
            continue;
        }
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
        if (quoted)
            continue;
        if (c == '{' || c == '[') {
            if (++depth > 24)
                return NULL;
        } else if (c == '}' || c == ']') {
            if (!depth--)
                return NULL;
        }
    }
    if (quoted || depth)
        return NULL;
    const char *end = NULL;
    cJSON *j = cJSON_ParseWithLengthOpts(s, n, &end, false);
    if (!j)
        return NULL;
    while (end < s + n && (*end == ' ' || *end == '\n' || *end == '\r' || *end == '\t'))
        end++;
    if (end != s + n || !unique(j)) {
        pw_spotify_secret_json_delete(j);
        return NULL;
    }
    return j;
}
static void secret_tree_wipe(cJSON *j) {
    for (cJSON *a = j; a; a = a->next) {
        if (a->child) secret_tree_wipe(a->child);
        if (a->valuestring) pw_spotify_wipe(a->valuestring, strlen(a->valuestring));
    }
}
void pw_spotify_secret_json_delete(cJSON *j) {
    secret_tree_wipe(j);
    cJSON_Delete(j);
}
/* Called only after pw_spotify_text has validated the entire source and bounded
 * the result. Keep visible names intact, including their spacing/formatting. */
static bool device_name_visible(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    while (*p) {
        uint32_t cp = *p++;
        if (cp >= 0xC0) {
            unsigned remaining = cp < 0xE0 ? 1 : cp < 0xF0 ? 2 : 3;
            cp &= remaining == 1 ? 31 : remaining == 2 ? 15 : 7;
            while (remaining--)
                cp = (cp << 6) | (*p++ & 63);
        }
        /* Unicode spaces and common zero-width/directional/variation markers
         * alone cannot provide a useful output label. */
        if (!(cp <= 0x20 || cp == 0xA0 || cp == 0xAD || cp == 0x034F || cp == 0x061C ||
              cp == 0x115F || cp == 0x1160 || cp == 0x1680 ||
              (cp >= 0x180B && cp <= 0x180F) || (cp >= 0x2000 && cp <= 0x200F) ||
              (cp >= 0x2028 && cp <= 0x202F) || (cp >= 0x205F && cp <= 0x206F) ||
              cp == 0x3000 || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF ||
              cp == 0xE0001 || (cp >= 0xE0020 && cp <= 0xE007F) ||
              (cp >= 0xE0100 && cp <= 0xE01EF)))
            return true;
    }
    return false;
}
static bool playback_fail(pw_spotify_playback_parse_error_t *reason,
                          pw_spotify_playback_parse_error_t value) {
    if (reason)
        *reason = value;
    return false;
}
static bool device(const cJSON *j, pw_spotify_device_t *d, bool nullable_id,
                   pw_spotify_playback_parse_error_t *reason) {
    if (!cJSON_IsObject(j))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_OBJECT);
    memset(d, 0, sizeof(*d));
    const cJSON *id_value = field(j, "id");
    /* A playback device can be known but not addressable. Only explicit JSON
     * null is accepted here; absent, empty and invalid IDs remain errors. */
    if (!(nullable_id && cJSON_IsNull(id_value))) {
        const char *id = str(id_value);
        if (!id || !pw_spotify_ascii(id, sizeof(d->id), false))
            return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_ID);
        memcpy(d->id, id, strlen(id) + 1);
    }
    if (!pw_spotify_text(d->name, sizeof(d->name), str(field(j, "name"))))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_NAME);
    if (!pw_spotify_text(d->type, sizeof(d->type), str(field(j, "type"))))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_TYPE);
    if (!device_name_visible(d->name))
        strcpy(d->name, "Ausgabe ohne Namen");
    const cJSON *active = field(j, "is_active"), *restricted = field(j, "is_restricted"),
                *support = field(j, "supports_volume");
    if (!cJSON_IsBool(active) || !cJSON_IsBool(restricted))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_FLAGS);
    d->active = cJSON_IsTrue(active);
    d->restricted = cJSON_IsTrue(restricted);
    d->supports_volume = cJSON_IsTrue(support);
    const cJSON *volume = field(j, "volume_percent");
    if (volume && !cJSON_IsNull(volume) && !number(volume, 0, 100))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_DEVICE_VOLUME);
    d->volume_known = number(volume, 0, 100);
    if (d->volume_known)
        d->volume = volume->valueint;
    return true;
}
bool pw_spotify_parse_devices(const cJSON *j, pw_spotify_snapshot_t *s) {
    const cJSON *list = field(j, "devices");
    if (!cJSON_IsObject(j) || !cJSON_IsArray(list))
        return false;
    s->device_count = 0;
    s->devices_truncated = false;
    memset(s->devices, 0, sizeof(s->devices));
    for (const cJSON *a = list->child; a; a = a->next) {
        /* Null IDs identify devices that cannot be addressed, so omit them. */
        if (cJSON_IsObject(a) && cJSON_IsNull(field(a, "id")))
            continue;
        pw_spotify_device_t d;
        if (!device(a, &d, false, NULL))
            return false;
        for (unsigned i = 0; i < s->device_count; i++)
            if (!strcmp(s->devices[i].id, d.id))
                return false;
        if (s->device_count == PW_SPOTIFY_MAX_DEVICES) {
            s->devices_truncated = true;
            continue;
        }
        s->devices[s->device_count++] = d;
    }
    return true;
}
void pw_spotify_clear_playback(pw_spotify_snapshot_t *s) {
    s->playback_known = false;
    s->playing = false;
    s->volume_known = false;
    s->supports_volume = false;
    s->position_known = false;
    s->position_ms = s->duration_ms = 0;
    s->volume = 0;
    s->observed_at_ms = 0;
    s->title[0] = s->artist[0] = s->item_type[0] = s->item_uri[0] = s->context_uri[0] =
        s->active_device_id[0] = s->active_device_name[0] = 0;
}
bool pw_spotify_parse_playback_ex(const cJSON *j, pw_spotify_snapshot_t *s,
                                  pw_spotify_disallows_t *dis,
                                  pw_spotify_playback_parse_error_t *reason) {
    if (reason)
        *reason = PW_SPOTIFY_PLAYBACK_PARSE_OK;
    if (!cJSON_IsObject(j))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_ROOT);
    if (!cJSON_IsBool(field(j, "is_playing")))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_IS_PLAYING);
    pw_spotify_device_t d;
    if (!device(field(j, "device"), &d, true, reason))
        return false;
    pw_spotify_clear_playback(s);
    memset(dis, 0, sizeof(*dis));
    s->playback_known = true;
    s->playing = cJSON_IsTrue(field(j, "is_playing"));
    s->supports_volume = d.supports_volume;
    s->volume_known = d.volume_known;
    s->volume = d.volume;
    strcpy(s->active_device_id, d.id);
    strcpy(s->active_device_name, d.name);
    const cJSON *pos = field(j, "progress_ms");
    if (pos && !cJSON_IsNull(pos) && !number(pos, 0, UINT32_MAX))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_PROGRESS);
    s->position_known = number(pos, 0, UINT32_MAX);
    if (s->position_known)
        s->position_ms = (uint32_t)pos->valuedouble;
    const cJSON *item = field(j, "item");
    const char *type = str(field(j, "currently_playing_type"));
    if (type && !pw_spotify_text(s->item_type, sizeof(s->item_type), type))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_ITEM_TYPE);
    if (cJSON_IsObject(item)) {
        const char *name = str(field(item, "name")), *uri = str(field(item, "uri"));
        if (name && !pw_spotify_text(s->title, sizeof(s->title), name))
            return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_TITLE);
        if (uri && (!pw_spotify_ascii(uri, sizeof(s->item_uri), false)))
            return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_ITEM_URI);
        if (uri)
            strcpy(s->item_uri, uri);
        const cJSON *duration = field(item, "duration_ms");
        if (duration && !number(duration, 0, UINT32_MAX))
            return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_DURATION);
        if (duration)
            s->duration_ms = (uint32_t)duration->valuedouble;
        const cJSON *artists = field(item, "artists");
        const char *artist = NULL;
        if (cJSON_IsArray(artists) && artists->child)
            artist = str(field(artists->child, "name"));
        if (!artist)
            artist = str(field(field(item, "show"), "name"));
        if (artist && !pw_spotify_text(s->artist, sizeof(s->artist), artist))
            return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_ARTIST);
    } else if (item && !cJSON_IsNull(item))
        return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_ITEM);
    const char *context = str(field(field(j, "context"), "uri"));
    if (context) {
        if (!pw_spotify_ascii(context, sizeof(s->context_uri), false))
            return playback_fail(reason, PW_SPOTIFY_PLAYBACK_PARSE_CONTEXT);
        strcpy(s->context_uri, context);
    }
    const cJSON *actions = field(j, "actions"), *a = field(actions, "disallows");
    /* Spotify responses use actions.disallows; the flattened documented shape
     * is also interpreted as disallow flags, never as evidence of playback. */
    if (!a)
        a = actions;
    dis->pause = cJSON_IsTrue(field(a, "pausing"));
    dis->resume = cJSON_IsTrue(field(a, "resuming"));
    dis->next = cJSON_IsTrue(field(a, "skipping_next"));
    dis->previous = cJSON_IsTrue(field(a, "skipping_prev"));
    if (d.restricted)
        dis->pause = dis->resume = dis->next = dis->previous = true;
    return true;
}
bool pw_spotify_parse_playback(const cJSON *j, pw_spotify_snapshot_t *s,
                               pw_spotify_disallows_t *dis) {
    return pw_spotify_parse_playback_ex(j, s, dis, NULL);
}
void pw_spotify_capabilities(pw_spotify_snapshot_t *s, const pw_spotify_disallows_t *d,
                             bool fresh) {
    s->selected_present = s->selected_restricted = s->selected_supports_volume =
        s->selected_volume_known = false;
    s->selected_volume = 0;
    for (unsigned i = 0; i < s->device_count; i++)
        if (s->selected_device_id[0] && !strcmp(s->devices[i].id, s->selected_device_id)) {
            const pw_spotify_device_t *p = &s->devices[i];
            s->selected_present = true;
            s->selected_restricted = p->restricted;
            s->selected_supports_volume = p->supports_volume;
            s->selected_volume_known = p->volume_known;
            s->selected_volume = p->volume;
            strcpy(s->selected_device_name, p->name);
            break;
        }
    bool active = s->playback_known && s->active_device_id[0] &&
                  !strcmp(s->active_device_id, s->selected_device_id);
    if (active) {
        s->selected_volume_known = s->volume_known;
        s->selected_volume = s->volume;
        s->selected_supports_volume = s->supports_volume;
    }
    bool available = s->enabled && s->linked && s->connected && s->state == PW_SPOTIFY_READY &&
                     s->selected_present && !s->selected_restricted && fresh;
    s->can_play = available && (!active || !d->resume);
    s->can_pause = available && active && s->playing && !d->pause;
    s->can_play_pause = s->can_play || s->can_pause;
    s->can_next = available && active && !d->next;
    s->can_previous = available && active && !d->previous;
    s->can_volume = available && s->selected_supports_volume;
}
bool pw_spotify_command_valid(const pw_spotify_command_t *c, const pw_spotify_snapshot_t *s) {
    if (!c || !s || c->kind > PW_SPOTIFY_VOLUME || c->kind < PW_SPOTIFY_PLAY ||
        !pw_spotify_ascii(c->device_id, sizeof(c->device_id), false) ||
        strnlen(c->uri, sizeof(c->uri)) == sizeof(c->uri))
        return false;
    if (c->session != s->session || c->selection_generation != s->selection_generation ||
        strcmp(c->device_id, s->selected_device_id))
        return false;
    if (c->kind != PW_SPOTIFY_PLAY && c->uri[0])
        return false;
    switch (c->kind) {
    case PW_SPOTIFY_PLAY:
        return s->can_play && (!c->uri[0] || pw_spotify_uri(c->uri));
    case PW_SPOTIFY_PAUSE:
        return s->can_pause;
    case PW_SPOTIFY_NEXT:
        return s->can_next;
    case PW_SPOTIFY_PREVIOUS:
        return s->can_previous;
    case PW_SPOTIFY_VOLUME:
        return s->can_volume && c->volume <= 100;
    default:
        return false;
    }
}
static bool scopes(const char *s) {
    if (!s || strlen(s) > 512)
        return false;
    bool read = false, write = false;
    while (*s) {
        while (*s == ' ')
            s++;
        const char *start = s;
        while (*s && *s != ' ')
            s++;
        size_t n = s - start;
        if (n == 24 && !memcmp(start, "user-read-playback-state", 24))
            read = true;
        if (n == 26 && !memcmp(start, "user-modify-playback-state", 26))
            write = true;
    }
    return read && write;
}
bool pw_spotify_parse_token(const cJSON *j, const pw_spotify_tokens_t *old, int64_t now,
                            pw_spotify_tokens_t *out) {
    if (!cJSON_IsObject(j) || now < 1704067200 || now > 4102444800LL)
        return false;
    const char *access = str(field(j, "access_token")), *refresh = str(field(j, "refresh_token")),
               *type = str(field(j, "token_type")), *scope = str(field(j, "scope"));
    const cJSON *expires = field(j, "expires_in");
    if (!pw_spotify_ascii(access, PW_SPOTIFY_TOKEN_BYTES, false) || !type ||
        strcmp(type, "Bearer") || !number(expires, 1, 86400))
        return false;
    /* RFC 6749 allows an omitted scope when unchanged from the requested grant. */
    if (scope && !scopes(scope))
        return false;
    if (field(j, "scope") && !scope)
        return false;
    if (refresh && !pw_spotify_ascii(refresh, PW_SPOTIFY_TOKEN_BYTES, false))
        return false;
    if (field(j, "refresh_token") && !refresh)
        return false;
    if (!refresh && (!old || !pw_spotify_ascii(old->refresh, PW_SPOTIFY_TOKEN_BYTES, false)))
        return false;
    memset(out, 0, sizeof(*out));
    strcpy(out->access, access);
    strcpy(out->refresh, refresh ? refresh : old->refresh);
    out->authorized_at = old ? old->authorized_at : now;
    out->expires_at = now + (int64_t)expires->valuedouble;
    return true;
}
static void put64(uint8_t *p, uint64_t n) {
    for (unsigned i = 0; i < 8; i++)
        p[i] = n >> (8 * i);
}
static uint64_t get64(const uint8_t *p) {
    uint64_t n = 0;
    for (unsigned i = 0; i < 8; i++)
        n |= (uint64_t)p[i] << (8 * i);
    return n;
}
size_t pw_spotify_record_encode(const pw_spotify_tokens_t *t, uint8_t *out, size_t cap) {
    const size_t r = strlen(PW_SPOTIFY_REDIRECT_URI);
    if (!t || !pw_spotify_ascii(t->refresh, sizeof(t->refresh), false) ||
        t->authorized_at < 1704067200 || t->authorized_at > 4102444800LL)
        return 0;
    size_t n = strlen(t->refresh), len = 51 + r + n;
    if (cap < len)
        return 0;
    memset(out, 0, len);
    memcpy(out, "PWSPOT01", 8);
    put64(out + 8, t->authorized_at);
    memcpy(out + 16, PW_SPOTIFY_CLIENT_ID, 32);
    out[48] = r;
    out[49] = n & 255;
    out[50] = n >> 8;
    memcpy(out + 51, PW_SPOTIFY_REDIRECT_URI, r);
    memcpy(out + 51 + r, t->refresh, n);
    return len;
}
bool pw_spotify_record_decode(const uint8_t *in, size_t len, pw_spotify_tokens_t *out) {
    size_t r = strlen(PW_SPOTIFY_REDIRECT_URI);
    if (!in || !out || len < 51 + r || memcmp(in, "PWSPOT01", 8) ||
        memcmp(in + 16, PW_SPOTIFY_CLIENT_ID, 32) || in[48] != r ||
        memcmp(in + 51, PW_SPOTIFY_REDIRECT_URI, r))
        return false;
    size_t n = (size_t)in[49] | ((size_t)in[50] << 8);
    uint64_t when = get64(in + 8);
    if (n == 0 || n >= PW_SPOTIFY_TOKEN_BYTES || len != 51 + r + n || when < 1704067200 ||
        when > 4102444800LL || memchr(in + 51 + r, 0, n))
        return false;
    for (size_t i = 0; i < n; i++)
        if (in[51 + r + i] < 33 || in[51 + r + i] > 126)
            return false;
    memset(out, 0, sizeof(*out));
    memcpy(out->refresh, in + 51 + r, n);
    out->authorized_at = (int64_t)when;
    return true;
}
static bool b64url(const uint8_t *in, size_t n, char *out, size_t cap) {
    size_t len = 0;
    if (mbedtls_base64_encode((unsigned char *)out, cap, &len, in, n))
        return false;
    while (len && out[len - 1] == '=')
        len--;
    out[len] = 0;
    for (size_t i = 0; i < len; i++) {
        if (out[i] == '+')
            out[i] = '-';
        else if (out[i] == '/')
            out[i] = '_';
    }
    return true;
}
void pw_spotify_auth_cancel(pw_spotify_auth_t *a) {
    uint32_t g = a->generation + 1;
    if (!g)
        g = 1;
    pw_spotify_wipe(a, sizeof(*a));
    a->generation = g;
}
bool pw_spotify_auth_begin(pw_spotify_auth_t *a, const uint8_t random[56], int64_t now,
                           pw_spotify_auth_start_t *out) {
    if (!a || !random || !out || a->exchanging || now < 0)
        return false;
    pw_spotify_auth_cancel(a);
    memset(out, 0, sizeof(*out));
    static const char h[] = "0123456789abcdef";
    for (unsigned i = 0; i < 24; i++) {
        a->state[2 * i] = h[random[i] >> 4];
        a->state[2 * i + 1] = h[random[i] & 15];
    }
    char challenge[45];
    uint8_t digest[32];
    if (!b64url(random + 24, 32, a->verifier, sizeof(a->verifier)) ||
        mbedtls_sha256((const unsigned char *)a->verifier, strlen(a->verifier), digest, 0) ||
        !b64url(digest, sizeof(digest), challenge, sizeof(challenge))) {
        pw_spotify_auth_cancel(a);
        return false;
    }
    char redirect[192];
    pw_spotify_encode(PW_SPOTIFY_REDIRECT_URI, redirect, sizeof(redirect));
    int n = snprintf(
        out->url, sizeof(out->url),
        "https://accounts.spotify.com/"
        "authorize?client_id=%s&response_type=code&redirect_uri=%s&scope=user-read-playback-state%%"
        "20user-modify-playback-state&code_challenge_method=S256&code_challenge=%s&state=%s",
        PW_SPOTIFY_CLIENT_ID, redirect, challenge, a->state);
    pw_spotify_wipe(digest, sizeof(digest));
    if (n < 0 || (size_t)n >= sizeof(out->url)) {
        pw_spotify_auth_cancel(a);
        return false;
    }
    a->pending = true;
    a->expires_ms = now + PW_SPOTIFY_AUTH_TTL_SECONDS * 1000LL;
    out->expires_in_seconds = PW_SPOTIFY_AUTH_TTL_SECONDS;
    return true;
}
bool pw_spotify_auth_accept(pw_spotify_auth_t *a, const char *code, const char *state,
                            int64_t now) {
    if (!a || !a->pending || a->exchanging || now >= a->expires_ms || now < 0 ||
        !pw_spotify_ascii(code, PW_SPOTIFY_AUTH_CODE_BYTES, false) || !state ||
        strnlen(state, PW_SPOTIFY_AUTH_STATE_BYTES) != 48)
        return false;
    unsigned diff = 0;
    for (unsigned i = 0; i < 48; i++)
        diff |= (unsigned char)state[i] ^ (unsigned char)a->state[i];
    if (diff)
        return false;
    a->pending = false;
    a->exchanging = true;
    pw_spotify_wipe(a->state, sizeof(a->state));
    return true;
}
uint32_t pw_spotify_retry_after(const char *s) {
    if (!s || !*s)
        return 30;
    uint64_t n = 0;
    for (unsigned i = 0; s[i]; i++) {
        if (i >= 10 || s[i] < '0' || s[i] > '9')
            return 30;
        n = n * 10 + (unsigned)(s[i] - '0');
        if (n > UINT32_MAX)
            return UINT32_MAX;
    }
    return n ? (uint32_t)n : 1;
}

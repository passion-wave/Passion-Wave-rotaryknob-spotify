// SPDX-License-Identifier: MIT
#include "pw_validation.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

static const char *const settings_keys[] = {
    "name",      "brightness", "haptic",         "weather_enabled", "latitude",
    "longitude", "timezone",   "avatar_enabled", "avatar_blond",    "screensaver_mode"};
static const char *const config_keys[] = {"schema", "revision", "settings", "catalog"};
static const char *const catalog_keys[] = {"favorites", "stations"};
static const char *const favorite_keys[] = {"id", "kind", "name", "uri", "enabled"};
static const char *const station_keys[] = {"id", "name", "url", "enabled"};

static const cJSON *field(const cJSON *object, const char *key) {
    return cJSON_GetObjectItemCaseSensitive(object, key);
}
static bool ascii_alnum(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}
static unsigned char lower_ascii(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}
static int hex(unsigned char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    c = lower_ascii(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

/* Count-free cJSON strings require a NUL-free JSON input at the parser boundary.
 * Reject invalid UTF-8, overlong encodings, surrogates and control characters. */
static bool plain_text(const char *value, size_t min, size_t max) {
    if (!value)
        return false;
    size_t length = 0;
    while (length <= max && value[length])
        ++length;
    if (length < min || length > max)
        return false;
    bool visible = false;
    for (size_t i = 0; i < length;) {
        unsigned char lead = (unsigned char)value[i++];
        uint32_t cp;
        size_t trailing;
        if (lead < 0x80) {
            cp = lead;
            trailing = 0;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            cp = lead & 0x1f;
            trailing = 1;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            cp = lead & 0x0f;
            trailing = 2;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            cp = lead & 7;
            trailing = 3;
        } else
            return false;
        if (trailing > length - i)
            return false;
        for (size_t j = 0; j < trailing; ++j) {
            unsigned char next = (unsigned char)value[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            cp = (cp << 6) | (next & 0x3f);
        }
        if ((trailing == 1 && cp < 0x80) || (trailing == 2 && cp < 0x800) ||
            (trailing == 3 && cp < 0x10000) || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
            cp < 0x20 || (cp >= 0x7f && cp <= 0x9f))
            return false;
        if (cp != ' ' && cp != 0xa0)
            visible = true;
    }
    return visible;
}
static bool text_field(const cJSON *object, const char *key, size_t min, size_t max) {
    const cJSON *value = field(object, key);
    return cJSON_IsString(value) && plain_text(value->valuestring, min, max);
}
static bool integer(const cJSON *value, double min, double max) {
    return cJSON_IsNumber(value) && isfinite(value->valuedouble) && value->valuedouble >= min &&
           value->valuedouble <= max && floor(value->valuedouble) == value->valuedouble;
}
bool pw_keys_only(const cJSON *object, const char *const *names, size_t count) {
    if (!cJSON_IsObject(object) || (!names && count))
        return false;
    size_t actual = 0;
    for (const cJSON *value = object->child; value; value = value->next) {
        if (++actual > count || !value->string)
            return false;
        bool known = false;
        for (size_t i = 0; i < count; ++i)
            if (names[i] && !strcmp(value->string, names[i]))
                known = true;
        if (!known)
            return false;
        for (const cJSON *other = value->next; other; other = other->next)
            if (!other->string || !strcmp(value->string, other->string))
                return false;
    }
    return true;
}
static bool required_keys(const cJSON *object, const char *const *keys, size_t count) {
    if (!pw_keys_only(object, keys, count))
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!field(object, keys[i]))
            return false;
    return true;
}
static bool coordinate(const cJSON *value, double max) {
    return cJSON_IsNull(value) || (cJSON_IsNumber(value) && isfinite(value->valuedouble) &&
                                   fabs(value->valuedouble) <= max);
}
bool pw_validate_settings_patch(const cJSON *settings) {
    if (!pw_keys_only(settings, settings_keys, sizeof settings_keys / sizeof settings_keys[0]) ||
        !settings->child)
        return false;
    for (const cJSON *value = settings->child; value; value = value->next) {
        const char *key = value->string;
        if (!strcmp(key, "name")) {
            if (!cJSON_IsString(value) || !plain_text(value->valuestring, 1, 48))
                return false;
        } else if (!strcmp(key, "brightness")) {
            if (!integer(value, 1, 100))
                return false;
        } else if (!strcmp(key, "haptic") || !strcmp(key, "weather_enabled") ||
                   !strcmp(key, "avatar_enabled") || !strcmp(key, "avatar_blond")) {
            if (!cJSON_IsBool(value))
                return false;
        } else if (!strcmp(key, "screensaver_mode")) {
            if (!cJSON_IsString(value) ||
                (strcmp(value->valuestring, "weather_photo") && strcmp(value->valuestring, "off")))
                return false;
        } else if (!strcmp(key, "latitude")) {
            if (!coordinate(value, 90))
                return false;
        } else if (!strcmp(key, "longitude")) {
            if (!coordinate(value, 180))
                return false;
        } else if (!strcmp(key, "timezone")) {
            if (!cJSON_IsString(value) || strcmp(value->valuestring, "Europe/Berlin"))
                return false;
        }
    }
    const cJSON *lat = field(settings, "latitude"), *lon = field(settings, "longitude");
    if (lat && lon && cJSON_IsNull(lat) != cJSON_IsNull(lon))
        return false;
    if (cJSON_IsTrue(field(settings, "weather_enabled")) &&
        ((lat && cJSON_IsNull(lat)) || (lon && cJSON_IsNull(lon))))
        return false;
    return true;
}
static bool complete_settings(const cJSON *settings) {
    if (!required_keys(settings, settings_keys, sizeof settings_keys / sizeof settings_keys[0]) ||
        !pw_validate_settings_patch(settings))
        return false;
    const cJSON *lat = field(settings, "latitude"), *lon = field(settings, "longitude");
    if (cJSON_IsNull(lat) != cJSON_IsNull(lon))
        return false;
    return !cJSON_IsTrue(field(settings, "weather_enabled")) ||
           (cJSON_IsNumber(lat) && cJSON_IsNumber(lon));
}

static bool suffix(const char *host, const char *ending) {
    size_t host_n = strlen(host), end_n = strlen(ending);
    if (host_n < end_n)
        return false;
    for (size_t i = 0; i < end_n; ++i)
        if (lower_ascii((unsigned char)host[host_n - end_n + i]) != (unsigned char)ending[i])
            return false;
    return true;
}
static bool public_ipv4(const char *host) {
    unsigned octets[4] = {0};
    const char *at = host;
    for (unsigned i = 0; i < 4; ++i) {
        const char *start = at;
        unsigned digits = 0;
        while (*at >= '0' && *at <= '9') {
            if (++digits > 3)
                return false;
            octets[i] = octets[i] * 10 + (unsigned)(*at++ - '0');
        }
        if (!digits || octets[i] > 255 || (digits > 1 && *start == '0'))
            return false;
        if (i < 3) {
            if (*at++ != '.')
                return false;
        } else if (*at)
            return false;
    }
    unsigned a = octets[0], b = octets[1], c = octets[2];
    if (a == 0 || a == 10 || a == 127 || a >= 224 || (a == 100 && b >= 64 && b <= 127) ||
        (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
        (a == 192 && b == 0 && (c == 0 || c == 2)) || (a == 192 && b == 88 && c == 99) ||
        (a == 198 && (b == 18 || b == 19 || (b == 51 && c == 100))) ||
        (a == 203 && b == 0 && c == 113))
        return false;
    return true;
}
static bool public_host(const char *host) {
    size_t length = strlen(host);
    if (!length || length > 253 || host[length - 1] == '.')
        return false;
    bool numeric = true, has_dot = false;
    for (size_t i = 0; i < length; ++i) {
        if (host[i] == '.')
            has_dot = true;
        else if (host[i] < '0' || host[i] > '9')
            numeric = false;
    }
    if (numeric)
        return public_ipv4(host);
    if (!has_dot)
        return false;
    size_t label = 0;
    bool final_alpha = false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)host[i];
        if (c == '.') {
            if (!label || host[i - 1] == '-')
                return false;
            label = 0;
            final_alpha = false;
        } else {
            if (!ascii_alnum(c) && c != '-')
                return false;
            if ((!label && c == '-') || ++label > 63)
                return false;
            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
                final_alpha = true;
        }
    }
    if (!label || host[length - 1] == '-' || !final_alpha)
        return false;
    const char *const reserved[] = {".local", ".localhost", ".internal", ".home", ".lan",
                                    ".test",  ".invalid",   ".example",  ".onion"};
    for (size_t i = 0; i < sizeof reserved / sizeof reserved[0]; ++i)
        if (suffix(host, reserved[i]))
            return false;
    return true;
}
bool pw_public_radio_url(const char *url) {
    if (!url)
        return false;
    size_t length = 0;
    while (length <= 1024 && url[length])
        ++length;
    if (length < 10 || length > 1024)
        return false;
    const char *authority = !strncmp(url, "https://", 8)  ? url + 8
                            : !strncmp(url, "http://", 7) ? url + 7
                                                          : NULL;
    if (!authority)
        return false;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)url[i];
        if (c <= 0x20 || c >= 0x7f || c == '\\' || c == '#')
            return false;
        if (c == '%') {
            if (i + 2 >= length || hex((unsigned char)url[i + 1]) < 0 ||
                hex((unsigned char)url[i + 2]) < 0)
                return false;
            int decoded = hex((unsigned char)url[i + 1]) * 16 + hex((unsigned char)url[i + 2]);
            if (decoded == 0 || decoded == 10 || decoded == 13)
                return false;
            i += 2;
        }
    }
    size_t host_length = strcspn(authority, ":/?");
    if (!host_length || host_length > 253)
        return false;
    char host[254];
    memcpy(host, authority, host_length);
    host[host_length] = 0;
    if (!public_host(host))
        return false;
    const char *after = authority + host_length;
    if (*after == ':') {
        ++after;
        unsigned port = 0, digits = 0;
        while (*after >= '0' && *after <= '9') {
            if (++digits > 5)
                return false;
            port = port * 10 + (unsigned)(*after++ - '0');
        }
        if (!digits || !port || port > 65535)
            return false;
    }
    return !*after || *after == '/' || *after == '?';
}
static bool identifier(const cJSON *object) {
    const cJSON *id = field(object, "id");
    if (!cJSON_IsString(id) || !plain_text(id->valuestring, 1, 64))
        return false;
    for (const unsigned char *c = (const unsigned char *)id->valuestring; *c; ++c)
        if (!ascii_alnum(*c) && *c != '-' && *c != '_')
            return false;
    return true;
}
static bool favorite(const cJSON *entry) {
    if (!required_keys(entry, favorite_keys, 5) || !identifier(entry) ||
        !text_field(entry, "name", 1, 80) || !cJSON_IsBool(field(entry, "enabled")))
        return false;
    const cJSON *kind = field(entry, "kind"), *uri = field(entry, "uri");
    if (!cJSON_IsString(kind) || !cJSON_IsString(uri))
        return false;
    const char *prefix = !strcmp(kind->valuestring, "spotify_playlist")  ? "spotify:playlist:"
                         : !strcmp(kind->valuestring, "spotify_show")    ? "spotify:show:"
                         : !strcmp(kind->valuestring, "spotify_episode") ? "spotify:episode:"
                                                                         : NULL;
    if (!prefix || strncmp(uri->valuestring, prefix, strlen(prefix)))
        return false;
    const char *id = uri->valuestring + strlen(prefix);
    if (strlen(id) != 22)
        return false;
    for (size_t i = 0; i < 22; ++i)
        if (!ascii_alnum((unsigned char)id[i]))
            return false;
    return true;
}
static bool station(const cJSON *entry) {
    if (!required_keys(entry, station_keys, 4) || !identifier(entry) ||
        !text_field(entry, "name", 1, 80) || !cJSON_IsBool(field(entry, "enabled")))
        return false;
    const cJSON *url = field(entry, "url");
    return cJSON_IsString(url) && pw_public_radio_url(url->valuestring);
}
bool pw_validate_catalog(const cJSON *catalog) {
    if (!required_keys(catalog, catalog_keys, 2))
        return false;
    const cJSON *favorites = field(catalog, "favorites"), *stations = field(catalog, "stations");
    if (!cJSON_IsArray(favorites) || !cJSON_IsArray(stations))
        return false;
    const cJSON *groups[] = {favorites, stations};
    for (unsigned group = 0; group < 2; ++group) {
        size_t count = 0;
        for (const cJSON *entry = groups[group]->child; entry; entry = entry->next) {
            if (++count > 64 || !(group ? station(entry) : favorite(entry)))
                return false;
            const char *id = field(entry, "id")->valuestring;
            for (const cJSON *other = entry->next; other; other = other->next) {
                const cJSON *other_id = field(other, "id");
                if (cJSON_IsString(other_id) && !strcmp(id, other_id->valuestring))
                    return false;
            }
            if (!group)
                for (const cJSON *other = stations->child; other; other = other->next) {
                    const cJSON *other_id = field(other, "id");
                    if (cJSON_IsString(other_id) && !strcmp(id, other_id->valuestring))
                        return false;
                }
        }
    }
    return true;
}
bool pw_validate_config(const cJSON *config) {
    return required_keys(config, config_keys, 4) && integer(field(config, "schema"), 1, 1) &&
           integer(field(config, "revision"), 1, UINT32_MAX) &&
           complete_settings(field(config, "settings")) &&
           pw_validate_catalog(field(config, "catalog"));
}
bool pw_validate_next_config(const cJSON *config, uint32_t previous_revision) {
    return previous_revision > 0 && previous_revision < UINT32_MAX && pw_validate_config(config) &&
           field(config, "revision")->valuedouble == (double)previous_revision + 1;
}
cJSON *pw_parse_json(const char *json, size_t length) {
    if (!json || length < 3 || length > 128 * 1024 || json[length - 1] != 0 ||
        memchr(json, 0, length - 1))
        return NULL;
    bool quoted = false;
    unsigned depth = 0;
    for (size_t i = 0; i < length - 1; ++i) {
        unsigned char value = (unsigned char)json[i];
        if (quoted) {
            if (value < 0x20)
                return NULL;
            if (value == '\\') {
                if (i + 1 >= length - 1)
                    return NULL;
                if (json[i + 1] == 'u' && i + 5 < length - 1 && !memcmp(json + i + 2, "0000", 4))
                    return NULL;
                ++i; /* Escaped slash/quote is data, never the start of another escape. */
            } else if (value == '"')
                quoted = false;
        } else if (value < 0x20 && value != '\t' && value != '\r' && value != '\n')
            return NULL;
        else if (value == '"')
            quoted = true;
        else if (value == '{' || value == '[') {
            if (++depth > 16)
                return NULL;
        } else if (value == '}' || value == ']') {
            if (!depth)
                return NULL;
            --depth;
        }
    }
    if (quoted || depth)
        return NULL;
    const char *end = NULL;
    cJSON *result = cJSON_ParseWithLengthOpts(json, length, &end, true);
    if ((!cJSON_IsObject(result) && !cJSON_IsArray(result)) || end != json + length - 1) {
        cJSON_Delete(result);
        return NULL;
    }
    return result;
}

// SPDX-License-Identifier: MIT
#include "pw_validation.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned checked;
static void check(bool value, const char *name) {
    if (!value)
        fprintf(stderr, "FAIL: %s\n", name);
    assert(value);
    ++checked;
}
static const char valid_json[] =
    "{\"schema\":1,\"revision\":1,\"settings\":{\"name\":\"Wohnzimmer\",\"brightness\":65,"
    "\"haptic\":true,"
    "\"avatar_enabled\":true,\"avatar_blond\":false,\"screensaver_mode\":\"weather_photo\","
    "\"weather_enabled\":false,\"latitude\":null,\"longitude\":null,\"timezone\":\"Europe/"
    "Berlin\"},"
    "\"catalog\":{\"favorites\":[],\"stations\":[]}}";
static cJSON *valid(void) {
    cJSON *value = cJSON_Parse(valid_json);
    assert(value);
    return value;
}
static cJSON *settings(cJSON *config) {
    return cJSON_GetObjectItemCaseSensitive(config, "settings");
}
static cJSON *catalog(cJSON *config) {
    return cJSON_GetObjectItemCaseSensitive(config, "catalog");
}
static void patch(const char *json, bool expected, const char *name) {
    cJSON *value = cJSON_Parse(json);
    assert(value);
    check(pw_validate_settings_patch(value) == expected, name);
    cJSON_Delete(value);
}
static cJSON *favorite(const char *id) {
    cJSON *value =
        cJSON_Parse("{\"id\":\"f\",\"kind\":\"spotify_playlist\",\"name\":\"Entspannen\",\"uri\":"
                    "\"spotify:playlist:0123456789abcdefghABCD\",\"enabled\":true}");
    assert(value);
    cJSON_ReplaceItemInObjectCaseSensitive(value, "id", cJSON_CreateString(id));
    return value;
}
static cJSON *station(const char *id) {
    cJSON *value = cJSON_Parse("{\"id\":\"s\",\"name\":\"Radio\",\"url\":\"https://stream.radio.de/"
                               "live.mp3\",\"enabled\":true}");
    assert(value);
    cJSON_ReplaceItemInObjectCaseSensitive(value, "id", cJSON_CreateString(id));
    return value;
}
int main(void) {
    const char *valid_parse[] = {"{}", "[]", " \t{\"x\":1}\r\n", "{\"literal\":\"\\\\u0000\"}",
                                 "{\"quote\":\"\\\"}\"}"};
    for (size_t i = 0; i < sizeof valid_parse / sizeof valid_parse[0]; ++i) {
        cJSON *parsed = pw_parse_json(valid_parse[i], strlen(valid_parse[i]) + 1);
        check(parsed != NULL, "strict parser accepts complete valid JSON");
        cJSON_Delete(parsed);
    }
    const char *invalid_parse[] = {"{} trailing",
                                   "{}{}",
                                   "{\"name\":\"ok\\u0000hidden\"}",
                                   "{\"name\\u0000hidden\":1}",
                                   "{\"odd\":\"\\\\\\u0000\"}",
                                   "{",
                                   "42",
                                   "null",
                                   "{\"x\":\"unterminated",
                                   "{\"x\":true,}",
                                   "[[[[[[[[[[[[[[[[[0]]]]]]]]]]]]]]]]]"};
    for (size_t i = 0; i < sizeof invalid_parse / sizeof invalid_parse[0]; ++i) {
        cJSON *parsed = pw_parse_json(invalid_parse[i], strlen(invalid_parse[i]) + 1);
        check(parsed == NULL, "strict parser rejects ambiguous or resource-excessive JSON");
        cJSON_Delete(parsed);
    }
    check(!pw_parse_json("{\"x\":\"raw\nline\"}", sizeof "{\"x\":\"raw\nline\"}"),
          "unescaped control character rejected");
    check(!pw_parse_json("{\v}", sizeof "{\v}"), "non-JSON whitespace rejected");
    const char raw_nul[] = "{}\0hidden";
    check(!pw_parse_json(raw_nul, sizeof raw_nul), "raw embedded NUL rejected");
    check(!pw_parse_json("{}", 2), "missing required terminator rejected");
    check(!pw_parse_json(NULL, 5), "null JSON buffer rejected");
    check(!pw_validate_config(NULL), "null config");
    check(!pw_validate_catalog(NULL), "null catalog");
    check(!pw_validate_settings_patch(NULL), "null patch");
    check(!pw_public_radio_url(NULL), "null URL");
    check(!pw_keys_only(NULL, NULL, 0), "null keys object");
    cJSON *value = valid();
    check(pw_validate_config(value), "complete defaults accepted");
    cJSON_Delete(value);
    const char *bad_roots[] = {"{}",
                               "[]",
                               "null",
                               "42",
                               "{\"schema\":1,\"revision\":1,\"settings\":{}}",
                               "{\"schema\":1,\"revision\":1,\"settings\":{},\"catalog\":null}"};
    for (size_t i = 0; i < sizeof bad_roots / sizeof bad_roots[0]; ++i) {
        value = cJSON_Parse(bad_roots[i]);
        check(!pw_validate_config(value), "malformed or incomplete root rejected");
        cJSON_Delete(value);
    }
    value = valid();
    cJSON_AddNumberToObject(value, "revision", 2);
    check(!pw_validate_config(value), "duplicate revision rejected");
    cJSON_Delete(value);
    value = valid();
    cJSON_AddBoolToObject(value, "extra", true);
    check(!pw_validate_config(value), "unknown root key rejected");
    cJSON_Delete(value);
    double bad_revisions[] = {0, -1, .5, (double)UINT32_MAX + 1, INFINITY, NAN};
    for (size_t i = 0; i < sizeof bad_revisions / sizeof bad_revisions[0]; ++i) {
        value = valid();
        cJSON *revision = cJSON_CreateNumber(1);
        assert(revision);
        revision->valuedouble = bad_revisions[i];
        cJSON_ReplaceItemInObjectCaseSensitive(value, "revision", revision);
        check(!pw_validate_config(value), "unsafe revision rejected");
        cJSON_Delete(value);
    }
    value = valid();
    cJSON_ReplaceItemInObjectCaseSensitive(value, "revision", cJSON_CreateNumber(UINT32_MAX));
    check(pw_validate_config(value), "last uint32 revision representable");
    cJSON_Delete(value);
    value = valid();
    check(!pw_validate_next_config(value, 1), "replayed revision rejected");
    check(!pw_validate_next_config(value, 0), "zero previous revision rejected");
    cJSON_ReplaceItemInObjectCaseSensitive(value, "revision", cJSON_CreateNumber(2));
    check(pw_validate_next_config(value, 1), "direct successor accepted");
    check(!pw_validate_next_config(value, 3), "older candidate rejected");
    cJSON_ReplaceItemInObjectCaseSensitive(value, "revision", cJSON_CreateNumber(9));
    check(!pw_validate_next_config(value, 1), "skipped revision rejected");
    cJSON_ReplaceItemInObjectCaseSensitive(value, "revision", cJSON_CreateNumber(UINT32_MAX));
    check(pw_validate_next_config(value, UINT32_MAX - 1), "final representable successor accepted");
    check(!pw_validate_next_config(value, UINT32_MAX), "exhausted revision rejected");
    cJSON_ReplaceItemInObjectCaseSensitive(value, "revision", cJSON_CreateNumber(1));
    check(!pw_validate_next_config(value, UINT32_MAX), "revision wrap rejected");
    cJSON_Delete(value);
    value = valid();
    cJSON_DeleteItemFromObjectCaseSensitive(settings(value), "timezone");
    check(!pw_validate_config(value), "missing default field rejected");
    cJSON_Delete(value);
    value = valid();
    cJSON_ReplaceItemInObjectCaseSensitive(settings(value), "weather_enabled",
                                           cJSON_CreateBool(true));
    check(!pw_validate_config(value), "weather without location rejected");
    cJSON_ReplaceItemInObjectCaseSensitive(settings(value), "latitude", cJSON_CreateNumber(52.52));
    check(!pw_validate_config(value), "half location rejected");
    cJSON_ReplaceItemInObjectCaseSensitive(settings(value), "longitude",
                                           cJSON_CreateNumber(13.405));
    check(pw_validate_config(value), "complete weather location accepted");
    cJSON_Delete(value);

    patch("{\"name\":\"Küche ☀\"}", true, "UTF-8 display name accepted");
    patch("{\"avatar_enabled\":false,\"avatar_blond\":true,\"screensaver_mode\":\"off\"}", true,
          "avatar and disabled screensaver accepted");
    patch("{\"screensaver_mode\":\"weather_photo\"}", true, "weather photo mode accepted");
    patch("{\"screensaver_mode\":\"sleep\"}", false, "unqualified screensaver mode rejected");
    patch("{\"screensaver_mode\":null}", false, "null mode rejected");
    patch("{\"avatar_enabled\":1}", false, "numeric avatar switch rejected");
    patch("{\"avatar_blond\":\"false\"}", false, "string hair switch rejected");
    value = valid();
    cJSON_DeleteItemFromObjectCaseSensitive(settings(value), "avatar_enabled");
    check(!pw_validate_config(value), "missing avatar default rejected");
    cJSON_Delete(value);
    patch("{\"brightness\":1}", true, "minimum brightness accepted");
    patch("{\"brightness\":100}", true, "maximum brightness accepted");
    patch("{\"brightness\":0}", false, "zero brightness rejected");
    patch("{\"brightness\":2.5}", false, "fractional brightness rejected");
    patch("{\"brightness\":101}", false, "excess brightness rejected");
    patch("{\"haptic\":1}", false, "truthy number rejected");
    patch("{\"haptic\":false,\"weather_enabled\":true}", true,
          "partial patch permits existing location");
    patch("{\"weather_enabled\":true,\"latitude\":null}", false,
          "explicit removed active location rejected");
    patch("{\"latitude\":52,\"longitude\":null}", false, "partial explicit location rejected");
    patch("{\"latitude\":91}", false, "latitude bounds");
    patch("{\"longitude\":-181}", false, "longitude bounds");
    patch("{\"timezone\":\"UTC\"}", false, "unqualified timezone rejected");
    patch("{\"timezone\":\"Europe/Berlin\"}", true, "Germany timezone accepted");
    patch("{}", false, "empty patch rejected");
    patch("{\"brightness\":30,\"brightness\":40}", false, "duplicate patch rejected");
    patch("{\"name\":\"\"}", false, "empty name rejected");
    patch("{\"name\":\"   \"}", false, "blank name rejected");
    patch("{\"name\":\"a\\nb\"}", false, "embedded control rejected");
    patch("{\"Name\":\"wrong case\"}", false, "wrong-case key rejected");
    patch("{\"name\":\"x\",\"secret\":\"x\"}", false, "unknown patch key rejected");
    char long_name[50];
    memset(long_name, 'a', 49);
    long_name[49] = 0;
    value = cJSON_CreateObject();
    cJSON_AddStringToObject(value, "name", long_name);
    check(!pw_validate_settings_patch(value), "UTF-8 byte bound enforced");
    cJSON_Delete(value);
    const char *invalid_utf8[] = {"\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80",
                                  "\xe2\x82", "\x80",         "\xc2\x85"};
    for (size_t i = 0; i < sizeof invalid_utf8 / sizeof invalid_utf8[0]; ++i) {
        value = cJSON_CreateObject();
        cJSON_AddStringToObject(value, "name", invalid_utf8[i]);
        check(!pw_validate_settings_patch(value), "invalid or control UTF-8 rejected");
        cJSON_Delete(value);
    }

    const char *good_urls[] = {"https://stream.radio.de/live.mp3",
                               "http://ice.radio.org:8000/live?token=abc%2Fdef",
                               "https://8.8.8.8/radio",
                               "https://xn--br-kka.radio/stream",
                               "https://AUDIO.RADIO.DE/live",
                               "https://radio.de"};
    for (size_t i = 0; i < sizeof good_urls / sizeof good_urls[0]; ++i)
        check(pw_public_radio_url(good_urls[i]), "public URL accepted");
    const char *bad_urls[] = {"",
                              "file:///radio.mp3",
                              "javascript:alert(1)",
                              "ftp://radio.de/live",
                              "https://u:p@radio.de/live",
                              "https://radio.de@127.0.0.1/live",
                              "https://radio.de\\@127.0.0.1/live",
                              "https://127.0.0.1/live",
                              "https://10.1.2.3/live",
                              "https://172.16.0.1/live",
                              "https://192.168.1.1/live",
                              "https://169.254.169.254/latest",
                              "https://0.0.0.0/live",
                              "https://100.64.0.1/live",
                              "https://198.18.0.1/live",
                              "https://192.0.2.1/live",
                              "https://224.0.0.1/live",
                              "https://255.255.255.255/live",
                              "https://2130706433/live",
                              "https://127.1/live",
                              "https://0177.0.0.1/live",
                              "https://0x7f.0.0.1/live",
                              "https://[::1]/live",
                              "https://[::ffff:127.0.0.1]/live",
                              "https://localhost/live",
                              "https://speaker.local/live",
                              "https://SPEAKER.LOCAL/live",
                              "https://speaker.internal/live",
                              "https://speaker.test/live",
                              "https://speaker.invalid/live",
                              "https://radio.de./live",
                              "https://a..de/live",
                              "https://-a.de/live",
                              "https://a-.de/live",
                              "https://a.123/live",
                              "https://radio.de:/live",
                              "https://radio.de:0/live",
                              "https://radio.de:65536/live",
                              "https://radio.de:abc/live",
                              "https://radio.de/with space",
                              "https://radio.de/live\r\nHost:x",
                              "https://radio.de/%0d%0aHost:x",
                              "https://radio.de/%00",
                              "https://radio.de/%",
                              "https://radio.de/%xx",
                              "https://radio.de/#fragment",
                              "https://radio.de/ö"};
    for (size_t i = 0; i < sizeof bad_urls / sizeof bad_urls[0]; ++i)
        check(!pw_public_radio_url(bad_urls[i]), "unsafe or ambiguous URL rejected");

    value = valid();
    cJSON *favorites = cJSON_GetObjectItemCaseSensitive(catalog(value), "favorites"),
          *stations = cJSON_GetObjectItemCaseSensitive(catalog(value), "stations");
    cJSON_AddItemToArray(favorites, favorite("favorite_1"));
    cJSON_AddItemToArray(stations, station("station_1"));
    check(pw_validate_config(value), "valid curated catalog accepted");
    cJSON_AddItemToArray(stations, station("favorite_1"));
    check(!pw_validate_catalog(catalog(value)), "cross-kind duplicate ID rejected");
    cJSON_DeleteItemFromArray(stations, 1);
    cJSON_AddItemToArray(favorites, favorite("favorite_1"));
    check(!pw_validate_catalog(catalog(value)), "same-kind duplicate ID rejected");
    cJSON_DeleteItemFromArray(favorites, 1);
    cJSON *first = cJSON_GetArrayItem(favorites, 0);
    cJSON_ReplaceItemInObjectCaseSensitive(first, "kind", cJSON_CreateString("spotify_show"));
    check(!pw_validate_catalog(catalog(value)), "URI kind mismatch rejected");
    cJSON_ReplaceItemInObjectCaseSensitive(first, "kind", cJSON_CreateString("spotify_playlist"));
    cJSON_ReplaceItemInObjectCaseSensitive(
        first, "uri", cJSON_CreateString("spotify:playlist:0123456789abcdefghABC_"));
    check(!pw_validate_catalog(catalog(value)), "non-base62 Spotify ID rejected");
    cJSON_ReplaceItemInObjectCaseSensitive(
        first, "uri", cJSON_CreateString("spotify:playlist:0123456789abcdefghABCD?x"));
    check(!pw_validate_catalog(catalog(value)), "Spotify trailing query rejected");
    cJSON_ReplaceItemInObjectCaseSensitive(
        first, "uri", cJSON_CreateString("spotify:playlist:0123456789abcdefghABCD"));
    cJSON_AddStringToObject(first, "name", "ambiguous");
    check(!pw_validate_catalog(catalog(value)), "duplicate nested name rejected");
    cJSON_Delete(value);
    value = valid();
    favorites = cJSON_GetObjectItemCaseSensitive(catalog(value), "favorites");
    for (unsigned i = 0; i < 64; ++i) {
        char id[24];
        snprintf(id, sizeof id, "favorite_%u", i);
        cJSON_AddItemToArray(favorites, favorite(id));
    }
    check(pw_validate_catalog(catalog(value)), "64-item budget accepted");
    char *large_json = cJSON_PrintUnformatted(value);
    assert(large_json);
    size_t blob_bytes = strlen(large_json) + 1;
    check(blob_bytes > 4000 && blob_bytes < 49152,
          "real 64-favorite configuration exceeds NVS string limit but fits blob budget");
    cJSON *blob_roundtrip = pw_parse_json(large_json, blob_bytes);
    check(pw_validate_config(blob_roundtrip),
          "large configuration round-trips with explicit blob length and terminator");
    check(!pw_parse_json(large_json, blob_bytes - 1),
          "unterminated stored blob cannot become defaults");
    cJSON_Delete(blob_roundtrip);
    cJSON_free(large_json);
    cJSON_AddItemToArray(favorites, favorite("overflow"));
    check(!pw_validate_catalog(catalog(value)), "65th item rejected");
    cJSON_Delete(value);
    value = cJSON_Parse("{\"favorites\":[],\"stations\":[{}]}");
    check(!pw_validate_catalog(value), "missing station fields rejected");
    cJSON_Delete(value);
    value = cJSON_Parse("{\"favorites\":[],\"stations\":[] ,\"stations\":[]}");
    check(!pw_validate_catalog(value), "duplicate list rejected");
    cJSON_Delete(value);
    printf("%u native validation checks passed (ASan/UBSan).\n", checked);
    return 0;
}

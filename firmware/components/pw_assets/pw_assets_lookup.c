// SPDX-FileCopyrightText: 2026 Passion Wave
// SPDX-License-Identifier: MIT
#include "pw_assets_internal.h"
#include <string.h>
const char *pw_assets_name(int id) {
    return id >= 0 && id < PW_ASSET_COUNT ? pw_asset_sources[id].name : "";
}
int pw_assets_weather_id(const char *symbol) {
    if (!symbol || !*symbol) return PW_ASSET_NONE;
    const char *name = NULL;
    // Exact symbol vocabulary stems, followed only by MET's day/night suffix.
    // Unrecognized codes never select a plausible-looking fallback photograph.
    static const struct { const char *stem, *photo; } map[] = {
        {"clearsky", "sunny"}, {"fair", "partlycloudy"}, {"partlycloudy", "partlycloudy"},
        {"cloudy", "cloudy"}, {"fog", "fog"},
        {"lightrain", "rainy"}, {"rain", "rainy"}, {"heavyrain", "pouring"},
        {"lightrainshowers", "rainy"}, {"rainshowers", "rainy"}, {"heavyrainshowers", "pouring"},
        {"lightsleet", "snowy-rainy"}, {"sleet", "snowy-rainy"}, {"heavysleet", "snowy-rainy"},
        {"lightsleetshowers", "snowy-rainy"}, {"sleetshowers", "snowy-rainy"}, {"heavysleetshowers", "snowy-rainy"},
        {"lightsnow", "snowy"}, {"snow", "snowy"}, {"heavysnow", "snowy"},
        {"lightsnowshowers", "snowy"}, {"snowshowers", "snowy"}, {"heavysnowshowers", "snowy"},
        {"lightrainandthunder", "lightning-rainy"}, {"rainandthunder", "lightning-rainy"}, {"heavyrainandthunder", "lightning-rainy"},
        {"lightrainshowersandthunder", "lightning-rainy"}, {"rainshowersandthunder", "lightning-rainy"}, {"heavyrainshowersandthunder", "lightning-rainy"},
        {"lightsleetandthunder", "lightning"}, {"sleetandthunder", "lightning"}, {"heavysleetandthunder", "lightning"},
        {"lightsleetshowersandthunder", "lightning"}, {"sleetshowersandthunder", "lightning"}, {"heavysleetshowersandthunder", "lightning"},
        {"lightsnowandthunder", "lightning"}, {"snowandthunder", "lightning"}, {"heavysnowandthunder", "lightning"},
        {"lightsnowshowersandthunder", "lightning"}, {"snowshowersandthunder", "lightning"}, {"heavysnowshowersandthunder", "lightning"},
        // MET keeps these historical extra-s spellings for API compatibility.
        {"lightssleetshowersandthunder", "lightning"}, {"lightssnowshowersandthunder", "lightning"},
    };
    const char *suffix = strchr(symbol, '_');
    const size_t length = suffix ? (size_t)(suffix - symbol) : strlen(symbol);
    if (suffix && strcmp(suffix, "_day") && strcmp(suffix, "_night") && strcmp(suffix, "_polartwilight")) return PW_ASSET_NONE;
    for (unsigned i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (strlen(map[i].stem) == length && !strncmp(symbol, map[i].stem, length)) {
            name = map[i].photo;
            if (!strcmp(map[i].stem, "clearsky") && suffix && !strcmp(suffix, "_night")) name = "clear-night";
            break;
        }
    }
    if (!name) return PW_ASSET_NONE;
    for (int i = PW_ASSET_AVATAR_COUNT; i < PW_ASSET_COUNT; ++i)
        if (!strcmp(pw_asset_sources[i].name + sizeof("weather/") - 1, name)) return i;
    return PW_ASSET_NONE;
}

// SPDX-License-Identifier: MIT
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pw_assets.h"

static void expect(const char *symbol, const char *name) {
    int id = pw_assets_weather_id(symbol);
    assert(id >= PW_ASSET_AVATAR_COUNT && id < PW_ASSET_COUNT);
    assert(strcmp(pw_assets_name(id), name) == 0);
}
int main(void) {
    expect("clearsky_day", "weather/sunny");
    expect("clearsky_night", "weather/clear-night");
    expect("fair_polartwilight", "weather/partlycloudy");
    expect("cloudy", "weather/cloudy");
    expect("fog", "weather/fog");
    expect("lightrainshowers_day", "weather/rainy");
    expect("heavyrain", "weather/pouring");
    expect("lightsleet_night", "weather/snowy-rainy");
    expect("heavysnowshowers_night", "weather/snowy");
    expect("heavyrainshowersandthunder_day", "weather/lightning-rainy");
    // Public MET spellings retain a historic extra s, unlike their labels.
    expect("lightssleetshowersandthunder_day", "weather/lightning");
    expect("lightssnowshowersandthunder_night", "weather/lightning");
    const char *unknown[] = {NULL, "", "unknown", "clearskygarbage", "clearsky_day_extra",
                            "fair_sunset", "_night", "CLOUDY", "sunny", "lightning"};
    for (unsigned i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i)
        assert(pw_assets_weather_id(unknown[i]) == PW_ASSET_NONE);
    assert(!*pw_assets_name(-1) && !*pw_assets_name(PW_ASSET_COUNT));
    puts("MET photo lookup and unknown-state rejection passed");
}

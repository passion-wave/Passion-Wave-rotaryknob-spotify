// SPDX-License-Identifier: MIT
// Adapter for original Passion Wave 2026 deterministic outfit rules.
// Original rule implementation and its MIT license are retained in avatar_port/.
#include "pw_weather_model.h"
#include "avatar_port/weather_avatar.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
uint8_t condition(const char *symbol) {
    if (std::strstr(symbol,"sleet")) return 12;
    if (std::strstr(symbol,"snow")) return 11;
    if (std::strstr(symbol,"rain") && std::strstr(symbol,"thunder")) return 7;
    if (std::strstr(symbol,"heavyrain")) return 9;
    if (std::strstr(symbol,"rain")) return 10;
    if (std::strstr(symbol,"thunder")) return 6;
    if (!std::strncmp(symbol,"clearsky",8)) return std::strstr(symbol,"night")?1:13;
    if (!std::strncmp(symbol,"fair",4) || !std::strncmp(symbol,"partlycloudy",12)) return 8;
    if (!std::strcmp(symbol,"cloudy")) return 2;
    if (!std::strcmp(symbol,"fog")) return 4;
    return 0;
}
}
extern "C" void pw_weather_avatar_resolve(const pw_weather_snapshot_t *snapshot,int64_t now,
                                          bool blond,pw_weather_avatar_t *output) {
    if (!output) return;
    *output={};
    weather_avatar::Context context;
    pw_weather_point_t hours[4];
    if(snapshot && now>0 && now<UINT32_MAX && pw_weather_avatar_window(snapshot,now,hours)) {
        context.mode=weather_avatar::FORECAST;
        context.fetched_at=static_cast<uint32_t>(snapshot->checked_utc);
        context.provider_at=static_cast<uint32_t>(snapshot->source_updated_utc);
        context.sequence=snapshot->location_generation;
        context.start_utc=static_cast<uint32_t>(hours[0].time_utc);
        context.end_utc=static_cast<uint32_t>(hours[3].interval_end_utc);
        context.flags=weather_avatar::TEMP_VALID;
        float temp_min=hours[0].temperature_c,temp_max=temp_min,rain=0,wind=0,gust=0,probability=0;
        unsigned wind_count=0,probability_count=0;
        for(unsigned i=0;i<4;++i) {
            const auto &p=hours[i];
            temp_min=std::min(temp_min,p.temperature_c); temp_max=std::max(temp_max,p.temperature_c);
            uint8_t code=condition(p.symbol);
            context.conditions|=uint16_t(1u<<code);
            if(!i) context.first_condition=code;
            if(code) context.flags|=weather_avatar::CONDITIONS_KNOWN;
            if(std::strstr(p.symbol,"thunder")) context.conditions|=weather_avatar::condition_bit(6);
            bool liquid=code==7 || code==9 || code==10 || code==12;
            if(liquid) context.flags|=weather_avatar::LIQUID_RAIN;
            if(p.valid&PW_WEATHER_WIND) { wind=std::max(wind,p.wind_mps*3.6f); ++wind_count; context.flags|=weather_avatar::WIND_VALID; }
            if(p.valid&PW_WEATHER_GUST) { gust=std::max(gust,p.gust_mps*3.6f); context.flags|=weather_avatar::GUST_VALID; }
            if(p.valid&PW_WEATHER_PRECIPITATION) { rain+=p.precipitation_mm; context.flags|=weather_avatar::PRECIPITATION_VALID; }
            if(p.valid&PW_WEATHER_RAIN_PROBABILITY) { probability=std::max(probability,p.rain_probability_percent); ++probability_count; context.flags|=weather_avatar::PROBABILITY_VALID; }
            if(code!=11 && code!=5 && !liquid &&
               (((p.valid&PW_WEATHER_PRECIPITATION) && p.precipitation_mm>0) ||
                ((p.valid&PW_WEATHER_RAIN_PROBABILITY) && p.rain_probability_percent>=50)))
                context.flags|=weather_avatar::WET_PHASE_UNKNOWN;
        }
        if(wind_count!=4 || probability_count!=4 || (context.conditions&1)) context.flags|=weather_avatar::PARTIAL_DATA;
        context.temperature_min_tenths=static_cast<int16_t>(std::floor(temp_min*10.0));
        context.temperature_max_tenths=static_cast<int16_t>(std::ceil(temp_max*10.0));
        context.wind_tenths=static_cast<uint16_t>(std::min(65535.0,std::floor(wind*10.0)));
        context.gust_tenths=static_cast<uint16_t>(std::min(65535.0,std::floor(gust*10.0)));
        context.precipitation_tenths=static_cast<uint16_t>(std::min(65535.0,std::round(rain*10.0)));
        context.precipitation_probability=static_cast<uint8_t>(std::floor(probability));
    }
    auto outfit=weather_avatar::resolve(context,now>0 && now<UINT32_MAX?static_cast<uint32_t>(now):0);
    output->valid=outfit.valid; output->partial=outfit.partial;
    output->windy=outfit.windy; output->wet=outfit.liquid_rain||outfit.wet_unknown;
    output->snow=outfit.protection==weather_avatar::Protection::SNOW;
    output->sun_hat=outfit.sun_hat; output->hail=outfit.hail; output->lightning=outfit.lightning;
    output->asset_index=outfit.asset_index(blond); output->thermal_band=static_cast<uint8_t>(outfit.thermal);
    output->protection=static_cast<uint8_t>(outfit.protection); output->scene=static_cast<uint8_t>(outfit.scene);
    output->start_utc=context.start_utc; output->end_utc=context.end_utc;
    std::snprintf(output->scene_label,sizeof(output->scene_label),"%s",weather_avatar::scene_label(outfit));
    std::snprintf(output->outfit_label,sizeof(output->outfit_label),"%s",weather_avatar::outfit_label(outfit));
}

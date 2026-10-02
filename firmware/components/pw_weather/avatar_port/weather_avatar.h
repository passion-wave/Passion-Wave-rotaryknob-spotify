#pragma once

// Pure, deterministic outfit selection. No networking, LVGL or device state.
#include <cstdint>
#include "weather_avatar_context.h"

namespace weather_avatar {

// ESPHome defines HOT as a function attribute macro.
enum class Thermal : uint8_t { HOT_WEATHER, WARM, MILD, COOL, COLD, FROST };
enum class Protection : uint8_t { DRY, WIND, WET, SNOW };
enum class Scene : uint8_t {
  EVERYDAY, WIND, RAIN, POURING, SNOW, MIX, FOG, NIGHT,
  HAIL, LIGHTNING, LIGHTNING_RAIN, EXCEPTIONAL, UNAVAILABLE
};

constexpr uint16_t condition_bit(uint8_t code) {
  return code > 0 && code < 16 ? uint16_t(1U << code) : 0;
}
constexpr bool has_condition(const Context &c, uint8_t code) {
  return (c.conditions & condition_bit(code)) != 0;
}
constexpr Thermal thermal_band(int16_t temperature_tenths) {
  return temperature_tenths >= 280 ? Thermal::HOT_WEATHER :
         temperature_tenths >= 220 ? Thermal::WARM :
         temperature_tenths >= 160 ? Thermal::MILD :
         temperature_tenths >= 100 ? Thermal::COOL :
         temperature_tenths >= 0 ? Thermal::COLD : Thermal::FROST;
}

struct Outfit {
  bool valid{false};
  bool liquid_rain{false};
  bool wet_unknown{false};
  bool windy{false};
  bool sun_hat{false};
  bool hail{false};
  bool lightning{false};
  bool partial{false};
  Thermal thermal{Thermal::MILD};
  Protection protection{Protection::DRY};
  Scene scene{Scene::UNAVAILABLE};
  uint8_t reference_motif{20};
  // Four protection atlases, each containing six brown and six blond outfits.
  // The final atlas contains neutral brown/blond, then sun-hat brown/blond.
  uint8_t asset_index(bool blond) const {
    if (!valid || scene == Scene::EXCEPTIONAL) return 48 + uint8_t(blond);
    if (sun_hat) return 50 + uint8_t(blond);
    return uint8_t(protection) * 12 + uint8_t(blond) * 6 + uint8_t(thermal);
  }
};

inline bool context_is_fresh(const Context &c, uint32_t now) {
  if (c.schema != 1 || now < 1577836800U || c.fetched_at == 0 ||
      c.fetched_at > now || now - c.fetched_at > 1200U) return false;
  if (c.mode != FORECAST && c.mode != CURRENT) return false;
  if (c.end_utc <= now || c.end_utc <= c.start_utc) return false;
  // MET starts at the next full hour. Display that exact future window rather
  // than inventing coverage between fetch time and its first forecast point.
  if (c.mode == FORECAST && (c.end_utc - c.start_utc != 14400U ||
      c.start_utc < c.fetched_at || c.start_utc - c.fetched_at > 3600U)) return false;
  if (c.mode == CURRENT && c.start_utc > now) return false;
  if (c.mode == CURRENT && c.provider_at != 0 &&
      (c.provider_at > now || now - c.provider_at > 1200U)) return false;
  return true;
}

inline Outfit resolve(const Context &c, uint32_t now) {
  Outfit out;
  const bool fresh = context_is_fresh(c, now);
  out.hail = fresh && has_condition(c, 5);
  out.lightning = fresh && (has_condition(c, 6) || has_condition(c, 7));
  if (!fresh || !(c.flags & TEMP_VALID) || !(c.flags & CONDITIONS_KNOWN) ||
      (c.conditions & 0xfffeU) == 0 || c.temperature_min_tenths < -1000 ||
      c.temperature_max_tenths > 700 ||
      c.temperature_min_tenths > c.temperature_max_tenths) return out;

  out.valid = true;
  out.thermal = thermal_band(c.temperature_min_tenths);
  const bool snow = has_condition(c, 11) || has_condition(c, 12);
  out.liquid_rain = (c.flags & LIQUID_RAIN) || has_condition(c, 7) ||
                   has_condition(c, 9) || has_condition(c, 10) || has_condition(c, 12);
  out.wet_unknown = !out.liquid_rain && ((c.flags & WET_PHASE_UNKNOWN) ||
    (!snow && !out.hail && (((c.flags & PROBABILITY_VALID) && c.precipitation_probability >= 50) ||
                         ((c.flags & PRECIPITATION_VALID) && c.precipitation_tenths > 0))));
  out.windy = has_condition(c, 14) || has_condition(c, 15) ||
              ((c.flags & WIND_VALID) && c.wind_tenths >= 200) ||
              ((c.flags & GUST_VALID) && c.gust_tenths >= 350);
  out.partial = (c.flags & PARTIAL_DATA) || !(c.flags & WIND_VALID) ||
                !(c.flags & PROBABILITY_VALID);
  out.protection = snow ? Protection::SNOW :
    (out.liquid_rain || out.wet_unknown) ? Protection::WET :
    out.windy ? Protection::WIND : Protection::DRY;
  out.scene = Scene::EVERYDAY;
  out.reference_motif = uint8_t(out.thermal) + 1;

  if (has_condition(c, 3)) { out.scene = Scene::EXCEPTIONAL; out.reference_motif = 19; }
  else if (out.hail) { out.scene = Scene::HAIL; out.reference_motif = 16; }
  else if (out.lightning) {
    out.scene = out.liquid_rain ? Scene::LIGHTNING_RAIN : Scene::LIGHTNING;
    out.reference_motif = out.liquid_rain ? 18 : 17;
  } else if (snow && out.liquid_rain) { out.scene = Scene::MIX; out.reference_motif = 12; }
  else if (snow) { out.scene = Scene::SNOW; out.reference_motif = 13; }
  else if (has_condition(c, 9)) { out.scene = Scene::POURING; out.reference_motif = 11; }
  else if (out.liquid_rain || out.wet_unknown) {
    out.scene = Scene::RAIN;
    out.reference_motif = c.temperature_min_tenths >= 180 ? 9 :
                          c.temperature_min_tenths >= 50 ? 10 : 21; // cold rain, no snow
  } else if (out.windy) {
    out.scene = Scene::WIND;
    out.reference_motif = c.temperature_min_tenths >= 100 ? 7 : 8;
  } else if (has_condition(c, 4)) { out.scene = Scene::FOG; out.reference_motif = 14; }
  else if (c.first_condition == 1) { out.scene = Scene::NIGHT; out.reference_motif = 15; }

  out.sun_hat = out.scene == Scene::EVERYDAY && out.thermal == Thermal::HOT_WEATHER &&
                (has_condition(c, 13) || has_condition(c, 8));
  return out;
}

inline const char *scene_label(const Outfit &o) {
  switch (o.scene) {
    case Scene::UNAVAILABLE: return "Keine Wetterdaten";
    case Scene::EXCEPTIONAL: return "Vorhersage prüfen";
    case Scene::HAIL: return o.lightning ? "Hagel + Gewitter" : "Hagel";
    case Scene::LIGHTNING_RAIN: return "Gewitter + Regen";
    case Scene::LIGHTNING: return "Gewitter";
    case Scene::MIX: return "Regen und Schnee";
    case Scene::SNOW: return "Schnee";
    case Scene::POURING: return "Starker Regen";
    case Scene::RAIN: return o.liquid_rain ? "Regen" : "Niederschlag möglich";
    case Scene::WIND: return "Wind";
    case Scene::FOG: return "Nebel";
    case Scene::NIGHT: return "Klare Nacht";
    default: return "Dein Wetteroutfit";
  }
}

inline const char *outfit_label(const Outfit &o) {
  if (!o.valid) return o.hail || o.lightning ? "Wetterhinweis prüfen" : "Noch keine Empfehlung";
  if (o.scene == Scene::EXCEPTIONAL) return "Besondere Wetterlage";
  if (o.hail) return "Schutz suchen";
  if (o.lightning) return "Drinnen bleiben";
  if (o.protection == Protection::SNOW)
    return o.thermal >= Thermal::COLD ? "Warm + wasserdicht" : "Wasserdicht + griffig";
  if (o.protection == Protection::WET)
    return o.thermal >= Thermal::COLD ? "Warm + Nässeschutz" : "Leichter Nässeschutz";
  if (o.protection == Protection::WIND)
    return o.thermal >= Thermal::COLD ? "Warm + Windschutz" : "Leichter Windschutz";
  if (o.scene == Scene::FOG) return "Gut sichtbar kleiden";
  if (o.sun_hat) return "Sonnenhut + Shorts";
  switch (o.thermal) {
    case Thermal::HOT_WEATHER: case Thermal::WARM: return "T-Shirt + Shorts";
    case Thermal::MILD: return "Pullover + lange Hose";
    case Thermal::COOL: return "Leichte Jacke";
    case Thermal::COLD: return "Warme Jacke";
    case Thermal::FROST: return "Mütze + Handschuhe";
  }
  return "";
}

}  // namespace weather_avatar

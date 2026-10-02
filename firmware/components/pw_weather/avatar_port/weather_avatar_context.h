#pragma once

#include <cstddef>
#include <cstdint>

// Transport-neutral, versioned weather input. Replaying a UART snapshot never
// changes fetched_at; freshness is evaluated against UTC by the renderer.
namespace weather_avatar {
enum Mode : uint8_t { UNAVAILABLE = 0, FORECAST = 1, CURRENT = 2 };
enum Flags : uint16_t {
  TEMP_VALID = 1, CONDITIONS_KNOWN = 2, LIQUID_RAIN = 4,
  WET_PHASE_UNKNOWN = 8, FELT_TEMPERATURE = 16, PRECIPITATION_VALID = 32,
  WIND_VALID = 64, GUST_VALID = 128, PROBABILITY_VALID = 256, PARTIAL_DATA = 512,
};
struct Context {
  uint8_t schema{1};
  Mode mode{UNAVAILABLE};
  uint16_t flags{0};
  uint32_t sequence{0}, fetched_at{0}, provider_at{0}, start_utc{0}, end_utc{0};
  int16_t temperature_min_tenths{0}, temperature_max_tenths{0};
  // Bits 1..15 match dual_mcu::WeatherCondition. Bit 0 means unknown.
  uint16_t conditions{0};
  uint8_t first_condition{0}, precipitation_probability{0};
  uint16_t wind_tenths{0}, gust_tenths{0}, precipitation_tenths{0};
};
inline Context current_context{};
constexpr size_t CONTEXT_WIRE_SIZE = 38;

inline bool valid_context(const Context &value) {
  return value.schema == 1 && value.mode <= CURRENT &&
    value.first_condition <= 15 && value.precipitation_probability <= 100 &&
    (value.flags & ~uint16_t(1023)) == 0 &&
    ((value.flags & TEMP_VALID) == 0 ||
      (value.temperature_min_tenths >= -1000 &&
       value.temperature_max_tenths <= 700 &&
       value.temperature_min_tenths <= value.temperature_max_tenths)) &&
    (value.mode == UNAVAILABLE ||
      (value.fetched_at != 0 && value.start_utc <= value.end_utc));
}

inline bool update_context(const Context &value) {
  if (!valid_context(value) || value.fetched_at < current_context.fetched_at ||
      (value.fetched_at == current_context.fetched_at &&
       value.sequence <= current_context.sequence)) return false;
  current_context = value;
  return true;
}

inline void pack_context(const Context &value, uint8_t *out) {
  size_t offset = 0;
  const auto byte = [&](uint8_t v) { out[offset++] = v; };
  const auto word = [&](uint16_t v) { byte(v & 255); byte(v >> 8); };
  const auto dword = [&](uint32_t v) { word(v & 65535); word(v >> 16); };
  byte(value.schema); byte(value.mode); word(value.flags);
  dword(value.sequence); dword(value.fetched_at); dword(value.provider_at);
  dword(value.start_utc); dword(value.end_utc);
  word(static_cast<uint16_t>(value.temperature_min_tenths));
  word(static_cast<uint16_t>(value.temperature_max_tenths));
  word(value.conditions); byte(value.first_condition);
  byte(value.precipitation_probability); word(value.wind_tenths);
  word(value.gust_tenths); word(value.precipitation_tenths);
}

inline bool unpack_context(const uint8_t *data, size_t size, Context *value) {
  if (data == nullptr || value == nullptr || size != CONTEXT_WIRE_SIZE) return false;
  size_t offset = 0;
  const auto byte = [&]() -> uint8_t { return data[offset++]; };
  const auto word = [&]() -> uint16_t {
    const uint16_t low = byte(); return low | (uint16_t(byte()) << 8);
  };
  const auto dword = [&]() -> uint32_t {
    const uint32_t low = word(); return low | (uint32_t(word()) << 16);
  };
  Context decoded;
  decoded.schema = byte(); decoded.mode = static_cast<Mode>(byte());
  decoded.flags = word(); decoded.sequence = dword(); decoded.fetched_at = dword();
  decoded.provider_at = dword(); decoded.start_utc = dword(); decoded.end_utc = dword();
  decoded.temperature_min_tenths = static_cast<int16_t>(word());
  decoded.temperature_max_tenths = static_cast<int16_t>(word());
  decoded.conditions = word(); decoded.first_condition = byte();
  decoded.precipitation_probability = byte(); decoded.wind_tenths = word();
  decoded.gust_tenths = word(); decoded.precipitation_tenths = word();
  if (!valid_context(decoded)) return false;
  *value = decoded;
  return true;
}
}  // namespace weather_avatar

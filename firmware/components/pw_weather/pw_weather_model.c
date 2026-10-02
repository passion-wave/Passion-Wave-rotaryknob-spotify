// SPDX-License-Identifier: MIT
#include "pw_weather_model.h"
#include "cJSON.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { const char *start, *end; } json_view_t;
static const char *white(const char *p, const char *end) {
    while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) ++p;
    return p;
}
// A structural pass bounds nesting and validates skipped parts without allocating
// one cJSON DOM for the entire 128 KiB payload. Only meta and one point are parsed.
static const char *string_end(const char *p, const char *end) {
    if (p == end || *p++ != '"') return NULL;
    while (p < end) {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '"') return p;
        if (ch < 0x20) return NULL;
        if (ch == '\\') {
            if (p == end) return NULL;
            char esc = *p++;
            if (esc == 'u') {
                for (unsigned i = 0; i < 4; ++i)
                    if (p == end || !isxdigit((unsigned char)*p++)) return NULL;
            } else if (!strchr("\"\\/bfnrt", esc)) return NULL;
        }
    }
    return NULL;
}
static const char *value_end(const char *p, const char *end, unsigned depth) {
    if (depth > 24 || (p = white(p, end)) == end) return NULL;
    if (*p == '"') return string_end(p, end);
    if (*p == '[' || *p == '{') {
        const bool object = *p++ == '{';
        const char close = object ? '}' : ']';
        p = white(p, end);
        if (p != end && *p == close) return p + 1;
        while (p < end) {
            if (object) {
                p = string_end(p, end);
                if (!p || (p = white(p, end)) == end || *p++ != ':') return NULL;
            }
            p = value_end(p, end, depth + 1);
            if (!p || (p = white(p, end)) == end) return NULL;
            if (*p == close) return p + 1;
            if (*p++ != ',') return NULL;
            p = white(p, end);
        }
        return NULL;
    }
    const char *literals[] = {"true", "false", "null"};
    for (unsigned i = 0; i < 3; ++i) {
        size_t n = strlen(literals[i]);
        if ((size_t)(end - p) >= n && memcmp(p, literals[i], n) == 0) return p + n;
    }
    if (*p == '-') ++p;
    if (p == end || !isdigit((unsigned char)*p)) return NULL;
    if (*p == '0') ++p;
    else while (p < end && isdigit((unsigned char)*p)) ++p;
    if (p < end && *p == '.') {
        ++p;
        if (p == end || !isdigit((unsigned char)*p)) return NULL;
        while (p < end && isdigit((unsigned char)*p)) ++p;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        ++p;
        if (p < end && (*p == '+' || *p == '-')) ++p;
        if (p == end || !isdigit((unsigned char)*p)) return NULL;
        while (p < end && isdigit((unsigned char)*p)) ++p;
    }
    return p;
}
static bool member(json_view_t parent, const char *name, json_view_t *out) {
    const char *p = white(parent.start, parent.end);
    if (p == parent.end || *p++ != '{') return false;
    bool found = false;
    while ((p = white(p, parent.end)) < parent.end && *p != '}') {
        const char *key = p;
        const char *endkey = string_end(key, parent.end);
        if (!endkey) return false;
        p = white(endkey, parent.end);
        if (p == parent.end || *p++ != ':') return false;
        p = white(p, parent.end);
        const char *end = value_end(p, parent.end, 0);
        if (!end) return false;
        size_t keylen = (size_t)(endkey - key - 2);
        if (keylen == strlen(name) && memcmp(key + 1, name, keylen) == 0) {
            if (found) return false; // Reject duplicate relevant properties.
            *out = (json_view_t){p, end};
            found = true;
        }
        p = white(end, parent.end);
        if (p < parent.end && *p == ',') ++p;
        else break;
    }
    return found;
}
static int64_t civil_days(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + doe - 719468;
}
bool pw_weather_parse_utc(const char *s, int64_t *out) {
    if (!s || !out || strlen(s) != 20 || s[4] != '-' || s[7] != '-' ||
        s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != 'Z') return false;
    const unsigned pos[] = {0,1,2,3,5,6,8,9,11,12,14,15,17,18};
    for (size_t i = 0; i < sizeof(pos) / sizeof(pos[0]); ++i)
        if (!isdigit((unsigned char)s[pos[i]])) return false;
    int y = (s[0]-'0')*1000+(s[1]-'0')*100+(s[2]-'0')*10+s[3]-'0';
    int m=(s[5]-'0')*10+s[6]-'0', d=(s[8]-'0')*10+s[9]-'0';
    int h=(s[11]-'0')*10+s[12]-'0', n=(s[14]-'0')*10+s[15]-'0';
    int sec=(s[17]-'0')*10+s[18]-'0';
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (y < 2020 || y > 2100 || m < 1 || m > 12 || d < 1 || h > 23 || n > 59 || sec > 59) return false;
    int limit = days[m-1] + (m == 2 && y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    if (d > limit) return false;
    *out = civil_days(y, (unsigned)m, (unsigned)d)*86400 + h*3600 + n*60 + sec;
    return true;
}
int64_t pw_weather_parse_http_date(const char *s) {
    // HTTP IMF-fixdate is UTC with English month names, independent of locale/TZ.
    char day[4]={0},month[4]={0},zone[4]={0},tail=0;
    int date,year,hour,minute,second;
    if(!s || sscanf(s,"%3[^,], %d %3s %d %d:%d:%d %3s %c",day,&date,month,&year,&hour,&minute,&second,zone,&tail)!=8 || strcmp(zone,"GMT")) return 0;
    static const char *months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
    int m=0;
    while(m<12 && strcmp(month,months[m])) ++m;
    if(m==12 || year<2020 || year>2100 || date<1 || date>31 || hour<0 || hour>23 || minute<0 || minute>59 || second<0 || second>59) return 0;
    char iso[24];
    snprintf(iso,sizeof(iso),"%04d-%02d-%02dT%02d:%02d:%02dZ",year,m+1,date,hour,minute,second);
    int64_t result=0;
    return pw_weather_parse_utc(iso,&result)?result:0;
}
static const cJSON *get(const cJSON *object, const char *key) { return cJSON_GetObjectItemCaseSensitive(object, key); }
static const char *str(const cJSON *j) { return cJSON_IsString(j) ? j->valuestring : NULL; }
static bool field(const cJSON *details, const cJSON *units, const char *key,
                  const char *unit, double lo, double hi, float *output) {
    const cJSON *v = get(details, key);
    const char *u = str(get(units, key));
    if (!u || strcmp(u, unit) || !cJSON_IsNumber(v) || !isfinite(v->valuedouble) ||
        v->valuedouble < lo || v->valuedouble > hi) return false;
    *output = (float)v->valuedouble;
    return true;
}
static bool symbol(const cJSON *data, char *output, size_t size) {
    const char *s = str(get(get(data, "summary"), "symbol_code"));
    if (!s || strlen(s) >= size || !*s) return false;
    for (const char *p = s; *p; ++p) if (!(*p >= 'a' && *p <= 'z') && *p != '_') return false;
    strcpy(output, s);
    return true;
}
static void read_point(const cJSON *data, const cJSON *units, int64_t t, pw_weather_point_t *p) {
    memset(p, 0, sizeof(*p));
    p->time_utc = t;
    const cJSON *instant = get(get(data, "instant"), "details");
#define READ(key, unit, lo, hi, target, bit) do { if (field(instant, units, key, unit, lo, hi, &p->target)) p->valid |= bit; } while (0)
    READ("air_temperature", "celsius", -90, 65, temperature_c, PW_WEATHER_TEMPERATURE);
    READ("apparent_air_temperature", "celsius", -110, 80, apparent_temperature_c, PW_WEATHER_APPARENT_TEMPERATURE);
    READ("relative_humidity", "%", 0, 100, humidity_percent, PW_WEATHER_HUMIDITY);
    READ("wind_speed", "m/s", 0, 150, wind_mps, PW_WEATHER_WIND);
    READ("wind_from_direction", "degrees", 0, 360, wind_from_degrees, PW_WEATHER_WIND_DIRECTION);
    READ("wind_speed_of_gust", "m/s", 0, 200, gust_mps, PW_WEATHER_GUST);
    READ("cloud_area_fraction", "%", 0, 100, cloud_percent, PW_WEATHER_CLOUD);
    READ("ultraviolet_index_clear_sky", "1", 0, 30, uv_index, PW_WEATHER_UV);
#undef READ
    const cJSON *next = get(data, "next_1_hours");
    if (cJSON_IsObject(next)) {
        p->interval_end_utc = t + 3600;
        const cJSON *details = get(next, "details");
        if (field(details, units, "precipitation_amount", "mm", 0, 1000, &p->precipitation_mm)) p->valid |= PW_WEATHER_PRECIPITATION;
        if (field(details, units, "probability_of_precipitation", "%", 0, 100, &p->rain_probability_percent)) p->valid |= PW_WEATHER_RAIN_PROBABILITY;
        if (symbol(next, p->symbol, sizeof(p->symbol))) p->valid |= PW_WEATHER_SYMBOL;
    }
}
static void init_days(pw_weather_snapshot_t *out, int64_t now) {
    time_t t = (time_t)now;
    struct tm local;
    localtime_r(&t, &local);
    local.tm_hour = local.tm_min = local.tm_sec = 0;
    local.tm_isdst = -1;
    time_t start = mktime(&local);
    for (unsigned i = 0; i < PW_WEATHER_DAYS_MAX; ++i) {
        pw_weather_day_t *d = &out->days[i];
        localtime_r(&start, &local);
        d->year = local.tm_year + 1900;
        d->month = local.tm_mon + 1;
        d->day = local.tm_mday;
        d->start_utc = start;
        ++local.tm_mday;
        local.tm_isdst = -1;
        start = mktime(&local);
        d->end_utc = start;
    }
}
static void add_temperature(pw_weather_day_t *d, float value) {
    if (!d->temperature_valid) { d->temperature_min_c = d->temperature_max_c = value; d->temperature_valid = true; }
    else { if (value < d->temperature_min_c) d->temperature_min_c = value; if (value > d->temperature_max_c) d->temperature_max_c = value; }
}
static void add_day(const cJSON *data, const cJSON *units, const pw_weather_point_t *p,
                    pw_weather_snapshot_t *out, int64_t last_end[5], int64_t nearest[5]) {
    for (unsigned i = 0; i < PW_WEATHER_DAYS_MAX; ++i) {
        pw_weather_day_t *d = &out->days[i];
        if (p->time_utc < d->start_utc || p->time_utc >= d->end_utc) continue;
        if (p->valid & PW_WEATHER_TEMPERATURE) { add_temperature(d, p->temperature_c); ++d->temperature_sample_count; }
        const cJSON *period = get(data, "next_1_hours");
        unsigned seconds = 3600;
        if (!cJSON_IsObject(period)) { period = get(data, "next_6_hours"); seconds = 21600; }
        if (cJSON_IsObject(period)) {
            time_t instant = (time_t)p->time_utc;
            struct tm local;
            localtime_r(&instant, &local);
            int64_t noon_distance = llabs((int64_t)local.tm_hour*3600 + local.tm_min*60 - 12*3600);
            if (noon_distance < nearest[i] && symbol(period, d->symbol, sizeof(d->symbol))) nearest[i] = noon_distance;
            const cJSON *details = get(period, "details");
            if (p->time_utc + seconds <= d->end_utc && p->time_utc >= last_end[i]) {
                float amount;
                if (field(details, units, "precipitation_amount", "mm", 0, 3000, &amount)) {
                    d->precipitation_mm += amount;
                    d->precipitation_valid = true;
                    d->precipitation_covered_seconds += seconds;
                    last_end[i] = p->time_utc + seconds;
                }
                float min, max;
                if (field(details, units, "air_temperature_min", "celsius", -90, 65, &min) &&
                    field(details, units, "air_temperature_max", "celsius", -90, 65, &max) && min <= max) {
                    add_temperature(d, min); add_temperature(d, max);
                }
            }
        }
        d->precipitation_complete = d->precipitation_covered_seconds == (uint32_t)(d->end_utc-d->start_utc);
        if (i + 1 > out->day_count) out->day_count = (uint8_t)(i + 1);
        break;
    }
}
static bool fail(pw_weather_snapshot_t *out, char *error, size_t size, const char *message) {
    memset(out, 0, sizeof(*out));
    out->status = PW_WEATHER_ERROR;
    snprintf(out->error, sizeof(out->error), "%s", message);
    if (error && size) snprintf(error, size, "%s", message);
    return false;
}
bool pw_weather_parse_met(const char *json, size_t length, int64_t now, uint32_t generation,
                          pw_weather_snapshot_t *out, char *error, size_t error_size) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (error && error_size) *error = 0;
    if (!json || !length || length > PW_WEATHER_BODY_MAX || now < 1704067200)
        return fail(out, error, error_size, "invalid_body_or_clock");
    const char *end = value_end(json, json+length, 0);
    if (!end || white(end, json+length) != json+length) return fail(out,error,error_size,"invalid_json");
    json_view_t root={json,json+length}, props, meta, series;
    if (!member(root,"properties",&props) || !member(props,"meta",&meta) || !member(props,"timeseries",&series) || *series.start != '[')
        return fail(out,error,error_size,"missing_forecast");
    if (meta.end-meta.start > 8192) return fail(out,error,error_size,"oversized_metadata");
    cJSON *metadata = cJSON_ParseWithLength(meta.start, (size_t)(meta.end-meta.start));
    const cJSON *units = get(metadata,"units");
    if (!metadata || !cJSON_IsObject(units) || !pw_weather_parse_utc(str(get(metadata,"updated_at")), &out->source_updated_utc) ||
        out->source_updated_utc > now+300 || out->source_updated_utc < now-48*3600) {
        cJSON_Delete(metadata);
        return fail(out,error,error_size,"invalid_source_time_or_units");
    }
    init_days(out, now);
    int64_t day_last_end[5]={0}, nearest[5]={INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX,INT64_MAX};
    int64_t previous = 0;
    size_t count=0;
    const char *p=white(series.start+1,series.end);
    const char *failure=NULL;
    while (p < series.end && *p != ']') {
        end=value_end(p,series.end,0);
        if (!end || end-p > 8192 || ++count>384) { failure="oversized_timeseries"; break; }
        cJSON *item=cJSON_ParseWithLength(p,(size_t)(end-p));
        int64_t t=0;
        const cJSON *data=get(item,"data");
        if (!item || !pw_weather_parse_utc(str(get(item,"time")),&t) || t<=previous ||
            !cJSON_IsObject(get(data,"instant")) || t>now+12*86400) {
            cJSON_Delete(item); failure="invalid_forecast_sequence"; break;
        }
        previous=t;
        pw_weather_point_t point;
        read_point(data,units,t,&point);
        if (t<=now && now<t+3600 && (point.valid & PW_WEATHER_TEMPERATURE)) { out->current=point; out->current_valid=true; }
        if (t>=now-now%3600 && t<now-now%3600+48*3600 && out->hour_count<PW_WEATHER_HOURS_MAX)
            out->hours[out->hour_count++]=point;
        add_day(data,units,&point,out,day_last_end,nearest);
        cJSON_Delete(item);
        p=white(end,series.end);
        if (p<series.end && *p==',') p=white(p+1,series.end);
    }
    cJSON_Delete(metadata);
    if (failure) return fail(out,error,error_size,failure);
    if (!out->hour_count) return fail(out,error,error_size,"no_current_forecast_window");
    out->status=PW_WEATHER_READY;
    out->location_generation=generation;
    out->checked_utc=now;
    out->model_forecast=true;
    strcpy(out->source,"MET Norway");
    pw_weather_recompute_freshness(out,now);
    return true;
}
void pw_weather_recompute_freshness(pw_weather_snapshot_t *s, int64_t now) {
    if (!s || !s->checked_utc) return;
    s->current_valid=false;
    for (unsigned i=0;i<s->hour_count;++i) {
        const pw_weather_point_t *p=&s->hours[i];
        if (p->time_utc<=now && now<p->time_utc+3600 && (p->valid&PW_WEATHER_TEMPERATURE)) {
            s->current=*p; s->current_valid=true; break;
        }
    }
    if (now < s->checked_utc-300 || now-s->checked_utc>2*3600 || now-s->source_updated_utc>24*3600 || !s->current_valid) {
        if (s->status==PW_WEATHER_READY) s->status=PW_WEATHER_STALE;
    } else if (s->status==PW_WEATHER_STALE && !s->error[0]) s->status=PW_WEATHER_READY;
}
bool pw_weather_avatar_window(const pw_weather_snapshot_t *s, int64_t now, pw_weather_point_t out[4]) {
    if (!s || !out || !s->checked_utc || now<s->checked_utc || now-s->checked_utc>20*60 ||
        now-s->source_updated_utc>24*3600 || s->source_updated_utc>now+300) return false;
    // Anchor four complete future intervals at the VERIFIED fetch, not each render.
    int64_t start=((s->checked_utc+3599)/3600)*3600;
    const uint32_t required=PW_WEATHER_TEMPERATURE|PW_WEATHER_SYMBOL;
    unsigned found=0;
    for (unsigned i=0;i<s->hour_count && found<4;++i) {
        const pw_weather_point_t *p=&s->hours[i];
        if (p->time_utc<start) continue;
        if (p->time_utc!=start+(int64_t)found*3600 || p->interval_end_utc!=p->time_utc+3600 || (p->valid&required)!=required) return false;
        out[found++]=*p;
    }
    return found==4;
}
const char *pw_weather_status_name(pw_weather_status_t status) {
    static const char *names[]={"unconfigured","waiting_network","waiting_clock","fetching","ready","stale","error","suspended"};
    return (unsigned)status<sizeof(names)/sizeof(names[0]) ? names[status] : "error";
}

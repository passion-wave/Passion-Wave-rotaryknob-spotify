// SPDX-License-Identifier: MIT
#define _POSIX_C_SOURCE 200809L
#include "pw_weather_model.h"
#include "pw_weather_inflate.h"
#include "cJSON.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static int64_t stamp(const char *s) { int64_t t=0; CHECK(pw_weather_parse_utc(s,&t)); return t; }
static cJSON *get(cJSON *x,const char *s) { return cJSON_GetObjectItemCaseSensitive(x,s); }
static cJSON *points(cJSON *j) { return get(get(j,"properties"),"timeseries"); }
static bool parse(cJSON *j,int64_t now,pw_weather_snapshot_t *out) {
    char *s=cJSON_PrintUnformatted(j),err[100];
    bool result=pw_weather_parse_met(s,strlen(s),now,42,out,err,sizeof(err));
    free(s); return result;
}
static cJSON *point(cJSON *j,unsigned i) { return cJSON_GetArrayItem(points(j),(int)i); }
static cJSON *instant(cJSON *j,unsigned i) { return get(get(get(point(j,i),"data"),"instant"),"details"); }
static cJSON *units(cJSON *j) { return get(get(get(j,"properties"),"meta"),"units"); }
static void set_time(cJSON *j,unsigned i,const char *s) { cJSON_ReplaceItemInObjectCaseSensitive(point(j,i),"time",cJSON_CreateString(s)); }
static cJSON *fixture(int64_t start,unsigned count) {
    cJSON *j=cJSON_Parse("{\"properties\":{\"meta\":{\"updated_at\":\"2026-01-01T00:00:00Z\",\"units\":{\"air_temperature\":\"celsius\",\"wind_speed\":\"m/s\",\"precipitation_amount\":\"mm\"}},\"timeseries\":[]}}");
    time_t t=(time_t)start; char iso[32];
    struct tm utc; gmtime_r(&t,&utc); strftime(iso,sizeof(iso),"%Y-%m-%dT%H:%M:%SZ",&utc);
    cJSON_ReplaceItemInObjectCaseSensitive(get(get(j,"properties"),"meta"),"updated_at",cJSON_CreateString(iso));
    for(unsigned i=0;i<count;++i) {
        cJSON *p=cJSON_Parse("{\"time\":\"\",\"data\":{\"instant\":{\"details\":{\"air_temperature\":0,\"wind_speed\":2}},\"next_1_hours\":{\"summary\":{\"symbol_code\":\"rain\"},\"details\":{\"precipitation_amount\":1}}}}");
        t=(time_t)(start+(int64_t)i*3600); gmtime_r(&t,&utc); strftime(iso,sizeof(iso),"%Y-%m-%dT%H:%M:%SZ",&utc);
        cJSON_ReplaceItemInObjectCaseSensitive(p,"time",cJSON_CreateString(iso));
        cJSON_AddItemToArray(points(j),p);
    }
    return j;
}
static void date_and_dst_tests(void) {
    int64_t t;
    CHECK(pw_weather_parse_http_date("Wed, 30 Sep 2026 19:03:21 GMT")==stamp("2026-09-30T19:03:21Z"));
    CHECK(pw_weather_parse_http_date("Wed, 30 Sep 2026 19:03:21 CET")==0);
    CHECK(pw_weather_parse_http_date("Wed, 31 Feb 2026 19:03:21 GMT")==0);
    CHECK(pw_weather_parse_http_date("Wed, 30 Sep 2026 19:03:21 GMT garbage")==0);
    CHECK(pw_weather_parse_http_date("Wed, 30 xxx 2026 19:03:21 GMT")==0);
    CHECK(!pw_weather_parse_utc("2026-02-29T00:00:00Z",&t));
    CHECK(pw_weather_parse_utc("2024-02-29T00:00:00Z",&t));
    CHECK(!pw_weather_parse_utc("2026-03-29T24:00:00Z",&t));
    CHECK(!pw_weather_parse_utc("2026-03-29T00:00:00+01:00",&t));
    setenv("TZ","CET-1CEST,M3.5.0/2,M10.5.0/3",1); tzset();
    const char *dates[]={"2026-03-28T23:00:00Z","2026-10-24T22:00:00Z"};
    const int hours[]={23,25};
    for(unsigned i=0;i<2;++i) {
        int64_t now=stamp(dates[i]); cJSON *j=fixture(now,48); pw_weather_snapshot_t out;
        CHECK(parse(j,now,&out));
        CHECK(out.days[0].end_utc-out.days[0].start_utc==hours[i]*3600);
        CHECK(out.days[0].precipitation_complete);
        CHECK(out.days[0].precipitation_mm==(float)hours[i]);
        CHECK(out.days[0].temperature_valid && out.days[0].temperature_min_c==0);
        cJSON_Delete(j);
    }
}
static void normalization_tests(void) {
    int64_t now=stamp("2026-09-30T18:00:00Z");
    cJSON *j=fixture(now,72); pw_weather_snapshot_t out; pw_weather_point_t window[4];
    CHECK(parse(j,now,&out));
    CHECK(out.location_generation==42 && out.hour_count==48);
    CHECK(out.current_valid && out.current.temperature_c==0);
    CHECK(out.hours[0].interval_end_utc==now+3600);
    CHECK(!(out.hours[0].valid&PW_WEATHER_RAIN_PROBABILITY));
    CHECK(pw_weather_avatar_window(&out,now,window));
    CHECK(pw_weather_avatar_window(&out,now+1200,window));
    CHECK(!pw_weather_avatar_window(&out,now+1201,window));
    CHECK(window[0].time_utc==now);
    pw_weather_recompute_freshness(&out,now+7201); CHECK(out.status==PW_WEATHER_STALE);
    cJSON_ReplaceItemInObjectCaseSensitive(instant(j,0),"air_temperature",cJSON_CreateNull());
    CHECK(parse(j,now,&out)); CHECK(!(out.hours[0].valid&PW_WEATHER_TEMPERATURE)); CHECK(!out.current_valid);
    cJSON_ReplaceItemInObjectCaseSensitive(instant(j,0),"air_temperature",cJSON_CreateNumber(-5));
    cJSON_ReplaceItemInObjectCaseSensitive(units(j),"air_temperature",cJSON_CreateString("kelvin"));
    CHECK(parse(j,now,&out)); CHECK(!(out.hours[0].valid&PW_WEATHER_TEMPERATURE));
    cJSON_ReplaceItemInObjectCaseSensitive(units(j),"air_temperature",cJSON_CreateString("celsius"));
    cJSON_DeleteItemFromObjectCaseSensitive(get(point(j,1),"data"),"next_1_hours");
    CHECK(parse(j,now,&out)); CHECK(out.hours[1].interval_end_utc==0); CHECK(!pw_weather_avatar_window(&out,now,window));
    set_time(j,1,"2026-09-30T18:00:00Z"); CHECK(!parse(j,now,&out)); CHECK(out.hour_count==0);
    set_time(j,1,"2026-09-30T17:00:00Z"); CHECK(!parse(j,now,&out));
    set_time(j,1,"2026-09-30T19:00:00Z");
    cJSON_ReplaceItemInObjectCaseSensitive(get(get(j,"properties"),"meta"),"updated_at",cJSON_CreateString("2026-09-30T18:06:00Z")); CHECK(!parse(j,now,&out));
    cJSON_ReplaceItemInObjectCaseSensitive(get(get(j,"properties"),"meta"),"updated_at",cJSON_CreateString("2026-09-27T18:00:00Z")); CHECK(!parse(j,now,&out));
    cJSON_Delete(j);
    char error[100];
    CHECK(!pw_weather_parse_met("{} junk",7,now,1,&out,error,sizeof(error)));
    CHECK(!pw_weather_parse_met("{}",PW_WEATHER_BODY_MAX+1,now,1,&out,error,sizeof(error)));
    CHECK(!pw_weather_parse_met("{\"properties\": {\"x\": [true,]}}",30,now,1,&out,error,sizeof(error)));
    CHECK(!pw_weather_parse_met("{\"x\": 01}",9,now,1,&out,error,sizeof(error)));
    CHECK(!pw_weather_parse_met("{}",2,0,1,&out,error,sizeof(error)));
}
static void live_fixture_tests(const char *path) {
    FILE *file=fopen(path,"rb"); CHECK(file!=NULL);
    CHECK(fseek(file,0,SEEK_END)==0); long size=ftell(file); rewind(file);
    char *body=malloc((size_t)size+1); CHECK(body!=NULL);
    CHECK(fread(body,1,(size_t)size,file)==(size_t)size); fclose(file); body[size]=0;
    pw_weather_snapshot_t out; char error[100]; int64_t now=stamp("2026-09-30T18:45:00Z");
    CHECK(pw_weather_parse_met(body,(size_t)size,now,9,&out,error,sizeof(error)));
    CHECK(out.current_valid && fabs(out.current.temperature_c-18.3)<0.01);
    CHECK(out.current.valid&PW_WEATHER_APPARENT_TEMPERATURE);
    CHECK(!(out.current.valid&PW_WEATHER_GUST));
    CHECK(!(out.current.valid&PW_WEATHER_RAIN_PROBABILITY));
    CHECK(out.hour_count==48 && out.day_count==5 && out.model_forecast);
    CHECK(out.checked_utc==now && out.source_updated_utc==stamp("2026-09-30T17:18:53Z"));
    CHECK(!out.days[0].precipitation_complete);
    free(body);
}
static void avatar_tests(void) {
    int64_t now=stamp("2026-09-30T18:00:00Z"); cJSON *j=fixture(now,6);
    pw_weather_snapshot_t out; pw_weather_avatar_t avatar;
    CHECK(parse(j,now,&out));
    pw_weather_avatar_resolve(&out,now,false,&avatar);
    CHECK(avatar.valid && avatar.wet && avatar.partial && avatar.asset_index==28);
    pw_weather_avatar_resolve(&out,now,true,&avatar); CHECK(avatar.asset_index==34);
    pw_weather_avatar_resolve(&out,now+1201,true,&avatar); CHECK(!avatar.valid && avatar.asset_index==49);
    for(unsigned i=0;i<out.hour_count;++i) {
        out.hours[i].temperature_c=28; out.hours[i].precipitation_mm=0;
        strcpy(out.hours[i].symbol,"clearsky_day");
    }
    pw_weather_avatar_resolve(&out,now,false,&avatar); CHECK(avatar.valid && avatar.sun_hat && avatar.asset_index==50);
    for(unsigned i=0;i<out.hour_count;++i) {
        out.hours[i].temperature_c=-0.1f; out.hours[i].precipitation_mm=1;
        strcpy(out.hours[i].symbol,"snow");
    }
    pw_weather_avatar_resolve(&out,now,false,&avatar); CHECK(avatar.valid && avatar.snow && !avatar.wet && avatar.asset_index==41);
    for(unsigned i=0;i<out.hour_count;++i) { strcpy(out.hours[i].symbol,"snowandthunder"); }
    pw_weather_avatar_resolve(&out,now,false,&avatar); CHECK(avatar.snow && avatar.lightning && !avatar.wet);
    for(unsigned i=0;i<out.hour_count;++i) { strcpy(out.hours[i].symbol,"unknowncondition"); }
    pw_weather_avatar_resolve(&out,now,false,&avatar); CHECK(!avatar.valid && avatar.asset_index==48);
    cJSON_Delete(j);
}
static void compression_tests(void) {
    const unsigned char input[]="{\"weather\":\"rain\",\"temp\":0}";
    unsigned char compressed[256],output[512]; size_t written=0;
    for(unsigned i=0;i<2;++i) {
        z_stream s={0}; CHECK(deflateInit2(&s,6,Z_DEFLATED,i?15:31,8,Z_DEFAULT_STRATEGY)==Z_OK);
        s.next_in=(unsigned char *)input; s.avail_in=sizeof(input)-1; s.next_out=compressed; s.avail_out=sizeof(compressed);
        CHECK(deflate(&s,Z_FINISH)==Z_STREAM_END); size_t count=s.total_out; deflateEnd(&s);
        CHECK(pw_weather_decode_body(compressed,count,i?"deflate":"gzip",output,sizeof(output),&written));
        CHECK(written==sizeof(input)-1 && !memcmp(input,output,written));
        CHECK(!pw_weather_decode_body(compressed,count,i?"deflate":"gzip",output,4,&written));
        CHECK(!pw_weather_decode_body(compressed,count-1,i?"deflate":"gzip",output,sizeof(output),&written));
        compressed[count-6]^=1;
        CHECK(!pw_weather_decode_body(compressed,count,i?"deflate":"gzip",output,sizeof(output),&written));
    }
    CHECK(pw_weather_decode_body(input,sizeof(input)-1,"identity",output,sizeof(output),&written));
    CHECK(!pw_weather_decode_body(input,sizeof(input)-1,"br",output,sizeof(output),&written));
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    date_and_dst_tests(); normalization_tests(); live_fixture_tests(argv[1]); compression_tests(); avatar_tests();
    printf("weather native: %u assertions passed; snapshot %zu bytes\n",checks,sizeof(pw_weather_snapshot_t));
    return 0;
}

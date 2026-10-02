// SPDX-License-Identifier: MIT
#include "pw_weather.h"
#include "pw_weather_inflate.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define USER_AGENT "PassionWaveRotaryKnob/0.1 (https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify)"
#define WIRE_MAX (128u*1024u)
#define NETWORK_TIMEOUT_MS 12000
#define TOTAL_TIMEOUT_US (35LL*1000000)

static SemaphoreHandle_t mutex;
static TaskHandle_t worker;
static struct {
    bool enabled,suspended;
    int32_t latitude_e4,longitude_e4;
    uint32_t generation,serial;
    unsigned failures;
    int64_t next_request_utc;
    char last_modified[80];
    pw_weather_snapshot_t snapshot;
} state;

typedef struct {
    uint32_t serial;
    uint8_t *body;
    size_t size;
    char last_modified[80],expires[80],retry_after[80],encoding[24],content_type[96],location[512];
    bool overflow,cancelled;
    int64_t started_us;
} response_t;

static bool cancelled(uint32_t serial) {
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool cancel=state.serial!=serial || !state.enabled || state.suspended;
    xSemaphoreGive(mutex);
    return cancel;
}
static void header_copy(char *out,size_t capacity,const char *value) {
    if(!value) return;
    size_t n=strlen(value);
    if(n>=capacity || strchr(value,'\r') || strchr(value,'\n')) { out[0]=0; return; }
    memcpy(out,value,n+1);
}
static esp_err_t http_event(esp_http_client_event_t *event) {
    response_t *r=event->user_data;
    if(event->event_id==HTTP_EVENT_ON_HEADER) {
#define HEADER(key,target) if(!strcasecmp(event->header_key,key)) header_copy(r->target,sizeof(r->target),event->header_value)
        HEADER("Last-Modified",last_modified);
        else HEADER("Expires",expires);
        else HEADER("Retry-After",retry_after);
        else HEADER("Content-Encoding",encoding);
        else HEADER("Content-Type",content_type);
        else HEADER("Location",location);
#undef HEADER
    }
    if(cancelled(r->serial) || esp_timer_get_time()-r->started_us>TOTAL_TIMEOUT_US) {
        r->cancelled=true; return ESP_FAIL;
    }
    return ESP_OK;
}
static int64_t retry_date(const char *s,int64_t now) {
    if(!s || !*s) return 0;
    char *end;
    long seconds=strtol(s,&end,10);
    if(end!=s && !*end && seconds>=0 && seconds<=7*86400) return now+seconds;
    return pw_weather_parse_http_date(s);
}
static bool network_ready(void) {
    esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip={0};
    return sta && esp_netif_is_netif_up(sta) && esp_netif_get_ip_info(sta,&ip)==ESP_OK && ip.ip.addr!=0;
}
static bool valid_redirect(const char *location,char out[512]) {
    if(!location || !*location || strchr(location,'\r') || strchr(location,'\n') || strchr(location,'\\')) return false;
    if(!strncmp(location,"https://api.met.no/",18) && strlen(location)<512) { strcpy(out,location); return true; }
    if(location[0]=='/' && location[1]!='/' && strlen(location)<480) {
        memcpy(out,"https://api.met.no",17);
        memcpy(out+17,location,strlen(location)+1); return true;
    }
    return false; // Do not follow TLS downgrade or unqualified origin changes.
}
static bool perform_fetch(const char *url,const char *last_modified,response_t *response,int *status,char *error,size_t error_size) {
    char current_url[512];
    snprintf(current_url,sizeof(current_url),"%s",url);
    for(unsigned redirects=0;redirects<4;++redirects) {
        response->size=0;
        response->last_modified[0]=response->expires[0]=response->retry_after[0]=response->encoding[0]=response->content_type[0]=response->location[0]=0;
        esp_http_client_config_t config={
            .url=current_url,.method=HTTP_METHOD_GET,.timeout_ms=NETWORK_TIMEOUT_MS,
            .crt_bundle_attach=esp_crt_bundle_attach,.user_agent=USER_AGENT,
            .disable_auto_redirect=true,.event_handler=http_event,.user_data=response,
            .buffer_size=2048,.buffer_size_tx=1024,
        };
        esp_http_client_handle_t client=esp_http_client_init(&config);
        if(!client) { snprintf(error,error_size,"http_allocation_failed"); return false; }
        esp_http_client_set_header(client,"Accept","application/json");
        esp_http_client_set_header(client,"Accept-Encoding","gzip, deflate");
        if(*last_modified) esp_http_client_set_header(client,"If-Modified-Since",last_modified);
        esp_err_t result=esp_http_client_open(client,0);
        int64_t content_length=result==ESP_OK ? esp_http_client_fetch_headers(client) : -1;
        *status=esp_http_client_get_status_code(client);
        if(result!=ESP_OK || content_length<0 || response->cancelled) {
            esp_http_client_cleanup(client);
            snprintf(error,error_size,response->cancelled?"request_cancelled":"https_request_failed"); return false;
        }
        if(*status==301 || *status==302 || *status==303 || *status==307 || *status==308) {
            bool valid=valid_redirect(response->location,current_url);
            esp_http_client_cleanup(client);
            if(!valid) { snprintf(error,error_size,"redirect_origin_rejected"); return false; }
            continue;
        }
        if(*status==304) { esp_http_client_cleanup(client); return true; }
        if(*status!=200 && *status!=203) {
            snprintf(error,error_size,"provider_http_%d",*status);
            esp_http_client_cleanup(client); return false;
        }
        if(strncasecmp(response->content_type,"application/json",16) || (response->content_type[16] && response->content_type[16]!=';' && response->content_type[16]!=' ')) {
            snprintf(error,error_size,"invalid_content_type"); esp_http_client_cleanup(client); return false;
        }
        if(content_length>WIRE_MAX) { snprintf(error,error_size,"compressed_body_too_large"); esp_http_client_cleanup(client); return false; }
        uint8_t chunk[2048];
        bool ok=true;
        while(!esp_http_client_is_complete_data_received(client)) {
            if(cancelled(response->serial) || esp_timer_get_time()-response->started_us>TOTAL_TIMEOUT_US) { response->cancelled=true; ok=false; break; }
            int n=esp_http_client_read(client,(char *)chunk,sizeof(chunk));
            if(n<0) { ok=false; break; }
            if(!n) { ok=esp_http_client_is_complete_data_received(client); break; }
            if(response->size+(size_t)n>WIRE_MAX) { response->overflow=true; ok=false; break; }
            memcpy(response->body+response->size,chunk,(size_t)n);
            response->size+=(size_t)n;
        }
        esp_http_client_cleanup(client);
        if(!ok) snprintf(error,error_size,response->overflow?"compressed_body_too_large":response->cancelled?"request_cancelled":"truncated_body");
        return ok;
    }
    snprintf(error,error_size,"redirect_limit"); return false;
}
static void fetch_once(uint32_t serial,int32_t lat,int32_t lon,char modified[80]) {
    response_t response={.serial=serial,.started_us=esp_timer_get_time()};
    // Large, temporary allocations are explicitly in PSRAM. No global cJSON hooks.
    response.body=heap_caps_malloc(WIRE_MAX,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *decoded=heap_caps_malloc(PW_WEATHER_BODY_MAX+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    pw_weather_snapshot_t *fresh=heap_caps_calloc(1,sizeof(*fresh),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    int status=0;
    char error[96]={0};
    bool success=false;
    if(!response.body || !decoded || !fresh) snprintf(error,sizeof(error),"weather_memory_unavailable");
    else {
        char url[160];
        snprintf(url,sizeof(url),"https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=%.4f&lon=%.4f",lat/10000.0,lon/10000.0);
        success=perform_fetch(url,modified,&response,&status,error,sizeof(error));
        if(success && status!=304) {
            size_t written=0;
            if(!pw_weather_decode_body(response.body,response.size,response.encoding,decoded,PW_WEATHER_BODY_MAX,&written)) {
                snprintf(error,sizeof(error),"invalid_or_oversized_compression"); success=false;
            } else {
                decoded[written]=0;
                // Generation is assigned at the atomic commit only if serial matches.
                success=pw_weather_parse_met((char *)decoded,written,time(NULL),0,fresh,error,sizeof(error));
            }
        }
    }
    int64_t now=time(NULL);
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(state.serial==serial && state.enabled && !state.suspended) {
        if(success && status==304 && (!state.snapshot.checked_utc || !*modified)) {
            success=false; snprintf(error,sizeof(error),"unexpected_not_modified");
        }
        if(success) {
            if(status!=304) {
                fresh->location_generation=state.generation;
                fresh->deprecated_source=status==203;
                state.snapshot=*fresh;
                header_copy(state.last_modified,sizeof(state.last_modified),response.last_modified);
            } else {
                state.snapshot.checked_utc=now;
                state.snapshot.status=PW_WEATHER_READY;
                state.snapshot.error[0]=0;
                if(*response.last_modified) header_copy(state.last_modified,sizeof(state.last_modified),response.last_modified);
            }
            state.failures=0;
            int64_t earliest=now+15*60;
            int64_t expires=pw_weather_parse_http_date(response.expires);
            if(!expires) expires=now+3600;
            state.next_request_utc=(expires>earliest?expires:earliest)+(esp_random()%121);
            pw_weather_recompute_freshness(&state.snapshot,now);
            // A matching 304 may validate a forecast that no longer covers now.
            // Fetch a full representation at the next permitted request, not now.
            if(!state.snapshot.current_valid) state.last_modified[0]=0;
        } else {
            if(state.failures<12) ++state.failures;
            unsigned exponent=state.failures>8?8:state.failures;
            int64_t delay=30*((int64_t)1<<exponent);
            if(delay>6*3600) delay=6*3600;
            if((status==429 || status==403) && delay<3600) delay=3600;
            state.next_request_utc=now+delay+(esp_random()%61);
            int64_t retry=retry_date(response.retry_after,now);
            int64_t expires=pw_weather_parse_http_date(response.expires);
            if(retry>state.next_request_utc) state.next_request_utc=retry;
            if(expires>state.next_request_utc) state.next_request_utc=expires;
            state.snapshot.status=state.snapshot.checked_utc?PW_WEATHER_STALE:PW_WEATHER_ERROR;
            snprintf(state.snapshot.error,sizeof(state.snapshot.error),"%s",error);
        }
        state.snapshot.next_request_utc=state.next_request_utc;
    }
    xSemaphoreGive(mutex);
    free(fresh); free(decoded); free(response.body);
}
static void weather_task(void *unused) {
    (void)unused;
    for(;;) {
        ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(10000));
        int64_t now=time(NULL);
        bool connected=network_ready();
        xSemaphoreTake(mutex,portMAX_DELAY);
        if(!state.enabled || state.suspended) { xSemaphoreGive(mutex); continue; }
        if(now<1704067200 || !connected) {
            state.snapshot.status=now<1704067200?PW_WEATHER_WAITING_CLOCK:PW_WEATHER_WAITING_NETWORK;
            xSemaphoreGive(mutex); continue;
        }
        if(!state.next_request_utc) state.next_request_utc=now+esp_random()%61;
        if(now<state.next_request_utc) {
            if(state.snapshot.checked_utc && (state.snapshot.status==PW_WEATHER_WAITING_NETWORK || state.snapshot.status==PW_WEATHER_WAITING_CLOCK || state.snapshot.status==PW_WEATHER_SUSPENDED)) state.snapshot.status=PW_WEATHER_READY;
            pw_weather_recompute_freshness(&state.snapshot,now);
            xSemaphoreGive(mutex); continue;
        }
        uint32_t serial=state.serial;
        int32_t lat=state.latitude_e4,lon=state.longitude_e4;
        char modified[80]; memcpy(modified,state.last_modified,sizeof(modified));
        state.snapshot.status=PW_WEATHER_FETCHING;
        state.next_request_utc=now+15*60; // Preserve minimum spacing if OTA cancels this job.
        state.snapshot.next_request_utc=state.next_request_utc;
        xSemaphoreGive(mutex);
        fetch_once(serial,lat,lon,modified);
    }
}
esp_err_t pw_weather_init(void) {
    if(mutex) return ESP_ERR_INVALID_STATE;
    mutex=xSemaphoreCreateMutex();
    if(!mutex) return ESP_ERR_NO_MEM;
    memset(&state,0,sizeof(state));
    state.snapshot.status=PW_WEATHER_UNCONFIGURED;
    if(xTaskCreate(weather_task,"pw_weather",8192,NULL,2,&worker)!=pdPASS) {
        vSemaphoreDelete(mutex); mutex=NULL; return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t pw_weather_configure(bool enabled,double latitude,double longitude,uint32_t generation) {
    if(!mutex) return ESP_ERR_INVALID_STATE;
    if(enabled && (!isfinite(latitude) || !isfinite(longitude) || latitude<-90 || latitude>90 || longitude<-180 || longitude>180)) return ESP_ERR_INVALID_ARG;
    int32_t lat=enabled?(int32_t)(latitude*10000.0):0,lon=enabled?(int32_t)(longitude*10000.0):0;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(state.enabled!=enabled || state.latitude_e4!=lat || state.longitude_e4!=lon || state.generation!=generation) {
        ++state.serial;
        state.enabled=enabled; state.latitude_e4=lat; state.longitude_e4=lon; state.generation=generation;
        state.failures=0;
        state.next_request_utc=0;
        state.last_modified[0]=0;
        memset(&state.snapshot,0,sizeof(state.snapshot));
        state.snapshot.location_generation=generation;
        state.snapshot.status=enabled?PW_WEATHER_WAITING_NETWORK:PW_WEATHER_UNCONFIGURED;
        // Spread starts after synchronized power recovery. No request before this time.
        if(enabled && time(NULL)>=1704067200) state.next_request_utc=time(NULL)+esp_random()%61;
        state.snapshot.next_request_utc=state.next_request_utc;
    }
    xSemaphoreGive(mutex);
    xTaskNotifyGive(worker);
    return ESP_OK;
}
void pw_weather_request_refresh(void) { if(worker) xTaskNotifyGive(worker); }
void pw_weather_set_suspended(bool suspended) {
    if(!mutex) return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(state.suspended!=suspended) {
        state.suspended=suspended; ++state.serial;
        if(suspended && state.enabled) state.snapshot.status=PW_WEATHER_SUSPENDED;
        else if(state.enabled) state.snapshot.status=state.snapshot.checked_utc?PW_WEATHER_READY:PW_WEATHER_WAITING_NETWORK;
    }
    xSemaphoreGive(mutex);
    if(worker) xTaskNotifyGive(worker);
}
void pw_weather_get_snapshot(pw_weather_snapshot_t *out) {
    if(!out) return;
    if(!mutex) { memset(out,0,sizeof(*out)); out->status=PW_WEATHER_UNCONFIGURED; return; }
    xSemaphoreTake(mutex,portMAX_DELAY);
    *out=state.snapshot;
    xSemaphoreGive(mutex);
    pw_weather_recompute_freshness(out,time(NULL));
}

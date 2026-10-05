#include "pw_lan_gate.h"
#include <assert.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define CONFIG_PW_LAN_HTTP_LAB 1
#define ESP_OK 0
#define SETUP_SECONDS 600
#define HTTPD_RESP_USE_STRLEN (-1)
typedef struct { const char *host,*origin,*cookie,*csrf; } httpd_req_t;
typedef struct { struct { uint32_t addr; } ip,netmask; } esp_netif_ip_info_t;
static int station_if;
static struct { bool setup_open; } view;
static int64_t setup_until, session_until;
static char session[65],csrf[65],lan_session[65]="fixture",lan_csrf[65]="fixture-csrf";
static pw_lan_gate_t lan_gate;
static uint32_t peer,local;
static void take(void) {} static void give(void) {}
static int64_t esp_timer_get_time(void) { return 1000; }
static bool same_secret(const char *a,const char *b) { return !strcmp(a,b); }
static int httpd_req_to_sockfd(httpd_req_t *r) { (void)r;return 1; }
static bool pw_http_socket_ipv4(int fd,bool remote,struct in_addr *ip) { (void)fd;ip->s_addr=remote?peer:local;return true; }
static int esp_netif_get_ip_info(int netif,esp_netif_ip_info_t *ip) { (void)netif;ip->ip.addr=inet_addr("192.168.2.122");ip->netmask.addr=inet_addr("255.255.255.0");return ESP_OK; }
static int httpd_req_get_hdr_value_str(httpd_req_t *r,const char *key,char *out,size_t size) {
 const char *s=!strcmp(key,"Host")?r->host:!strcmp(key,"Origin")?r->origin:!strcmp(key,"Cookie")?r->cookie:!strcmp(key,"X-CSRF-Token")?r->csrf:NULL;
 if(!s||strlen(s)>=size) return -1;
 snprintf(out,size,"%s",s);
 return ESP_OK;
}
static bool host_valid(httpd_req_t *r) { return r->host && !strcmp(r->host,"192.168.2.122"); }
static bool is_ap_request(httpd_req_t *r) { (void)r;return false; }
#include "auth.inc"
int main(void) {
 httpd_req_t r={"192.168.2.122","http://192.168.2.122","pw_lan=fixture","fixture-csrf"};
 local=inet_addr("192.168.2.122");peer=inet_addr("192.168.2.50");
 pw_lan_gate_open(&lan_gate,1,123456);assert(pw_lan_gate_pair(&lan_gate,2,peer,"123456"));
 assert(authorized(&r,false));assert(authorized(&r,true));
 r.origin="http://evil.invalid";assert(!authorized(&r,true));r.origin="http://192.168.2.122";
 r.csrf="wrong";assert(!authorized(&r,true));r.csrf="fixture-csrf";
 r.host="evil.invalid";assert(!authorized(&r,false));r.host="192.168.2.122";
 r.cookie="pw_lan=wrong";assert(!authorized(&r,false));r.cookie="pw_lan=fixture";
 peer=inet_addr("192.168.2.51");assert(!authorized(&r,false));
 peer=inet_addr("192.168.2.50");local=inet_addr("192.168.4.1");assert(!authorized(&r,false));
 local=inet_addr("192.168.2.122");peer=inet_addr("10.0.0.1");assert(!authorized(&r,false));
 peer=inet_addr("192.168.2.50");pw_lan_gate_close(&lan_gate);assert(!authorized(&r,false));
 puts("Actual HTTP authorization: host, origin, CSRF, cookie, peer, subnet, local endpoint and revoked gate PASS");
}

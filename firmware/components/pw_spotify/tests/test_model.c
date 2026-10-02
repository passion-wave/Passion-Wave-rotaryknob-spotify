// SPDX-License-Identifier: MIT
#include "pw_spotify_model.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned assertions;
#define CHECK(x)                                                               \
  do {                                                                         \
    assertions++;                                                              \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #x);           \
      abort();                                                                 \
    }                                                                          \
  } while (0)
static cJSON *parse(const char *s) { return pw_spotify_json(s, strlen(s)); }
static pw_spotify_snapshot_t snapshot;
static const char *device_json =
    "{\"id\":\"speaker-1\",\"name\":\"Küche\",\"type\":\"Speaker\",\"is_active\":true,\"is_restricted\":false,\"supports_volume\":true,\"volume_percent\":42}";
static void *watched[16];
static size_t watched_len[16];
static unsigned watch_count, freed;
static void track(cJSON *j) {
  for (cJSON *a = j; a; a = a->next) {
    if (a->valuestring) {
      CHECK(watch_count < 16);
      watched[watch_count] = a->valuestring;
      watched_len[watch_count++] = strlen(a->valuestring);
    }
    track(a->child);
  }
}
static void checking_free(void *p) {
  for (unsigned i = 0; i < watch_count; i++)
    if (watched[i] == p) {
      for (size_t n = 0; n < watched_len[i]; n++)
        CHECK(((unsigned char *)p)[n] == 0);
      freed++;
    }
  free(p);
}
static void test_json(void) {
  const char *bad[] = {"",
                       "{",
                       "null {}",
                       "{\"a\":1,\"a\":2}",
                       "{\"a\":{\"x\":1,\"x\":2}}",
                       "{\"access_token\":\"safe\\u0000hidden\"}",
                       "[1,]",
                       "{\"a\":NaN}",
                       "{}garbage"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
    CHECK(!parse(bad[i]));
  char raw[] = {'{', '}', 0, 'x'};
  CHECK(!pw_spotify_json(raw, sizeof(raw)));
  char deep[100];
  memset(deep, '[', 25);
  memset(deep + 25, ']', 25);
  deep[50] = 0;
  CHECK(!parse(deep));
  cJSON *j = parse("{\"name\":\"literal\\\\u0000\"} \n");
  CHECK(j);
  cJSON_Delete(j);
  cJSON_Hooks hooks = {.malloc_fn = malloc, .free_fn = checking_free};
  cJSON_InitHooks(&hooks);
  j = parse(
      "{\"access_token\":\"one\",\"nested\":{\"x\":\"two\",\"y\":\"three\",\"a\":[\"four\",\"five\",{\"z\":\"six\"}]},\"last\":\"seven\"}");
  CHECK(j);
  track(j);
  CHECK(watch_count == 7);
  pw_spotify_secret_json_delete(j);
  CHECK(freed == 7);
  watch_count = 0;
  cJSON_InitHooks(NULL);
}
static void test_text_and_uri(void) {
  char out[6];
  CHECK(pw_spotify_text(out, sizeof(out), "ééé"));
  CHECK(!strcmp(out, "éé"));
  CHECK(pw_spotify_text(out, 2, "ä"));
  CHECK(!strcmp(out, ""));
  CHECK(pw_spotify_text(out, sizeof(out), "a\nb"));
  CHECK(!strcmp(out, "a b"));
  CHECK(!pw_spotify_text(out, sizeof(out), "\xC0\x80"));
  CHECK(!pw_spotify_text(out, sizeof(out), "\xED\xA0\x80"));
  CHECK(!pw_spotify_text(out, sizeof(out), "ok\xE2"));
  CHECK(!pw_spotify_text(out, sizeof(out), "\xF4\x90\x80\x80"));
  CHECK(pw_spotify_uri("spotify:playlist:37i9dQZF1DXcBWIGoYBM5M"));
  CHECK(pw_spotify_uri("spotify:track:4uLU6hMCjMI75M1A2tKUQC"));
  const char *bad[] = {
      "spotify:show:37i9dQZF1DXcBWIGoYBM5M",
      "spotify:episode:37i9dQZF1DXcBWIGoYBM5M",
      "https://open.spotify.com/playlist/37i9dQZF1DXcBWIGoYBM5M",
      "spotify:playlist:37i9dQZF1DXcBWIGoYBM5/",
      "spotify:track:a",
      "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M?x=1"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
    CHECK(!pw_spotify_uri(bad[i]));
  char encoded[40];
  CHECK(pw_spotify_encode("a&b?c/# +", encoded, sizeof(encoded)));
  CHECK(!strcmp(encoded, "a%26b%3Fc%2F%23%20%2B"));
  CHECK(!pw_spotify_encode("&", out, 3));
  CHECK(!out[0]);
  CHECK(!pw_spotify_ascii("abc\r\n", 20, false));
  CHECK(!pw_spotify_ascii("", 2, false));
  CHECK(!pw_spotify_ascii("123", 3, false));
}
static void test_devices(void) {
  char input[1600];
  snprintf(input, sizeof(input), "{\"devices\":[%s,{\"id\":null}]}",
           device_json);
  cJSON *j = parse(input);
  CHECK(j);
  CHECK(pw_spotify_parse_devices(j, &snapshot));
  cJSON_Delete(j);
  CHECK(snapshot.device_count == 1);
  CHECK(snapshot.devices[0].supports_volume);
  CHECK(snapshot.devices[0].volume == 42);
  CHECK(!strcmp(snapshot.devices[0].name, "Küche"));
  snprintf(input, sizeof(input), "{\"devices\":[%s,%s]}", device_json,
           device_json);
  j = parse(input);
  CHECK(j);
  CHECK(!pw_spotify_parse_devices(j, &snapshot));
  cJSON_Delete(j);
  const char *bad[] = {
      "{}", "{\"devices\":null}", "{\"devices\":[{}]}", "{\"devices\":[false]}",
      "{\"devices\":[{\"id\":\"x\",\"name\":\"a\",\"type\":\"b\",\"is_active\":true,\"is_restricted\":false,\"volume_percent\":101}]}"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    j = parse(bad[i]);
    CHECK(j);
    CHECK(!pw_spotify_parse_devices(j, &snapshot));
    cJSON_Delete(j);
  }
  j = parse(
      "{\"devices\":[{\"id\":\"x\",\"name\":\"a\",\"type\":\"b\",\"is_active\":false,\"is_restricted\":true,\"volume_percent\":null}]}");
  CHECK(pw_spotify_parse_devices(j, &snapshot));
  CHECK(snapshot.devices[0].restricted);
  CHECK(!snapshot.devices[0].supports_volume);
  CHECK(!snapshot.devices[0].volume_known);
  cJSON_Delete(j);
  j = cJSON_CreateObject();
  cJSON *list = cJSON_AddArrayToObject(j, "devices");
  for (unsigned i = 0; i < 18; i++) {
    cJSON *d = parse(device_json);
    char id[24];
    snprintf(id, sizeof(id), "speaker-%u", i);
    cJSON_ReplaceItemInObjectCaseSensitive(d, "id", cJSON_CreateString(id));
    cJSON_AddItemToArray(list, d);
  }
  CHECK(pw_spotify_parse_devices(j, &snapshot));
  CHECK(snapshot.device_count == 16);
  CHECK(snapshot.devices_truncated);
  cJSON_Delete(j);
}
static cJSON *named_device(const char *id, const char *name) {
  cJSON *d = parse(device_json);
  CHECK(d);
  CHECK(cJSON_ReplaceItemInObjectCaseSensitive(d, "id", cJSON_CreateString(id)));
  CHECK(cJSON_ReplaceItemInObjectCaseSensitive(d, "name", cJSON_CreateString(name)));
  return d;
}
static void test_device_names(void) {
  memset(&snapshot, 0, sizeof(snapshot));
  cJSON *j = cJSON_CreateObject();
  cJSON *list = cJSON_AddArrayToObject(j, "devices");
  cJSON *phone = named_device("phone-1", "iPhone");
  cJSON *unnamed = named_device("unnamed-2", "");
  cJSON_ReplaceItemInObjectCaseSensitive(phone, "is_active", cJSON_CreateFalse());
  cJSON_AddItemToArray(list, phone);
  cJSON_AddItemToArray(list, unnamed);
  CHECK(pw_spotify_parse_devices(j, &snapshot));
  CHECK(snapshot.device_count == 2 && !snapshot.devices_truncated);
  CHECK(!strcmp(snapshot.devices[0].name, "iPhone"));
  CHECK(!strcmp(snapshot.devices[0].id, "phone-1"));
  CHECK(!snapshot.devices[0].active);
  CHECK(!strcmp(snapshot.devices[1].name, "Ausgabe ohne Namen"));
  CHECK(!strcmp(snapshot.devices[1].id, "unnamed-2"));
  CHECK(snapshot.devices[1].active && !snapshot.devices[1].restricted);
  CHECK(snapshot.devices[1].supports_volume && snapshot.devices[1].volume_known &&
        snapshot.devices[1].volume == 42);
  snapshot.enabled = snapshot.linked = snapshot.connected = true;
  snapshot.state = PW_SPOTIFY_READY;
  strcpy(snapshot.selected_device_id, "unnamed-2");
  pw_spotify_disallows_t dis = {0};
  pw_spotify_capabilities(&snapshot, &dis, true);
  CHECK(snapshot.selected_present && !snapshot.selected_restricted);
  CHECK(!strcmp(snapshot.selected_device_name, "Ausgabe ohne Namen"));
  CHECK(!strcmp(snapshot.selected_device_id, "unnamed-2"));
  CHECK(snapshot.can_play && snapshot.can_volume && snapshot.selected_volume == 42);
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "is_restricted", cJSON_CreateTrue());
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "supports_volume", cJSON_CreateFalse());
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "volume_percent", cJSON_CreateNull());
  CHECK(pw_spotify_parse_devices(j, &snapshot));
  pw_spotify_capabilities(&snapshot, &dis, true);
  CHECK(snapshot.selected_restricted && !snapshot.selected_supports_volume &&
        !snapshot.selected_volume_known);
  CHECK(!snapshot.can_play && !snapshot.can_pause && !snapshot.can_volume);

  const char *blank[] = {
      "", " \t\r\n\x7F\xC2\x85", "\xC2\xA0", "\xE1\x9A\x80",
      "\xE2\x80\x80\xE2\x80\x8A", /* en quad/hair space */
      "\xE2\x80\xA8\xE2\x80\xA9\xE2\x80\xAF", /* separators/narrow NBSP */
      "\xE2\x81\x9F\xE3\x80\x80", /* medium mathematical/ideographic space */
      "\xE2\x80\x8B\xE2\x80\x8C\xE2\x80\x8D", /* zero-width space/joiners */
      "\xD8\x9C\xE2\x80\x8E\xE2\x80\x8F", /* directional marks */
      "\xE2\x80\xAA\xE2\x80\xAE\xE2\x81\xA6\xE2\x81\xA9", /* bidi controls */
      "\xE2\x81\xA0\xEF\xBB\xBF", /* word joiner/BOM */
      "\xC2\xAD\xCD\x8F", /* soft hyphen/combining grapheme joiner */
      "\xE1\x85\x9F\xE1\x85\xA0\xE1\xA0\x8B\xE1\xA0\x8E\xE1\xA0\x8F",
      "\xEF\xB8\x80\xEF\xB8\x8F\xF3\xA0\x84\x80\xF3\xA0\x87\xAF", /* selectors */
      "\xF3\xA0\x80\x81\xF3\xA0\x80\xA0\xF3\xA0\x81\xBF" /* tags */};
  for (size_t i = 0; i < sizeof(blank) / sizeof(*blank); i++) {
    cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "name", cJSON_CreateString(blank[i]));
    CHECK(pw_spotify_parse_devices(j, &snapshot));
    CHECK(snapshot.device_count == 2);
    CHECK(!strcmp(snapshot.devices[1].name, "Ausgabe ohne Namen"));
    CHECK(!strcmp(snapshot.devices[1].id, "unnamed-2"));
    CHECK(snapshot.devices[1].active && snapshot.devices[1].restricted);
  }
  const char *visible[] = {"iPhone", "Küche", "東京", "🎵", "👩‍👩‍👧‍👦",
                           "  Küche \xE2\x81\xA0", "\xE2\x80\x8BiPhone", ""};
  for (size_t i = 0; i < sizeof(visible) / sizeof(*visible); i++) {
    cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "name", cJSON_CreateString(visible[i]));
    CHECK(pw_spotify_parse_devices(j, &snapshot));
    CHECK(!strcmp(snapshot.devices[1].name, visible[i]));
  }
  char truncated[160];
  memset(truncated, ' ', 128);
  strcpy(truncated + 128, "Küche");
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "name", cJSON_CreateString(truncated));
  CHECK(pw_spotify_parse_devices(j, &snapshot));
  CHECK(!strcmp(snapshot.devices[1].name, "Ausgabe ohne Namen"));
  strcpy(truncated + 128, "\xC0\x80");
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "name", cJSON_CreateString(truncated));
  CHECK(!pw_spotify_parse_devices(j, &snapshot));
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "name", cJSON_CreateString("\xED\xA0\x80"));
  CHECK(!pw_spotify_parse_devices(j, &snapshot));
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "name", cJSON_CreateNull());
  CHECK(!pw_spotify_parse_devices(j, &snapshot));
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "name", cJSON_CreateNumber(7));
  CHECK(!pw_spotify_parse_devices(j, &snapshot));
  cJSON_DeleteItemFromObjectCaseSensitive(unnamed, "name");
  CHECK(!pw_spotify_parse_devices(j, &snapshot));
  cJSON_Delete(j);

  /* Playback uses the same device parser and must retain addressing/permissions. */
  j = cJSON_CreateObject();
  unnamed = named_device("unnamed-2", "\xE2\x80\x8B");
  cJSON_AddItemToObject(j, "device", unnamed);
  cJSON_AddBoolToObject(j, "is_playing", true);
  CHECK(pw_spotify_parse_playback(j, &snapshot, &dis));
  CHECK(snapshot.playback_known && snapshot.playing);
  CHECK(!strcmp(snapshot.active_device_id, "unnamed-2"));
  CHECK(!strcmp(snapshot.active_device_name, "Ausgabe ohne Namen"));
  CHECK(snapshot.supports_volume && snapshot.volume_known && snapshot.volume == 42);
  CHECK(!dis.pause && !dis.resume && !dis.next && !dis.previous);
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "is_restricted", cJSON_CreateTrue());
  CHECK(pw_spotify_parse_playback(j, &snapshot, &dis));
  CHECK(dis.pause && dis.resume && dis.next && dis.previous);
  CHECK(!strcmp(snapshot.active_device_id, "unnamed-2"));
  CHECK(!strcmp(snapshot.active_device_name, "Ausgabe ohne Namen"));
  cJSON_ReplaceItemInObjectCaseSensitive(unnamed, "name", cJSON_CreateNull());
  CHECK(!pw_spotify_parse_playback(j, &snapshot, &dis));
  cJSON_Delete(j);
}
static void test_playback_and_commands(void) {
  char input[2048];
  snprintf(
      input, sizeof(input),
      "{\"device\":%s,\"is_playing\":true,\"progress_ms\":1234,\"currently_playing_type\":\"track\",\"context\":{\"uri\":\"spotify:playlist:37i9dQZF1DXcBWIGoYBM5M\"},\"item\":{\"name\":\"Titel\",\"uri\":\"spotify:track:4uLU6hMCjMI75M1A2tKUQC\",\"duration_ms\":200000,\"artists\":[{\"name\":\"Band\"}]},\"actions\":{\"disallows\":{\"skipping_next\":true}}}",
      device_json);
  memset(&snapshot, 0, sizeof(snapshot));
  pw_spotify_disallows_t dis = {0};
  cJSON *j = parse(input);
  CHECK(j);
  CHECK(pw_spotify_parse_playback(j, &snapshot, &dis));
  CHECK(snapshot.playback_known);
  CHECK(snapshot.playing);
  CHECK(snapshot.position_ms == 1234);
  CHECK(snapshot.duration_ms == 200000);
  CHECK(!strcmp(snapshot.title, "Titel"));
  CHECK(!strcmp(snapshot.artist, "Band"));
  CHECK(dis.next);
  CHECK(!dis.previous);
  cJSON_ReplaceItemInObjectCaseSensitive(j, "progress_ms",
                                         cJSON_CreateNumber(-1));
  CHECK(!pw_spotify_parse_playback(j, &snapshot, &dis));
  cJSON_Delete(j);
  j = parse(input);
  CHECK(pw_spotify_parse_playback(j, &snapshot, &dis));
  cJSON_Delete(j);
  snprintf(input, sizeof(input), "{\"devices\":[%s]}", device_json);
  j = parse(input);
  CHECK(pw_spotify_parse_devices(j, &snapshot));
  cJSON_Delete(j);
  snapshot.enabled = snapshot.linked = snapshot.connected = true;
  snapshot.state = PW_SPOTIFY_READY;
  snapshot.session = 9;
  snapshot.selection_generation = 3;
  strcpy(snapshot.selected_device_id, "speaker-1");
  pw_spotify_capabilities(&snapshot, &dis, true);
  CHECK(snapshot.can_pause);
  CHECK(!snapshot.can_next);
  CHECK(snapshot.can_previous);
  CHECK(snapshot.can_volume);
  CHECK(snapshot.selected_volume == 42);
  pw_spotify_command_t cmd = {.kind = PW_SPOTIFY_VOLUME,
                              .session = 9,
                              .selection_generation = 3,
                              .volume = 60};
  strcpy(cmd.device_id, "speaker-1");
  CHECK(pw_spotify_command_valid(&cmd, &snapshot));
  cmd.selection_generation = 2;
  CHECK(!pw_spotify_command_valid(&cmd, &snapshot));
  cmd.selection_generation = 3;
  cmd.session = 10;
  CHECK(!pw_spotify_command_valid(&cmd, &snapshot));
  cmd.session = 9;
  strcpy(cmd.device_id, "speaker-2");
  CHECK(!pw_spotify_command_valid(&cmd, &snapshot));
  strcpy(cmd.device_id, "speaker-1");
  cmd.volume = 101;
  CHECK(!pw_spotify_command_valid(&cmd, &snapshot));
  cmd.kind = PW_SPOTIFY_NEXT;
  CHECK(!pw_spotify_command_valid(&cmd, &snapshot));
  cmd.kind = PW_SPOTIFY_PREVIOUS;
  CHECK(pw_spotify_command_valid(&cmd, &snapshot));
  cmd.kind = PW_SPOTIFY_PLAY;
  strcpy(cmd.uri, "spotify:playlist:37i9dQZF1DXcBWIGoYBM5M");
  CHECK(pw_spotify_command_valid(&cmd, &snapshot));
  strcpy(cmd.uri, "spotify:episode:37i9dQZF1DXcBWIGoYBM5M");
  CHECK(!pw_spotify_command_valid(&cmd, &snapshot));
  pw_spotify_capabilities(&snapshot, &dis, false);
  CHECK(!snapshot.can_play && !snapshot.can_pause && !snapshot.can_volume);
  snapshot.state = PW_SPOTIFY_SUSPENDED;
  pw_spotify_capabilities(&snapshot, &dis, true);
  CHECK(!snapshot.can_play);
  pw_spotify_clear_playback(&snapshot);
  CHECK(!snapshot.playback_known && !snapshot.playing && !snapshot.title[0] &&
        !snapshot.active_device_id[0]);
}
static void test_tokens_and_storage(void) {
  const char *good =
      "{\"access_token\":\"accessABC\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"refresh_token\":\"refreshDEF\",\"scope\":\"user-read-playback-state user-modify-playback-state\"}";
  const int64_t now = 1790850000;
  pw_spotify_tokens_t t = {0}, next = {0};
  cJSON *j = parse(good);
  CHECK(pw_spotify_parse_token(j, NULL, now, &t));
  CHECK(t.authorized_at == now);
  CHECK(t.expires_at == now + 3600);
  CHECK(!strcmp(t.access, "accessABC"));
  cJSON_ReplaceItemInObjectCaseSensitive(
      j, "scope", cJSON_CreateString("user-read-playback-state"));
  CHECK(!pw_spotify_parse_token(j, NULL, now, &next));
  cJSON_Delete(j);
  const char *bad[] = {
      "{}",
      "{\"access_token\":\"abc\",\"token_type\":\"Bearer\",\"expires_in\":3600}",
      "{\"access_token\":\"abc\\r\\nInjected:hi\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"refresh_token\":\"good\"}",
      "{\"access_token\":\"abc\",\"token_type\":\"Basic\",\"expires_in\":3600,\"refresh_token\":\"good\"}",
      "{\"access_token\":\"abc\",\"token_type\":\"Bearer\",\"expires_in\":1.5,\"refresh_token\":\"good\"}",
      "{\"access_token\":\"abc\",\"token_type\":\"Bearer\",\"expires_in\":0,\"refresh_token\":\"good\"}",
      "{\"access_token\":\"abc\",\"token_type\":\"Bearer\",\"expires_in\":3600,\"refresh_token\":null}"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
    j = parse(bad[i]);
    CHECK(j);
    CHECK(!pw_spotify_parse_token(j, NULL, now, &next));
    pw_spotify_secret_json_delete(j);
  }
  j = parse(
      "{\"access_token\":\"newABC\",\"token_type\":\"Bearer\",\"expires_in\":3600}");
  CHECK(pw_spotify_parse_token(j, &t, now + 7200, &next));
  CHECK(!strcmp(next.refresh, t.refresh));
  CHECK(next.authorized_at == t.authorized_at);
  pw_spotify_secret_json_delete(j);
  j = parse(good);
  CHECK(!pw_spotify_parse_token(j, NULL, 1, &next));
  pw_spotify_secret_json_delete(j);
  uint8_t record[PW_SPOTIFY_RECORD_BYTES];
  size_t n = pw_spotify_record_encode(&t, record, sizeof(record));
  CHECK(n > 51);
  memset(&next, 0x42, sizeof(next));
  CHECK(pw_spotify_record_decode(record, n, &next));
  CHECK(!strcmp(next.refresh, t.refresh));
  CHECK(!next.access[0]);
  CHECK(next.authorized_at == now);
  CHECK(!next.expires_at);
  for (size_t i = 0; i < n; i++)
    CHECK(!pw_spotify_record_decode(record, i, &next));
  CHECK(!pw_spotify_record_decode(record, n + 1, &next));
  record[0] ^= 1;
  CHECK(!pw_spotify_record_decode(record, n, &next));
  record[0] ^= 1;
  record[16] ^= 1;
  CHECK(!pw_spotify_record_decode(record, n, &next));
  record[16] ^= 1;
  record[n - 1] = 0;
  CHECK(!pw_spotify_record_decode(record, n, &next));
  memset(t.refresh, 'x', PW_SPOTIFY_TOKEN_BYTES - 1);
  t.refresh[PW_SPOTIFY_TOKEN_BYTES - 1] = 0;
  n = pw_spotify_record_encode(&t, record, sizeof(record));
  CHECK(n > 2048);
  CHECK(pw_spotify_record_decode(record, n, &next));
  CHECK(strlen(next.refresh) == 2048);
}
static void test_pkce(void) {
  uint8_t random[56];
  for (unsigned i = 0; i < 56; i++)
    random[i] = i;
  pw_spotify_auth_t a = {0};
  pw_spotify_auth_start_t start;
  CHECK(pw_spotify_auth_begin(&a, random, 1000, &start));
  CHECK(start.expires_in_seconds == 600);
  CHECK(strlen(a.state) == 48);
  CHECK(!strcmp(a.verifier, "GBkaGxwdHh8gISIjJCUmJygpKissLS4vMDEyMzQ1Njc"));
  CHECK(strstr(start.url,
               "code_challenge=Wq_DNSgoHs3SBN8Y2C1Uu_psmqEWzFpXmA_coVW3InY"));
  CHECK(strstr(start.url,
               "redirect_uri=http%3A%2F%2F127.0.0.1%3A8766%2Fcallback"));
  CHECK(!strstr(start.url, a.verifier));
  CHECK(!strstr(start.url, "client_secret"));
  char state[49];
  strcpy(state, a.state);
  CHECK(!pw_spotify_auth_accept(&a, "code", "wrong", 2000));
  CHECK(a.pending);
  CHECK(!pw_spotify_auth_accept(&a, "code", state, 601000));
  CHECK(pw_spotify_auth_accept(&a, "code", state, 600999));
  CHECK(a.exchanging && !a.pending);
  CHECK(!pw_spotify_auth_accept(&a, "code", state, 600999));
  CHECK(!pw_spotify_auth_begin(&a, random, 2000, &start));
  uint32_t previous = a.generation;
  pw_spotify_auth_cancel(&a);
  CHECK(!a.verifier[0] && !a.state[0]);
  CHECK(a.generation != previous);
  CHECK(!pw_spotify_auth_accept(&a, "code", state, 3000));
  CHECK(pw_spotify_auth_begin(&a, random, 3000, &start));
  strcpy(state, a.state);
  random[0]++;
  CHECK(pw_spotify_auth_begin(&a, random, 4000, &start));
  CHECK(!pw_spotify_auth_accept(&a, "code", state, 4001));
  strcpy(state, a.state);
  CHECK(!pw_spotify_auth_accept(&a, "bad\n", state, 4001));
  char longcode[1026];
  memset(longcode, 'x', sizeof(longcode));
  longcode[1025] = 0;
  CHECK(!pw_spotify_auth_accept(&a, longcode, state, 4001));
  CHECK(pw_spotify_retry_after("300") == 300);
  CHECK(pw_spotify_retry_after(NULL) == 30);
  CHECK(pw_spotify_retry_after("0") == 1);
  CHECK(pw_spotify_retry_after("-1") == 30);
  CHECK(pw_spotify_retry_after("NaN") == 30);
  CHECK(pw_spotify_retry_after("4294967296") == UINT32_MAX);
}
int main(void) {
  test_json();
  test_text_and_uri();
  test_devices();
  test_device_names();
  test_playback_and_commands();
  test_tokens_and_storage();
  test_pkce();
  printf(
      "Spotify model: %u assertions passed (real cJSON/mbedTLS, ASan/UBSan)\n",
      assertions);
  return 0;
}

"use strict";
(() => {
  const $ = (id) => document.getElementById(id);
  const API = "/api/v1";
  const state = { status: null, session: null, busy: false, catalog: { favorites: [], stations: [] }, catalogDirty: false, catalogRevision: null, formRevision: {}, initialized: false, online: false, polling: false, wifiPending: false, wifiTarget: "", conflict: false, places: null, placesLoading: null, selectedPlace: null, update: null, updatePolling: false, updateUploading: false, spotify: null, spotifyDevices: [], spotifyPolling: false, spotifyDeviceSignature: null };
  const kinds = { spotify_playlist: "Playlist", spotify_show: "Podcast", spotify_episode: "Episode" };
  let spotifyReadEpoch = 0, spotifyChanging = false;
  const text = (id, value) => { $(id).textContent = value; };
  const canWrite = () => state.online && state.status?.secure_write !== false && state.session?.secure_write === true && typeof state.session.csrf === "string" && state.session.csrf.length > 0;
  const cloneCatalog = (catalog) => ({ favorites: (catalog?.favorites || []).map((x) => ({ id: x.id, kind: x.kind, name: x.name, uri: x.uri, enabled: x.enabled === true })), stations: (catalog?.stations || []).map((x) => ({ id: x.id, name: x.name, url: x.url, enabled: x.enabled === true })) });

  function message(value, error = false) {
    text("message", value);
    $("message").className = `message ${error ? "error" : "success"}`;
    $("message").hidden = false;
    $("reload-config").hidden = !error || !state.conflict;
  }
  function setWritingControls() {
    const blocked = !canWrite() || state.busy;
    document.querySelectorAll("[data-write], [data-write-button]").forEach((element) => { element.disabled = blocked; });
    $("save-catalog").disabled = blocked || !state.catalogDirty;
    $("discard-catalog").disabled = !state.catalogDirty || state.busy;
    $("security-notice").hidden = !state.online || canWrite();
    $("update-fields").disabled = blocked || state.update?.upload_enabled !== true || state.update?.busy === true;
    spotifyControls();
  }
  function errorText(error) {
    state.conflict = error.code === "conflict" || error.status === 409 || error.status === 412;
    if (state.conflict) return "Die Einstellungen wurden inzwischen geändert. Lade den aktuellen Stand erneut und wiederhole deine Änderung.";
    if (error.status === 401 || error.status === 403) return "Die sichere Sitzung ist abgelaufen oder hier sind keine Änderungen möglich. Verbinde dich erneut mit deinem RotaryKnob.";
    if (error.name === "AbortError") return "Das Gerät antwortet gerade nicht. Bitte versuche es gleich noch einmal.";
    if (error.publicMessage) return error.publicMessage.slice(0, 240);
    return "Die Verbindung zum RotaryKnob wurde unterbrochen. Prüfe dein WLAN und versuche es erneut.";
  }
  async function request(path, options = {}) {
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 18000);
    try {
      const response = await fetch(API + path, { ...options, credentials: "same-origin", cache: "no-store", signal: controller.signal, headers: { Accept: "application/json", ...options.headers } });
      let data = {};
      if (response.status !== 204) {
        try { data = await response.json(); } catch { if (response.ok) throw new Error("Invalid response"); }
      }
      if (!response.ok) {
        const error = new Error("API request failed");
        error.status = response.status;
        error.code = data?.error?.code;
        error.publicMessage = typeof data?.error?.message === "string" ? data.error.message : undefined;
        throw error;
      }
      return data;
    } finally { clearTimeout(timer); }
  }
  async function refreshSession() { state.session = await request("/session"); setWritingControls(); }
  async function mutate(path, method, payload, revision) {
    if (state.busy) throw Object.assign(new Error(), { publicMessage: "Bitte warte, bis das Speichern abgeschlossen ist." });
    if (!canWrite()) throw Object.assign(new Error(), { status: 403 });
    if (!Number.isSafeInteger(revision)) throw Object.assign(new Error(), { publicMessage: "Der aktuelle Gerätestand fehlt. Bitte verbinde dich erneut." });
    state.busy = true;
    setWritingControls();
    try {
      await refreshSession();
      if (!canWrite()) throw Object.assign(new Error(), { status: 403 });
      const result = await request(path, { method, headers: { "Content-Type": "application/json", "X-CSRF-Token": state.session.csrf, "If-Match": String(revision) }, body: JSON.stringify(payload) });
      const newRevision = result.config_revision ?? result.revision;
      if (Number.isSafeInteger(newRevision) && state.status) state.status.config_revision = newRevision;
      return result;
    } finally { state.busy = false; setWritingControls(); }
  }
  function revisionFor(form) { return state.formRevision[form] ?? state.status?.config_revision; }
  function watchForm(id) { $(id).addEventListener("input", () => { state.formRevision[id] ??= state.status?.config_revision; }); }
  ["wifi-form", "weather-form", "settings-form"].forEach(watchForm);

  function navigate(page, focus = true) {
    if (!["overview", "content", "weather", "device"].includes(page)) page = "overview";
    document.querySelectorAll(".page").forEach((element) => { element.hidden = element.id !== "page-" + page; });
    document.querySelectorAll("[data-page]").forEach((element) => { if (element.dataset.page === page) element.setAttribute("aria-current", "page"); else element.removeAttribute("aria-current"); });
    if (location.hash !== "#" + page) history.replaceState(null, "", "#" + page);
    if (page === "weather") loadPlaces();
    if (page === "device") refreshUpdates();
    if (focus) { $("main").focus({ preventScroll: true }); window.scrollTo({ top: 0, behavior: "auto" }); }
  }
  document.querySelectorAll("[data-page], [data-go]").forEach((button) => button.addEventListener("click", () => navigate(button.dataset.page || button.dataset.go)));
  window.addEventListener("hashchange", () => navigate(location.hash.slice(1)));
  navigate(location.hash.slice(1), false);

  function fillForms(status) {
    const settings = status.settings || {};
    if (status.settings && state.formRevision["settings-form"] === undefined) {
      $("device-name").value = settings.name || status.device?.name || "";
      $("brightness").value = Number.isFinite(settings.brightness) ? settings.brightness : 65;
      text("brightness-output", $("brightness").value + " %");
      $("haptic").checked = settings.haptic === true;
      $("screensaver-photo").checked = settings.screensaver_mode === "weather_photo";
    }
    if (status.settings && state.formRevision["weather-form"] === undefined) {
      $("weather-enabled").checked = settings.weather_enabled === true;
      $("avatar-enabled").checked = settings.avatar_enabled === true;
      $("avatar-blond").checked = settings.avatar_blond === true;
      $("weather-latitude").value = Number.isFinite(settings.latitude) && settings.latitude !== 0 ? settings.latitude : "";
      $("weather-longitude").value = Number.isFinite(settings.longitude) && settings.longitude !== 0 ? settings.longitude : "";
      syncLocationLabel();
    }
    if (status.catalog && !state.catalogDirty) {
      state.catalog = cloneCatalog(status.catalog);
      state.catalogRevision = status.config_revision;
      renderCatalog();
    }
  }
  const number = (value, digits = 0) => typeof value === "number" && Number.isFinite(value) ? value.toLocaleString("de-DE", { maximumFractionDigits: digits }) : null;
  function renderWeather(status) {
    const weather = status.weather || {};
    const enabled = status.settings?.weather_enabled === true;
    const temperature = number(weather.current?.temperature, 1);
    const currentAvailable = enabled && temperature !== null && ["ready", "ok", "fresh", "stale"].includes(weather.state);
    const names = { clear: "Klarer Himmel", clearsky: "Klarer Himmel", sunny: "Sonnig", fair: "Heiter", partly_cloudy: "Leicht bewölkt", partlycloudy: "Leicht bewölkt", cloudy: "Bewölkt", rain: "Regen", rainy: "Regen", lightrain: "Leichter Regen", heavyrain: "Starker Regen", rainshowers: "Regenschauer", lightrainshowers: "Leichte Regenschauer", heavyrainshowers: "Starke Regenschauer", snow: "Schnee", lightsnow: "Leichter Schnee", heavysnow: "Starker Schnee", snowshowers: "Schneeschauer", sleet: "Schneeregen", sleetshowers: "Schneeregenschauer", fog: "Nebel", thunderstorm: "Gewitter" };
    const symbol = typeof weather.current?.condition === "string" ? weather.current.condition.replace(/_(day|night|polartwilight)$/, "") : "";
    const condition = symbol.includes("thunder") ? "Gewitter" : names[symbol] || "Vorhersage für deinen Ort";
    text("weather-temperature", currentAvailable ? temperature + "°" : "—");
    text("weather-location", weather.location_name || weather.station_name || "DEIN STANDORT");
    let heading = "Noch nicht eingerichtet", description = "Aktiviere Wetter und hinterlege deinen Standort.";
    if (enabled) {
      heading = currentAvailable ? condition : "Wetter wird geladen";
      description = currentAvailable ? (number(weather.current?.wind_kmh) !== null ? "Wind: " + number(weather.current.wind_kmh) + " km/h" : "Die Werte stammen aus einer Wettervorhersage.") : "Sobald aktuelle Daten vorliegen, erscheinen sie hier.";
      if (weather.state === "stale") { heading = "Zuletzt verfügbares Wetter"; description = "Diese Vorhersage ist nicht mehr aktuell. Der RotaryKnob versucht einen neuen Abruf."; }
      if (["error", "unavailable", "offline"].includes(weather.state)) { heading = "Wetter gerade nicht verfügbar"; description = "Prüfe WLAN und Standort. Der nächste Abruf erfolgt automatisch."; }
      if (["not_configured", "disabled"].includes(weather.state)) { heading = "Standort noch nicht bereit"; description = "Prüfe die Koordinaten und speichere deine Wetter-Einstellungen."; }
      if (weather.state === "unconfigured") { heading = "Standort noch nicht bereit"; description = "Prüfe die Koordinaten und speichere deine Wetter-Einstellungen."; }
      if (weather.state === "waiting_network") { heading = "Wartet auf dein WLAN"; description = "Sobald dein RotaryKnob online ist, wird das Wetter abgerufen."; }
      if (weather.state === "waiting_clock") { heading = "Die Uhr wird eingestellt"; description = "Der RotaryKnob benötigt die aktuelle Uhrzeit für eine gültige Vorhersage."; }
      if (weather.state === "suspended") { heading = "Wetterabruf pausiert"; description = "Der RotaryKnob setzt den Abruf automatisch fort."; }
    }
    if (!status.settings) { heading = "Dein Wetter ist geschützt"; description = "Öffne die geschützte Einrichtung am RotaryKnob, um deinen Standort und das Wetter zu sehen."; }
    text("weather-state", heading);
    text("weather-description", description);
    text("weather-short", currentAvailable ? `${temperature} °C · ${condition}${weather.state === "stale" ? " · veraltet" : ""}` : enabled ? heading : "Wetter für deinen Standort einrichten.");
    const attribution = weather.attribution || weather.source;
    const checked = Number.isFinite(weather.checked_utc) && weather.checked_utc > 0 ? new Date(weather.checked_utc * 1000).toLocaleString("de-DE", { timeZone: "Europe/Berlin", day: "2-digit", month: "2-digit", hour: "2-digit", minute: "2-digit" }) : "";
    text("weather-source", [typeof attribution === "string" ? attribution : "", checked ? "Zuletzt geprüft: " + checked : ""].filter(Boolean).join(" · "));
    const container = $("weather-days");
    container.replaceChildren();
    if (enabled && Array.isArray(weather.days)) {
      weather.days.slice(0, 5).forEach((day) => {
        const low = number(day.temperature_min ?? day.min), high = number(day.temperature_max ?? day.max);
        if (low === null && high === null) return;
        const row = document.createElement("div"); row.className = "forecast-day";
        const label = document.createElement("span"), temps = document.createElement("span");
        const date = typeof day.date === "string" && /^\d{4}-\d{2}-\d{2}$/.test(day.date) ? new Date(day.date + "T12:00:00") : null;
        label.textContent = date && !Number.isNaN(date.getTime()) ? date.toLocaleDateString("de-DE", { weekday: "short", day: "numeric", month: "numeric" }) : "Vorhersage";
        temps.textContent = `${low ?? "—"}° / ${high ?? "—"}°`;
        row.append(label, temps); container.append(row);
      });
    }
    container.hidden = !container.childElementCount;
  }
  function renderStatus(status) {
    const network = status.network || {}, connected = network.connected === true;
    text("connection-pill", connected ? "Im WLAN verbunden" : "Einrichtung");
    $("connection-pill").classList.toggle("connected", connected);
    text("welcome-copy", connected ? "Dein RotaryKnob ist verbunden. Mach ihn jetzt zu deinem." : "Ein paar einfache Schritte. Und dein RotaryKnob ist bereit für dich.");
    text("network-summary", connected ? "Dein RotaryKnob ist mit deinem Heimnetz verbunden." : state.wifiPending ? "Die WLAN-Verbindung wird geprüft. Das kann einen Moment dauern." : "Verbinde deinen RotaryKnob mit einem 2,4-GHz-WLAN.");
    $("network-facts").hidden = !connected;
    text("network-name", network.ssid || (connected ? "Name geschützt" : "—")); text("network-ip", network.ip || "—");
    text("wifi-summary", connected ? "WLAN wechseln" : "WLAN einrichten");
    $("step-network").classList.toggle("done", connected);
    $("step-weather").classList.toggle("done", status.settings?.weather_enabled === true);
    text("device-info-name", status.settings?.name || status.device?.name || "Passion Wave");
    text("device-version", status.device?.version || "—");
    text("device-hardware", status.device?.hardware || "—");
    text("device-wifi", network.ssid || (connected ? "Verbunden" : "Nicht verbunden"));
    text("footer-device", status.settings?.name || status.device?.name || "Lokale Gerätewebsite");
    renderWeather(status);
    renderSpotify();
    if (!state.initialized) { $("wifi-details").open = !connected; }
    if (state.wifiPending && connected && !network.connecting && network.ssid === state.wifiTarget) {
      state.wifiPending = false;
      $("wifi-details").open = false;
      message("Mit deinem WLAN verbunden. Du kannst jetzt Wetter und Inhalte einrichten.");
    }
    if (state.wifiPending && !network.connecting && network.error) { text("network-summary", network.error + ". Bitte prüfe deine WLAN-Daten."); $("wifi-details").open = true; }
    fillForms(status);
    state.initialized = true;
  }
  async function refreshStatus({ session = false, silent = false } = {}) {
    if (state.polling) return;
    state.polling = true;
    try {
      if (session || !state.session) await refreshSession();
      const status = await request("/status");
      if (!status || typeof status !== "object" || !status.device || !status.network) throw new Error("Invalid device status");
      state.status = status; state.online = true;
      $("connection-notice").hidden = true;
      renderStatus(status);
      refreshSpotify();
      if (!$("page-device").hidden) refreshUpdates();
    } catch (error) {
      state.online = false;
      text("connection-pill", "Nicht erreichbar");
      $("connection-pill").classList.remove("connected");
      text("connection-message", state.wifiPending ? "Dein RotaryKnob wechselt möglicherweise gerade ins Heim-WLAN. Verbinde auch dieses Gerät mit deinem Heim-WLAN und öffne die am RotaryKnob angezeigte Adresse." : "Dein RotaryKnob ist gerade nicht erreichbar. Prüfe, ob du mit demselben WLAN verbunden bist.");
      $("connection-notice").hidden = false;
      if (!silent && state.initialized) message(errorText(error), true);
    } finally { state.polling = false; setWritingControls(); }
  }
  $("retry-connection").addEventListener("click", () => refreshStatus({ session: true }));
  $("reload-config").addEventListener("click", async () => {
    const button = $("reload-config"); button.disabled = true;
    try {
      const status = await request("/status");
      if (!status?.device || !status?.network) throw new Error("Invalid device status");
      state.status = status; state.catalogDirty = false; state.formRevision = {}; state.online = true; state.conflict = false;
      renderStatus(status); message("Der aktuelle Gerätestand ist geladen. Deine ungespeicherten Änderungen wurden verworfen.");
    } catch (error) { message(errorText(error), true); }
    finally { button.disabled = false; setWritingControls(); }
  });
  $("show-password").addEventListener("click", () => {
    const show = $("wifi-password").type === "password";
    $("wifi-password").type = show ? "text" : "password";
    $("show-password").setAttribute("aria-pressed", String(show));
    $("show-password").setAttribute("aria-label", show ? "WLAN-Passwort verbergen" : "WLAN-Passwort anzeigen");
    text("show-password", show ? "Verbergen" : "Anzeigen");
  });
  $("scan-wifi").addEventListener("click", async () => {
    const button = $("scan-wifi"), list = $("wifi-networks");
    button.disabled = true; text("scan-wifi", "Suche …");
    list.hidden = false; list.textContent = "Netzwerke werden gesucht …";
    try {
      const result = await request("/wifi/scan");
      list.replaceChildren();
      const seen = new Set();
      const networks = (Array.isArray(result.networks) ? result.networks : []).filter((network) => typeof network.ssid === "string" && network.ssid && !seen.has(network.ssid) && seen.add(network.ssid));
      networks.sort((a, b) => (b.rssi || -100) - (a.rssi || -100));
      for (const network of networks.slice(0, 24)) {
        const choice = document.createElement("button"); choice.type = "button"; choice.className = "network-choice";
        const name = document.createElement("span"), detail = document.createElement("small"); name.textContent = network.ssid;
        detail.textContent = (network.secure ? "Geschützt" : "Offen") + (network.rssi > -65 ? " · gutes Signal" : "");
        choice.append(name, detail); choice.addEventListener("click", () => { $("wifi-ssid").value = network.ssid; $("wifi-password").required = network.secure === true; state.formRevision["wifi-form"] ??= state.status?.config_revision; if (network.secure) $("wifi-password").focus(); list.hidden = true; }); list.append(choice);
      }
      if (!networks.length) list.textContent = "Keine Netzwerke gefunden. Du kannst den WLAN-Namen oben eintragen.";
    } catch (error) { list.textContent = errorText(error); }
    finally { text("scan-wifi", "Erneut suchen"); button.disabled = !canWrite(); }
  });
  $("wifi-ssid").addEventListener("input", () => { $("wifi-password").required = false; });
  $("wifi-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    const ssid = $("wifi-ssid").value;
    if (new TextEncoder().encode(ssid).length > 32) { message("Der WLAN-Name ist zu lang. Bitte prüfe die Schreibweise.", true); return; }
    try {
      await mutate("/wifi", "POST", { ssid, password: $("wifi-password").value }, revisionFor("wifi-form"));
      $("wifi-password").value = "";
      $("wifi-password").type = "password"; text("show-password", "Anzeigen"); $("show-password").setAttribute("aria-pressed", "false"); $("show-password").setAttribute("aria-label", "WLAN-Passwort anzeigen");
      delete state.formRevision["wifi-form"]; state.wifiPending = true; state.wifiTarget = ssid;
      message("WLAN-Daten übernommen. Der RotaryKnob prüft die Verbindung und speichert sie erst nach erfolgreicher Anmeldung.");
      setTimeout(() => refreshStatus({ silent: true }), 2000);
    } catch (error) { $("wifi-password").value = ""; message(errorText(error), true); }
  });
  $("brightness").addEventListener("input", () => text("brightness-output", $("brightness").value + " %"));
  $("settings-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    const name = $("device-name").value.trim();
    if (!name || new TextEncoder().encode(name).length > 48) { message("Bitte gib deinem RotaryKnob einen kurzen Namen. Lange Namen kannst du etwas kürzen.", true); return; }
    try {
      await mutate("/settings", "PATCH", { name, brightness: Number($("brightness").value), haptic: $("haptic").checked, screensaver_mode: $("screensaver-photo").checked ? "weather_photo" : "off" }, revisionFor("settings-form"));
      delete state.formRevision["settings-form"];
      message("Deine Einstellungen sind gespeichert."); await refreshStatus({ silent: true });
    } catch (error) { message(errorText(error), true); }
  });
  $("weather-enabled").addEventListener("change", () => { if ($("weather-enabled").checked && !$("weather-latitude").value) $("weather-place").focus(); });
  $("weather-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    const enabled = $("weather-enabled").checked, latitude = $("weather-latitude").value, longitude = $("weather-longitude").value;
    if (enabled && (!latitude || !longitude)) { message("Wähle bitte zuerst deinen Ort oder deine Postleitzahl aus.", true); $("weather-place").focus(); return; }
    const settings = { weather_enabled: enabled, avatar_enabled: $("avatar-enabled").checked, avatar_blond: $("avatar-blond").checked };
    if (latitude && longitude) { settings.latitude = Number(latitude); settings.longitude = Number(longitude); settings.timezone = "Europe/Berlin"; }
    try { await mutate("/settings", "PATCH", settings, revisionFor("weather-form")); delete state.formRevision["weather-form"]; message(enabled ? "Wetter ist aktiviert. Die Vorhersage wird direkt auf deinem RotaryKnob abgerufen." : "Wetter ist ausgeschaltet."); await refreshStatus({ silent: true }); }
    catch (error) { message(errorText(error), true); }
  });

  function placeKey(value) { return value.toLocaleLowerCase("de-DE").normalize("NFKD").replace(/[\u0300-\u036f]/g, "").replace(/ß/g, "ss").trim(); }
  function syncLocationLabel() {
    const lat = Number($("weather-latitude").value), lon = Number($("weather-longitude").value);
    if (!$("weather-latitude").value || !$("weather-longitude").value) { state.selectedPlace = null; text("selected-place", "Noch kein Standort ausgewählt."); return; }
    const place = state.places?.find((entry) => Math.abs(entry.latitude - lat) < 0.00005 && Math.abs(entry.longitude - lon) < 0.00005);
    state.selectedPlace = place || null;
    text("selected-place", place ? "Ausgewählt: " + place.label : "Standort: " + lat.toLocaleString("de-DE", { maximumFractionDigits: 4 }) + " / " + lon.toLocaleString("de-DE", { maximumFractionDigits: 4 }));
  }
  async function loadPlaces() {
    if (state.places) return state.places;
    if (state.placesLoading) return state.placesLoading;
    state.placesLoading = (async () => {
      text("places-help", "Ortsverzeichnis wird vom RotaryKnob geladen …");
      const controller = new AbortController(), timer = setTimeout(() => controller.abort(), 25000);
      try {
        const response = await fetch("/places-de.json", { credentials: "same-origin", signal: controller.signal, headers: { Accept: "application/json" } });
        if (!response.ok) throw new Error("Places unavailable");
        const data = await response.json();
        if (JSON.stringify(data.columns) !== JSON.stringify(["postcode", "name", "state", "latitude", "longitude"]) || !Array.isArray(data.places) || data.places.length > 30000) throw new Error("Invalid places");
        const places = [];
        for (const row of data.places) {
          if (!Array.isArray(row) || row.length !== 5 || typeof row[0] !== "string" || !/^\d{5}$/.test(row[0]) || typeof row[1] !== "string" || !row[1] || row[1].length > 120 || typeof row[2] !== "string" || row[2].length > 120 || !Number.isFinite(row[3]) || row[3] < 47 || row[3] > 55.2 || !Number.isFinite(row[4]) || row[4] < 5.5 || row[4] > 15.6) continue;
          const regions = { "Bavaria": "Bayern", "Hesse": "Hessen", "Lower Saxony": "Niedersachsen", "North Rhine-Westphalia": "Nordrhein-Westfalen", "Rhineland-Palatinate": "Rheinland-Pfalz", "Saxony": "Sachsen", "Saxony-Anhalt": "Sachsen-Anhalt", "Thuringia": "Thüringen" };
          const region = regions[row[2]] || row[2], label = row[0] + " " + row[1];
          places.push({ label, name: row[1], region, latitude: row[3], longitude: row[4], key: placeKey(label + " " + region) });
        }
        if (!places.length) throw new Error("Empty places");
        state.places = places;
        text("places-help", "Mindestens zwei Zeichen eingeben und deinen Ort auswählen.");
        syncLocationLabel(); renderPlaceResults();
        return places;
      } catch {
        text("places-help", "Ortsverzeichnis gerade nicht verfügbar. Erneut ins Suchfeld tippen oder unten Koordinaten eingeben.");
        return null;
      } finally { clearTimeout(timer); state.placesLoading = null; }
    })();
    return state.placesLoading;
  }
  function renderPlaceResults() {
    const container = $("place-results"), query = placeKey($("weather-place").value);
    container.replaceChildren(); container.hidden = query.length < 2 || !state.places;
    if (container.hidden) return;
    const terms = query.split(/\s+/).filter(Boolean);
    const results = state.places.filter((place) => terms.every((term) => place.key.includes(term))).slice(0, 12);
    if (!results.length) { const hint = document.createElement("p"); hint.textContent = "Keinen Ort gefunden. Versuche eine Postleitzahl oder einen benachbarten Ort."; container.append(hint); return; }
    for (const place of results) {
      const button = document.createElement("button"), label = document.createElement("span"), region = document.createElement("small");
      button.type = "button"; button.className = "place-choice"; button.disabled = !canWrite();
      label.textContent = place.label; region.textContent = place.region; button.append(label, region);
      button.addEventListener("click", () => {
        if (!canWrite() || state.busy) return;
        state.formRevision["weather-form"] ??= state.status?.config_revision;
        $("weather-latitude").value = place.latitude; $("weather-longitude").value = place.longitude;
        $("weather-place").value = place.label; $("weather-enabled").checked = true;
        container.hidden = true; syncLocationLabel();
      });
      container.append(button);
    }
  }
  $("weather-place").addEventListener("input", async () => { await loadPlaces(); renderPlaceResults(); });
  $("weather-place").addEventListener("focus", loadPlaces);
  ["weather-latitude", "weather-longitude"].forEach((id) => $(id).addEventListener("input", syncLocationLabel));

  function renderUpdates() {
    const update = state.update;
    if (!update) { text("update-status", canWrite() ? "Update-Status wird geladen …" : "Für den Update-Status öffne die geschützte Einrichtung am RotaryKnob."); return; }
    const labels = { locked: "Updates sind für dieses Gerät noch nicht freigegeben.", idle: "Bereit für ein signiertes Paket.", receiving: "Das Paket wird empfangen und seine Signatur geprüft.", staging_companion: "Das Image des Begleitprozessors wird geprüft und vorbereitet.", staging_s3: "Das Display-Image wird geprüft und vorbereitet.", local_ready: "Beide Images sind lokal geprüft. Der Begleitprozessor wird vorbereitet.", transferring_companion: "Der Begleitprozessor empfängt und prüft sein Image selbst.", prepared: "Beide Images sind vorbereitet. Sie wurden noch nicht aktiviert.", failed: "Das Paket konnte nicht vollständig vorbereitet werden. Die aktive Firmware wurde nicht umgeschaltet.", recovery: "Ein früherer Vorgang wurde unterbrochen. Staging-Daten werden nicht automatisch fortgesetzt." };
    text("update-badge", update.state === "prepared" ? "Noch nicht installiert" : update.busy ? "Wird vorbereitet" : update.upload_enabled ? "Vorbereitung möglich" : "Noch nicht freigegeben");
    text("update-status", (labels[update.state] || "Update-Status wird geprüft.") + (update.version ? " Zielversion: " + update.version + "." : ""));
    const progress = $("update-progress");
    let value = Number(update.received), total = Number(update.total);
    if (update.state === "transferring_companion" || update.state === "prepared") { value = Number(update.peer_received); total = Number(update.peer_total); }
    if (Number.isFinite(value) && Number.isFinite(total) && total > 0 && value >= 0 && value <= total) { progress.hidden = false; progress.max = total; progress.value = value; }
    else progress.hidden = true;
    setWritingControls();
  }
  async function refreshUpdates() {
    if (state.updatePolling) return;
    if (!canWrite()) { renderUpdates(); return; }
    state.updatePolling = true;
    try {
      const result = await request("/updates");
      if (typeof result?.state !== "string" || typeof result.upload_enabled !== "boolean") throw new Error("Invalid update status");
      state.update = result; renderUpdates();
    } catch (error) {
      if (error.status === 401 || error.status === 403) { try { await refreshSession(); } catch {} }
      text("update-status", errorText(error));
    } finally { state.updatePolling = false; }
  }
  $("update-refresh").addEventListener("click", () => refreshUpdates());
  function transmitBundle(file, revision) {
    return new Promise((resolve, reject) => {
      const xhr = new XMLHttpRequest(); xhr.open("POST", API + "/updates/upload"); xhr.timeout = 20 * 60 * 1000; xhr.withCredentials = true;
      xhr.setRequestHeader("Content-Type", "application/octet-stream");
      xhr.setRequestHeader("X-CSRF-Token", state.session.csrf); xhr.setRequestHeader("If-Match", String(revision)); xhr.setRequestHeader("X-PW-USB-Power", "confirmed");
      xhr.upload.addEventListener("progress", (event) => { if (event.lengthComputable) text("update-status", "Übertragung zum Gerät: " + Math.round(event.loaded / event.total * 100) + " %. Danach prüft das Gerät beide Images."); });
      xhr.addEventListener("load", () => {
        let result; try { result = JSON.parse(xhr.responseText); } catch { reject(new Error("Invalid update response")); return; }
        if (xhr.status >= 200 && xhr.status < 300) resolve(result);
        else reject(Object.assign(new Error("Upload rejected"), { status: xhr.status, code: result?.error?.code, publicMessage: result?.error?.message }));
      });
      xhr.addEventListener("error", () => reject(new Error("Upload connection lost")));
      xhr.addEventListener("timeout", () => reject(Object.assign(new Error(), { name: "AbortError" })));
      xhr.addEventListener("abort", () => reject(new Error("Upload cancelled")));
      xhr.send(file);
    });
  }
  $("update-form").addEventListener("submit", async (event) => {
    event.preventDefault();
    const file = $("update-file").files?.[0];
    if (!file || !/\.pwota$/i.test(file.name) || file.size < 598 || file.size > 16 * 1024 * 1024) { message("Wähle bitte ein signiertes .pwota-Paket bis 16 MB.", true); return; }
    if (!canWrite() || state.busy || !state.update?.upload_enabled || !$("update-power").checked) return;
    state.busy = true; state.updateUploading = true; setWritingControls();
    try {
      await refreshSession();
      if (!canWrite() || !Number.isSafeInteger(state.status?.config_revision)) throw Object.assign(new Error(), { status: 403 });
      await transmitBundle(file, state.status.config_revision);
      message("Paket lokal geprüft. Der Gerätestatus zeigt die anschließende Vorbereitung des Begleitprozessors.");
      $("update-file").value = "";
    } catch (error) { message(errorText(error), true); }
    finally { state.busy = false; state.updateUploading = false; setWritingControls(); await refreshUpdates(); }
  });
  setInterval(() => { if (!document.hidden && !$("page-device").hidden && canWrite()) refreshUpdates(); }, 2000);

  function spotifyOn() { return state.status?.capabilities?.spotify === true && canWrite(); }
  function spotifyTarget() {
    const s = state.spotify;
    return s && s.selected?.id ? { session: s.session, selection_generation: s.selection_generation, device_id: s.selected.id } : null;
  }
  function spotifyPlayingHere() { const s = state.spotify; return s?.playback?.known === true && s.playback.is_playing === true && s.playback.device_id === s.selected?.id; }
  function spotifyControls() {
    const s = state.spotify, blocked = !spotifyOn() || state.busy || spotifyChanging || !s?.linked;
    const target = !blocked && s.selected?.present === true && !s.selected.restricted;
    $("spotify-device").disabled = blocked || !state.spotifyDevices.length;
    $("spotify-refresh").disabled = !spotifyOn() || state.busy || spotifyChanging;
    $("spotify-play").disabled = !target || (spotifyPlayingHere() ? s.actions?.pause !== true : s.actions?.play !== true);
    $("spotify-previous").disabled = !target || s.actions?.previous !== true;
    $("spotify-next").disabled = !target || s.actions?.next !== true;
    $("spotify-volume").disabled = !target || s.actions?.volume !== true || !s.selected?.volume_known;
    $("spotify-disconnect").disabled = blocked;
    document.querySelectorAll("[data-spotify-favorite]").forEach((button) => {
      button.disabled = !target || s.actions?.play !== true || state.catalogDirty || button.dataset.spotifyKind !== "spotify_playlist" || button.dataset.spotifyEnabled !== "true";
    });
  }
  function renderSpotify() {
    const available = state.status?.capabilities?.spotify === true;
    const on = spotifyOn(), s = state.spotify;
    $("music-unavailable").hidden = on;
    $("music-player").hidden = !on;
    if (!on) {
      text("music-unavailable-title", available ? "Deine Musik ist geschützt." : "Musik kommt als Nächstes.");
      text("music-unavailable-copy", available ? "Öffne die geschützte Einrichtung am RotaryKnob, um Spotify und deine Ausgabe zu bedienen." : "Die Spotify-Verbindung ist in dieser Geräteversion noch nicht verfügbar. Deine Playlists und Podcasts kannst du bereits vorbereiten.");
    }
    text("content-playback-hint", on ? "Gespeicherte, freigegebene Playlists kannst du direkt auf deiner gewählten Ausgabe starten. Podcasts und Episoden bleiben in dieser Laborversion vorbereitet." : "Wiedergabe ist hier noch nicht verfügbar. Deine Auswahl wird auf dem RotaryKnob gespeichert.");
    const messages = { disabled: "Spotify ist in diesem Geräteprofil noch nicht verfügbar.", unlinked: "Verbinde Spotify einmal über die USB-Einrichtung.", waiting_network: "Der Knob wartet auf dein WLAN.", waiting_clock: "Die Uhrzeit wird eingestellt.", authorizing: "Spotify-Anmeldung wird abgeschlossen …", ready: "Wähle deine Ausgabe und starte deine Musik.", reauth_required: "Bitte verbinde Spotify über USB erneut.", rate_limited: "Spotify braucht kurz eine Pause. Wir warten automatisch.", error: "Spotify ist gerade nicht erreichbar.", suspended: "Spotify pausiert während der Updatevorbereitung.", disconnecting: "Spotify-Verbindung wird entfernt …" };
    let status = messages[s?.state] || "Spotify wird geprüft …";
    if (s?.state === "ready" && s.selected?.id && !s.selected.present) status = "Deine Ausgabe ist nicht erreichbar. Öffne sie in Spotify oder wähle eine andere.";
    if (s?.error === "forbidden") status = "Spotify erlaubt diese Aktion nicht. Premium, Testkontofreigabe und Ausgabegerät prüfen.";
    if (s?.last_command_state === "queued") status = "Anfrage wird an Spotify gesendet …";
    if (s?.last_command_state === "uncertain") status = "Die Bestätigung fehlt. Prüfe die Wiedergabe, bevor du erneut drückst.";
    if (s?.last_command_state === "rejected") status = "Spotify hat die letzte Aktion nicht ausgeführt.";
    if (s?.last_command_state === "stale") status = "Die Ausgabe hat sich geändert. Bitte erneut wählen.";
    text("spotify-state", status);
    const picker = $("spotify-device"), selected = s?.selected?.id || "";
    const signature = JSON.stringify([selected, state.spotifyDevices.map((d) => [d.id, d.name, d.restricted])]);
    if (signature !== state.spotifyDeviceSignature) {
      state.spotifyDeviceSignature = signature; picker.replaceChildren();
      const placeholder = document.createElement("option"); placeholder.value = ""; placeholder.textContent = "Ausgabe wählen"; picker.append(placeholder);
      if (selected && !state.spotifyDevices.some((d) => d.id === selected)) { const option = document.createElement("option"); option.value = selected; option.textContent = (s.selected.name || "Bisherige Ausgabe") + " · nicht verfügbar"; option.disabled = true; picker.append(option); }
      state.spotifyDevices.forEach((device) => { const option = document.createElement("option"); option.value = device.id; option.textContent = device.name + (device.restricted ? " · eingeschränkt" : ""); option.disabled = device.restricted || !device.id; picker.append(option); });
      picker.value = selected;
    }
    text("spotify-device-hint", state.spotifyDevices.length ? "Die Auswahl allein startet oder verschiebt keine Musik." : "Kein Gerät gefunden? Aktiviere den Lautsprecher in der Spotify-App und aktualisiere danach.");
    const p = s?.playback;
    text("spotify-title", p?.known ? p.title || "Wiedergabe ohne Titelangabe" : "Noch keine Wiedergabe bestätigt");
    text("spotify-artist", p?.known ? p.artist || "" : "");
    text("spotify-playing-device", p?.known ? (p.is_playing ? "Spielt" : "Pausiert") + (p.device_name ? " · " + p.device_name : "") : "");
    text("spotify-play", spotifyPlayingHere() ? "Pause" : "Abspielen");
    const progress = $("spotify-progress");
    progress.hidden = !p?.known || !p.position_known || !(p.duration_ms > 0);
    if (!progress.hidden) { progress.max = p.duration_ms; progress.value = Math.max(0, Math.min(p.duration_ms, p.position_ms)); }
    $("spotify-volume-field").hidden = s?.selected?.supports_volume !== true;
    if (s?.selected?.volume_known && document.activeElement !== $("spotify-volume")) {
      $("spotify-volume").value = s.selected.volume_percent;
      text("spotify-volume-output", s.selected.volume_percent + " %");
    }
    spotifyControls();
  }
  async function refreshSpotify(afterMutation = false) {
    if (state.spotifyPolling) { await state.spotifyPolling; if (!afterMutation) return; }
    if (!spotifyOn()) { state.spotify = null; state.spotifyDevices = []; renderSpotify(); return; }
    const epoch = spotifyReadEpoch;
    const operation = (async () => {
    try {
      const [snapshot, devices] = await Promise.all([request("/spotify/snapshot"), request("/spotify/devices")]);
      if (typeof snapshot?.state !== "string" || !Array.isArray(devices?.devices)) throw new Error("Invalid Spotify status");
      if (!spotifyOn() || epoch !== spotifyReadEpoch) return;
      state.spotify = snapshot;
      state.spotifyDevices = devices.session === snapshot.session ? devices.devices.slice(0, 16).filter((d) => typeof d.id === "string" && typeof d.name === "string") : [];
      renderSpotify();
    } catch (error) {
      if (epoch !== spotifyReadEpoch) return;
      state.spotify = null; state.spotifyDevices = [];
      text("spotify-message", errorText(error));
      if (error.status === 401 || error.status === 403) { try { await refreshSession(); } catch {} }
      renderSpotify();
    }
    })();
    state.spotifyPolling = operation;
    await operation;
    if (state.spotifyPolling === operation) state.spotifyPolling = null;
  }
  async function spotifyAction(action, extra = {}) {
    if (spotifyChanging) return;
    const target = spotifyTarget();
    if (action !== "refresh" && !target) { text("spotify-message", "Wähle zuerst deine Ausgabe."); return; }
    try {
      await mutate("/spotify/action", "POST", action === "refresh" ? { action } : { action, ...target, ...extra }, state.status?.config_revision);
      text("spotify-message", action === "refresh" ? "Aktuelle Geräte werden angefragt." : "Anfrage gesendet. Der angezeigte Zustand folgt der Bestätigung von Spotify.");
      await refreshSpotify();
    } catch (error) { text("spotify-message", error.code === "spotify_changed" ? "Die Ausgabe hat sich geändert. Bitte erneut wählen." : errorText(error)); await refreshSpotify(); }
  }
  $("spotify-device").addEventListener("change", async () => {
    const id = $("spotify-device").value;
    if (!id || !state.spotify || spotifyChanging) return;
    spotifyChanging = true; spotifyReadEpoch++; spotifyControls();
    try {
      await mutate("/spotify/select", "POST", { device_id: id, session: state.spotify.session }, state.status?.config_revision);
      text("spotify-message", "Ausgabe gewählt. Abspielen startet Musik auf diesem Gerät.");
      await refreshSpotify(true);
    } catch (error) { text("spotify-message", error.code === "spotify_changed" ? "Die Geräteliste hat sich geändert. Bitte erneut wählen." : errorText(error)); state.spotifyDeviceSignature = null; await refreshSpotify(true); }
    finally { spotifyChanging = false; renderSpotify(); }
  });
  $("spotify-play").addEventListener("click", () => spotifyAction(spotifyPlayingHere() ? "pause" : "play"));
  $("spotify-previous").addEventListener("click", () => spotifyAction("previous"));
  $("spotify-next").addEventListener("click", () => spotifyAction("next"));
  $("spotify-refresh").addEventListener("click", () => spotifyAction("refresh"));
  $("spotify-volume").addEventListener("input", () => text("spotify-volume-output", $("spotify-volume").value + " %"));
  $("spotify-volume").addEventListener("change", () => spotifyAction("volume", { volume_percent: Number($("spotify-volume").value) }));
  $("spotify-disconnect").addEventListener("click", async () => {
    try { await mutate("/spotify/disconnect", "POST", {}, state.status?.config_revision); text("spotify-message", "Spotify wird vom Knob getrennt."); await refreshSpotify(); }
    catch (error) { text("spotify-message", errorText(error)); }
  });
  setInterval(() => { if (!document.hidden && !state.busy && spotifyOn()) refreshSpotify(); }, 3000);

  function spotifyLink(value) {
    let match = /^spotify:(playlist|show|episode):([A-Za-z0-9]{22})$/.exec(value.trim());
    if (!match) {
      try {
        const url = new URL(value.trim());
        if (url.protocol !== "https:" || url.hostname !== "open.spotify.com" || url.username || url.password || url.port) return null;
        const path = url.pathname.replace(/^\/intl-[a-z]{2}(?=\/)/, "");
        const found = /^\/(playlist|show|episode)\/([A-Za-z0-9]{22})\/?$/.exec(path);
        if (found) match = found;
      } catch { return null; }
    }
    return match ? { kind: "spotify_" + match[1], uri: "spotify:" + match[1] + ":" + match[2] } : null;
  }
  function streamUrl(value) {
    try { const url = new URL(value.trim()); return ["http:", "https:"].includes(url.protocol) && !url.username && !url.password && !url.hash && !/\s/.test(value.trim()) && new TextEncoder().encode(url.href).length <= 1024 ? url.href : null; } catch { return null; }
  }
  function makeId(prefix) {
    if (globalThis.crypto?.getRandomValues) { const bytes = new Uint8Array(12); crypto.getRandomValues(bytes); return prefix + "_" + [...bytes].map((x) => x.toString(16).padStart(2, "0")).join(""); }
    return prefix + "_" + Date.now().toString(36) + "_" + Math.random().toString(36).slice(2, 10);
  }
  function markCatalogDirty() { if (!state.catalogDirty) state.catalogRevision = state.status?.config_revision; state.catalogDirty = true; renderCatalog(); }
  function catalogButton(label, textValue, action, disabled = false, extra = "") {
    const button = document.createElement("button"); button.type = "button"; button.className = "icon-button " + extra; button.textContent = textValue; button.setAttribute("aria-label", label); button.title = label; button.disabled = disabled || !canWrite() || state.busy; button.dataset.writeButton = "";
    // Position limits remain independent of the session-wide disabled state.
    if (disabled) { delete button.dataset.writeButton; }
    button.addEventListener("click", action); return button;
  }
  function renderCatalog() {
    for (const group of ["favorites", "stations"]) {
      const items = state.catalog[group], container = $(group + "-list"); container.replaceChildren();
      text(group + "-count", items.length + " / 64");
      if (!items.length) { const empty = document.createElement("p"); empty.className = "empty-state"; empty.textContent = group === "favorites" ? "Hier ist Platz für deine Lieblingsmusik und Podcasts." : "Deine Radiosender erscheinen hier."; container.append(empty); }
      items.forEach((item, index) => {
        const row = document.createElement("div"); row.className = "catalog-row";
        const symbol = document.createElement("span"); symbol.className = "item-symbol"; symbol.textContent = group === "favorites" ? "♪" : "◌"; symbol.setAttribute("aria-hidden", "true");
        const description = document.createElement("div"); description.className = "item-description";
        const name = document.createElement("input"); name.type = "text"; name.className = "item-edit"; name.value = item.name || ""; name.maxLength = 80; name.required = true; name.setAttribute("aria-label", "Anzeigename für " + (item.name || "Inhalt")); name.disabled = !canWrite() || state.busy; name.dataset.writeButton = "";
        name.addEventListener("input", () => { if (!state.catalogDirty) state.catalogRevision = state.status?.config_revision; state.catalogDirty = true; item.name = name.value; updateCatalogSaveState(); });
        const meta = document.createElement("span"); meta.className = "item-meta"; meta.textContent = group === "favorites" ? kinds[item.kind] || "Spotify-Favorit" : item.url;
        description.append(name, meta);
        const actions = document.createElement("div"); actions.className = "item-actions";
        const enabledLabel = document.createElement("label"); enabledLabel.className = "item-enabled";
        const enabled = document.createElement("input"); enabled.type = "checkbox"; enabled.checked = item.enabled; enabled.dataset.writeButton = ""; enabled.disabled = !canWrite() || state.busy; enabled.setAttribute("aria-label", item.name + " auf dem Knob anzeigen");
        const enabledText = document.createElement("span"); enabledText.textContent = "Anzeigen"; enabledLabel.append(enabled, enabledText);
        enabled.addEventListener("change", () => { item.enabled = enabled.checked; markCatalogDirty(); });
        if (group === "favorites" && state.status?.capabilities?.spotify === true) {
          const play = document.createElement("button"); play.type = "button"; play.className = "icon-button catalog-play"; play.textContent = "▶";
          play.setAttribute("aria-label", item.name + " abspielen"); play.dataset.spotifyFavorite = item.id; play.dataset.spotifyKind = item.kind; play.dataset.spotifyEnabled = String(item.enabled === true);
          play.title = item.kind === "spotify_playlist" ? "Gespeicherte Playlist auf gewählter Ausgabe starten" : "Podcasts und Episoden sind für diesen Laborpfad noch nicht freigegeben";
          play.addEventListener("click", () => spotifyAction("favorite", { favorite_id: item.id })); actions.append(play);
        }
        actions.append(enabledLabel, catalogButton(item.name + " nach oben", "↑", () => { [items[index - 1], items[index]] = [items[index], items[index - 1]]; markCatalogDirty(); }, index === 0), catalogButton(item.name + " nach unten", "↓", () => { [items[index + 1], items[index]] = [items[index], items[index + 1]]; markCatalogDirty(); }, index === items.length - 1), catalogButton(item.name + " entfernen", "×", () => { items.splice(index, 1); markCatalogDirty(); }, false, "remove"));
        row.append(symbol, description, actions); container.append(row);
      });
    }
    updateCatalogSaveState();
  }
  function updateCatalogSaveState() { text("catalog-save-state", state.catalogDirty ? "Deine Auswahl enthält ungespeicherte Änderungen." : "Deine Auswahl ist gespeichert."); setWritingControls(); }
  function addStation(name, url) {
    if (!canWrite()) { message("Zum Hinzufügen ist eine sichere Verbindung erforderlich.", true); return false; }
    if (state.catalog.stations.length >= 64) { message("Du kannst bis zu 64 Radiosender speichern.", true); return false; }
    if (state.catalog.stations.some((item) => item.url === url)) { message("Dieser Sender ist bereits in deiner Auswahl.", true); return false; }
    state.catalog.stations.push({ id: makeId("radio"), name: name.trim(), url, enabled: true }); markCatalogDirty(); return true;
  }
  $("favorite-form").addEventListener("submit", (event) => {
    event.preventDefault(); if (!canWrite()) return;
    const favorite = spotifyLink($("favorite-link").value), name = $("favorite-name").value.trim();
    if (!favorite) { message("Bitte verwende einen Spotify-Link zu einer Playlist, einem Podcast oder einer Episode.", true); return; }
    if (!name) { message("Gib deinem Favoriten bitte einen Namen.", true); return; }
    if (state.catalog.favorites.length >= 64) { message("Du kannst bis zu 64 Favoriten speichern.", true); return; }
    if (state.catalog.favorites.some((item) => item.uri === favorite.uri)) { message("Dieser Favorit ist bereits in deiner Auswahl.", true); return; }
    state.catalog.favorites.push({ id: makeId("favorite"), name, ...favorite, enabled: true }); markCatalogDirty(); $("favorite-form").reset(); message("Favorit hinzugefügt. Speichere deine Auswahl, wenn du fertig bist.");
  });
  $("station-form").addEventListener("submit", (event) => {
    event.preventDefault();
    const url = streamUrl($("station-url").value), name = $("station-name").value.trim();
    if (!url || !name) { message("Bitte gib einen Sendernamen und eine gültige HTTP- oder HTTPS-Stream-Adresse ohne Zugangsdaten ein.", true); return; }
    if (addStation(name, url)) { $("station-form").reset(); message("Sender hinzugefügt. Speichere deine Auswahl, wenn du fertig bist."); }
  });
  $("radio-search-form").addEventListener("submit", async (event) => {
    event.preventDefault(); const query = $("radio-query").value.trim(); if (query.length < 2) return;
    const button = $("radio-search-form").querySelector("button"), container = $("radio-results"); button.disabled = true; container.hidden = false; container.textContent = "Sender werden gesucht …";
    try {
      const result = await request("/radio/search?q=" + encodeURIComponent(query)); container.replaceChildren();
      const stations = Array.isArray(result.stations) ? result.stations.slice(0, 30) : [];
      if (!stations.length) { const p = document.createElement("p"); p.textContent = "Keinen Sender gefunden. Versuche einen anderen Namen oder trage die Stream-Adresse selbst ein."; container.append(p); }
      for (const station of stations) {
        const url = streamUrl(station.url_resolved || station.url || ""); if (!url || !station.name) continue;
        const row = document.createElement("div"); row.className = "search-result";
        const description = document.createElement("div"), name = document.createElement("span"), meta = document.createElement("span"); name.className = "item-title"; name.textContent = station.name; meta.className = "item-meta"; meta.textContent = [station.country, station.codec, station.bitrate ? station.bitrate + " kbit/s" : ""].filter(Boolean).join(" · "); description.append(name, meta);
        const add = document.createElement("button"); add.type = "button"; add.className = "button secondary"; add.textContent = "+ Hinzufügen"; add.dataset.writeButton = ""; add.disabled = !canWrite() || state.busy; add.setAttribute("aria-label", station.name + " hinzufügen");
        add.addEventListener("click", () => { if (addStation(station.name, url)) { add.textContent = "Hinzugefügt"; add.disabled = true; delete add.dataset.writeButton; } });
        row.append(description, add); container.append(row);
      }
      if (!container.childElementCount) container.textContent = "Für diese Suchergebnisse ist keine verwendbare Stream-Adresse vorhanden.";
    } catch (error) { container.textContent = errorText(error); }
    finally { button.disabled = false; }
  });
  $("save-catalog").addEventListener("click", async () => {
    const all = [...state.catalog.favorites, ...state.catalog.stations];
    if (all.some((item) => !item.name?.trim() || new TextEncoder().encode(item.name).length > 80)) { message("Jeder Eintrag braucht einen kurzen Namen. Bitte ergänze fehlende Namen und kürze sehr lange Namen.", true); return; }
    try {
      await mutate("/catalog", "PUT", cloneCatalog(state.catalog), state.catalogRevision);
      state.catalogDirty = false; message("Deine Auswahl ist auf dem RotaryKnob gespeichert."); await refreshStatus({ silent: true }); updateCatalogSaveState();
    } catch (error) { message(errorText(error), true); }
  });
  $("discard-catalog").addEventListener("click", async () => { state.catalogDirty = false; await refreshStatus({ silent: true }); if (!state.online && state.status) { state.catalog = cloneCatalog(state.status.catalog); renderCatalog(); } });
  window.addEventListener("beforeunload", (event) => { if (state.updateUploading || state.catalogDirty || Object.keys(state.formRevision).length) { event.preventDefault(); event.returnValue = ""; } });
  document.addEventListener("visibilitychange", () => { if (!document.hidden) refreshStatus({ session: true, silent: true }); });
  refreshStatus({ session: true, silent: true });
  setInterval(() => { if (!document.hidden && !state.busy) refreshStatus({ silent: true }); }, 15000);
})();

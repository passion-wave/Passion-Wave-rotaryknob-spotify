/* Interactive concept only. No device/backend connection, no network requests,
 * no localStorage/cookies, no real credentials and no real OTA actions.
 * State lives only in this tab. Replace demo adapters only after the production
 * API, Spotify permission and device transport gates have been validated. */
"use strict";

const $ = (selector, root = document) => root.querySelector(selector);
const $$ = (selector, root = document) => [...root.querySelectorAll(selector)];
const defaultFavorites = [
  { id: "demo-1", name: "Slow mornings", type: "playlist", enabled: true, source: "Beispiel-Playlist", url: "" },
  { id: "demo-2", name: "Good things ahead", type: "playlist", enabled: true, source: "Beispiel-Playlist", url: "" },
  { id: "demo-3", name: "Ein guter Gedanke", type: "show", enabled: true, source: "Beispiel-Podcast", url: "" },
];
const state = {
  favorites: defaultFavorites.map((item) => ({ ...item })),
  radios: [{ id: "radio-demo", name: "Dein Beispielsender", url: "https://stream.example.org/radio.mp3" }],
  wifiConnected: false,
  output: "living",
  otaRunning: false,
};
let toastTimer;
let nextId = 1;
let editedEntry = null;

function notify(message) {
  const toast = $("#toast");
  clearTimeout(toastTimer);
  toast.textContent = message;
  toast.hidden = false;
  toastTimer = setTimeout(() => { toast.hidden = true; }, 4200);
}

function showTab(name, focus = false) {
  const target = $(`[data-tab="${name}"]`);
  if (!target) return;
  $$("[data-tab]").forEach((tab) => {
    const selected = tab === target;
    tab.classList.toggle("active", selected);
    tab.setAttribute("aria-selected", String(selected));
    tab.tabIndex = selected ? 0 : -1;
    $("#" + tab.getAttribute("aria-controls")).hidden = !selected;
  });
  $("#breadcrumb-page").textContent = target.textContent.replace(/[◉▤♫⚙↗]/gu, "").trim();
  if (focus) target.focus();
}

$$("[data-tab]").forEach((tab, index, tabs) => {
  tab.addEventListener("click", () => showTab(tab.dataset.tab));
  tab.addEventListener("keydown", (event) => {
    let next = null;
    if (["ArrowRight", "ArrowDown"].includes(event.key)) next = (index + 1) % tabs.length;
    if (["ArrowLeft", "ArrowUp"].includes(event.key)) next = (index + tabs.length - 1) % tabs.length;
    if (event.key === "Home") next = 0;
    if (event.key === "End") next = tabs.length - 1;
    if (next !== null) { event.preventDefault(); showTab(tabs[next].dataset.tab, true); }
  });
});
$$("[data-go]").forEach((button) => button.addEventListener("click", () => {
  showTab(button.dataset.go);
  $("#panel-" + button.dataset.go).focus({ preventScroll: true });
  window.scrollTo({ top: 0, behavior: "smooth" });
}));
$(".brand").addEventListener("click", (event) => { event.preventDefault(); showTab("einrichtung"); });

function element(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function iconButton(symbol, label, action, className = "") {
  const button = element("button", "icon-button " + className, symbol);
  button.type = "button";
  button.setAttribute("aria-label", label);
  button.title = label;
  button.addEventListener("click", action);
  return button;
}

function renderFavorites() {
  const list = $("#favorites-list");
  list.replaceChildren();
  state.favorites.forEach((favorite, index) => {
    const row = element("article", "favorite-row");
    const art = element("div", "favorite-art", favorite.type === "playlist" ? "≋" : "◌");
    art.setAttribute("aria-hidden", "true");
    const details = element("div", "favorite-details");
    details.append(element("strong", "", favorite.name));
    details.append(element("p", "", favorite.source));
    const actions = element("div", "favorite-actions");
    const enabled = element("input", "switch");
    enabled.type = "checkbox";
    enabled.checked = favorite.enabled;
    enabled.setAttribute("aria-label", favorite.name + " am Gerät anzeigen");
    const status = element("span", "pill " + (favorite.enabled ? "green" : "neutral"), favorite.enabled ? "Freigeschaltet" : "Ausgeblendet");
    enabled.addEventListener("change", () => {
      favorite.enabled = enabled.checked;
      status.className = "pill " + (enabled.checked ? "green" : "neutral");
      status.textContent = enabled.checked ? "Freigeschaltet" : "Ausgeblendet";
      notify("Demo-Auswahl geändert.");
    });
    const moveUp = iconButton("↑", favorite.name + " nach oben verschieben", () => {
      [state.favorites[index - 1], state.favorites[index]] = [state.favorites[index], state.favorites[index - 1]];
      renderFavorites();
      $$(".favorite-row", list)[index - 1].querySelector("input").focus();
    });
    moveUp.disabled = index === 0;
    const remove = iconButton("×", favorite.name + " entfernen", () => {
      state.favorites = state.favorites.filter((item) => item.id !== favorite.id);
      renderFavorites();
      notify("Aus der Demo-Auswahl entfernt.");
    }, "danger");
    const edit = iconButton("✎", favorite.name + " bearbeiten", () => editEntry("favorite", favorite));
    actions.append(enabled, status, edit, moveUp, remove);
    row.append(art, details, actions);
    list.append(row);
  });
  if (!state.favorites.length) list.append(element("p", "empty-note", "Deine Auswahl ist leer. Füge eine Playlist oder einen Podcast hinzu."));
  updateCount();
}

function renderRadios() {
  const list = $("#radio-list");
  list.replaceChildren();
  state.radios.forEach((radio) => {
    const row = element("article", "radio-row");
    const art = element("div", "favorite-art", "◉");
    art.setAttribute("aria-hidden", "true");
    const details = element("div", "favorite-details");
    details.append(element("strong", "", radio.name), element("p", "", radio.url), element("p", "", "Gespeichert · Radio-Ausgang fehlt"));
    row.append(art, details, iconButton("✎", radio.name + " bearbeiten", () => editEntry("radio", radio)), iconButton("×", radio.name + " entfernen", () => {
      state.radios = state.radios.filter((item) => item.id !== radio.id);
      renderRadios();
      notify("Radiosender aus der Vorschau entfernt.");
    }, "danger"));
    list.append(row);
  });
  if (!state.radios.length) list.append(element("p", "empty-note", "Noch keine Radiosender eingetragen."));
  updateCount();
}

function updateCount() { $("#content-count").textContent = String(state.favorites.length + state.radios.length); }

function parseSpotifyLink(value) {
  let parsed;
  try { parsed = new URL(value.trim()); } catch { return null; }
  const match = parsed.pathname.match(/^\/(playlist|show|episode)\/([A-Za-z0-9]{22})\/?$/);
  if (parsed.protocol !== "https:" || parsed.hostname !== "open.spotify.com" || parsed.port || parsed.username || parsed.password || !match) return null;
  return { type: match[1], canonical: `https://open.spotify.com/${match[1]}/${match[2]}` };
}

$("#favorite-form").addEventListener("submit", (event) => {
  event.preventDefault();
  const error = $("#favorite-error");
  error.textContent = "";
  const name = $("#favorite-title").value.trim();
  const parsed = parseSpotifyLink($("#favorite-url").value);
  if (!parsed) {
    error.textContent = "Erlaubt sind https://open.spotify.com-Links zu einer Playlist, einem Podcast oder einer Episode.";
    return;
  }
  if (!name) { error.textContent = "Bitte einen Anzeigenamen eingeben."; return; }
  const canonical = parsed.canonical;
  if (state.favorites.some((item) => item.url === canonical)) { error.textContent = "Dieser Inhalt ist bereits in deiner Auswahl."; return; }
  const typeLabels = { playlist: "Playlist", show: "Podcast", episode: "Episode" };
  state.favorites.push({ id: "favorite-" + nextId++, name, type: parsed.type, url: canonical, enabled: true, source: typeLabels[parsed.type] + " · manuell eingetragen, nicht geprüft" });
  renderFavorites();
  event.target.reset();
  $("#favorite-title").focus();
  notify("Zur Demo-Auswahl hinzugefügt. Der Spotify-Inhalt wurde nicht geprüft.");
});

// Preview-only validation. Real firmware must resolve DNS and re-check every
// redirect against the full public-address policy before making any requests.
function acceptableRadioUrl(value) {
  let url;
  try { url = new URL(value); } catch { return null; }
  if (!["http:", "https:"].includes(url.protocol) || url.username || url.password) return null;
  const host = url.hostname.toLowerCase();
  if (!host.includes(".") || host.endsWith(".local") || host.endsWith(".localhost") || host.endsWith(".internal") || host.includes(":")) return null;
  if (/^\d+\.\d+\.\d+\.\d+$/.test(host)) {
    const octets = host.split(".").map(Number);
    const [a, b] = octets;
    if (octets.some((part) => part > 255) || a === 0 || a === 10 || a === 127 || a >= 224 || (a === 169 && b === 254) || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) || (a === 100 && b >= 64 && b <= 127)) return null;
  }
  url.hash = "";
  return url.href;
}

$("#radio-form").addEventListener("submit", (event) => {
  event.preventDefault();
  const name = $("#radio-title").value.trim();
  const url = acceptableRadioUrl($("#radio-url").value.trim());
  const error = $("#radio-error");
  error.textContent = "";
  if (!name) { error.textContent = "Bitte einen Sendernamen eingeben."; return; }
  if (!url) { error.textContent = "Bitte eine öffentliche HTTP(S)-Streamadresse ohne Zugangsdaten eingeben. Lokale Adressen sind in dieser Vorschau gesperrt."; return; }
  if (state.radios.some((radio) => radio.url === url)) { error.textContent = "Diese Streamadresse ist bereits gespeichert."; return; }
  state.radios.push({ id: "radio-" + nextId++, name, url });
  renderRadios();
  event.target.reset();
  $("#radio-title").focus();
  notify("Sender in der Vorschau gespeichert. Es wurde kein Stream aufgerufen.");
});

function editEntry(kind, entry) {
  editedEntry = { kind, id: entry.id };
  $("#edit-title").textContent = kind === "radio" ? "Radiosender bearbeiten" : "Spotify-Auswahl bearbeiten";
  $("#edit-name").value = entry.name;
  $("#edit-url").value = entry.url;
  $("#edit-url").required = kind === "radio" || Boolean(entry.url);
  $("#edit-hint").textContent = kind === "radio" ? "Öffentliche HTTP(S)-Streamadresse. Die Vorschau prüft keine Erreichbarkeit." : entry.url ? "Playlist, Podcast oder Episode. Es erfolgt kein Abruf bei Spotify." : "Dieses Beispiel hat keinen echten Spotify-Link. Du kannst einen Link ergänzen oder nur den Namen ändern.";
  $("#edit-error").textContent = "";
  $("#edit-dialog").showModal();
}
$("#edit-form").addEventListener("submit", (event) => {
  event.preventDefault();
  if (!editedEntry) return;
  const list = editedEntry.kind === "radio" ? state.radios : state.favorites;
  const entry = list.find((item) => item.id === editedEntry.id);
  if (!entry) return;
  const name = $("#edit-name").value.trim();
  const raw = $("#edit-url").value.trim();
  const error = $("#edit-error");
  error.textContent = "";
  if (!name) { error.textContent = "Bitte einen Anzeigenamen eingeben."; return; }
  const parsed = editedEntry.kind === "radio" ? acceptableRadioUrl(raw) : parseSpotifyLink(raw);
  if (!parsed && (editedEntry.kind === "radio" || raw || entry.url)) { error.textContent = "Bitte eine gültige, für diesen Inhalt erlaubte Adresse eingeben."; return; }
  const url = editedEntry.kind === "radio" ? parsed : parsed?.canonical || "";
  if (url && list.some((item) => item.id !== entry.id && item.url === url)) { error.textContent = "Diese Adresse ist bereits in der Auswahl."; return; }
  entry.name = name;
  entry.url = url;
  if (editedEntry.kind === "favorite" && parsed) {
    entry.type = parsed.type;
    entry.source = ({ playlist: "Playlist", show: "Podcast", episode: "Episode" })[parsed.type] + " · manuell eingetragen, nicht geprüft";
  }
  renderFavorites();
  renderRadios();
  $("#edit-dialog").close();
  notify("Eintrag in der Vorschau geändert.");
});
$(".edit-close").addEventListener("click", () => $("#edit-dialog").close());

$("#simulate-wifi").addEventListener("click", () => {
  state.wifiConnected = true;
  $("#wifi-pill").textContent = "Demo verbunden";
  $("#wifi-pill").className = "pill green";
  $("#simulate-wifi").textContent = "Erneut simulieren ✓";
  notify("WLAN-Verbindung simuliert. Deine echte Netzwerkverbindung bleibt unverändert.");
});

function selectOutput(value) {
  state.output = value;
  $("#onboarding-output").value = value;
  $$("input[name=output]").forEach((input) => { input.checked = input.value === value; });
}
$("#onboarding-output").addEventListener("change", (event) => selectOutput(event.target.value));
$$("input[name=output]").forEach((input) => input.addEventListener("change", () => {
  selectOutput(input.value);
  notify("Beispielausgabe geändert. Es wurde kein Lautsprecher gesteuert.");
}));

function updateVolumeControls() {
  const maximum = Number($("#volume-limit").value);
  const start = $("#preview-volume");
  start.max = String(maximum);
  if (Number(start.value) > maximum) start.value = String(maximum);
  $("#volume-limit-value").textContent = maximum + " %";
  $("#preview-volume-value").textContent = start.value + " %";
  $("#preview-volume-max").textContent = maximum + " %";
}
$("#volume-limit").addEventListener("input", updateVolumeControls);
$("#preview-volume").addEventListener("input", updateVolumeControls);
$("#brightness").addEventListener("input", (event) => { $("#brightness-value").textContent = event.target.value + " %"; });
$$("[data-save]").forEach((button) => button.addEventListener("click", () => notify("Demo-Einstellungen übernommen — nur für diesen Tab.")));
$("#device-form").addEventListener("submit", (event) => {
  event.preventDefault();
  const name = $("#device-name").value.trim();
  if (!name) { $("#device-name").focus(); notify("Bitte einen Gerätenamen eingeben."); return; }
  $("#sidebar-device-name").textContent = name;
  notify("Gerätename und Einstellungen in der Vorschau übernommen.");
});

function openInfo(title, introduction, steps, closing) {
  $("#dialog-title").textContent = title;
  const body = $("#dialog-body");
  body.replaceChildren(element("p", "", introduction));
  const list = element("ol");
  steps.forEach((step) => list.append(element("li", "", step)));
  body.append(list);
  if (closing) body.append(element("p", "", closing));
  $("#info-dialog").showModal();
}
$("#linking-info").addEventListener("click", () => openInfo("Einmal verknüpfen. Dann einfach hören.", "Der genaue Produktablauf ist erst nach Spotify-Partnerfreigabe verbindlich.", ["WLAN am RotaryKnob einrichten.", "Den von Spotify freigegebenen Verknüpfungsweg öffnen und bei Spotify anmelden.", "Das Konto am Gerät bestätigen und eine Connect-Ausgabe auswählen.", "Playlists und Podcasts für den RotaryKnob freischalten."], "Diese Vorschau startet keine Anmeldung und erfragt keine Spotify-Zugangsdaten."));
$("#upload-info").addEventListener("click", () => openInfo("Ein Paket für beide Prozessoren.", "Der Datei-Import wird später in der lokalen Gerätewebsite angeboten.", ["Ein signiertes PassionWave-Gesamtpaket auswählen.", "Hardware, Signatur, Imagegrößen und Versionskompatibilität prüfen.", "Zuerst den Begleitprozessor, danach Display & Steuerung aktualisieren.", "Ergebnisse bestätigen oder einen fehlgeschlagenen Teil auf die vorherige Version zurücksetzen."], "Die Vorschau nimmt keine Datei entgegen. Ein bloßer Prüfsummenvergleich ersetzt keine Signaturprüfung."));
$$(".dialog-close,.dialog-ok", $("#info-dialog")).forEach((button) => button.addEventListener("click", () => $("#info-dialog").close()));

const pause = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
function phase(name, status, text) {
  const item = $(`[data-phase="${name}"]`);
  item.className = status;
  $("b", item).textContent = text;
}
function resetPhases() {
  $$(".ota-steps li").forEach((item, index) => { item.className = ""; $("b", item).textContent = index === 0 ? "Bereit" : "Wartet"; });
}
$("#start-ota").addEventListener("click", async () => {
  if (state.otaRunning) return;
  state.otaRunning = true;
  const fail = $("#simulate-failure").checked;
  const controls = [$("#start-ota"), $("#simulate-failure"), $("#update-channel"), $("#reset-demo")];
  controls.forEach((control) => { control.disabled = true; });
  resetPhases();
  $("#companion-version").textContent = "Demo 0.1.0";
  $("#display-version").textContent = "Demo 0.1.0";
  const status = $("#ota-status");
  try {
    phase("verify", "running", "Prüft …");
    status.textContent = "Simulation: Paket, Signatur und erlaubte Zwischenversionen prüfen.";
    await pause(950);
    phase("verify", "done", "Simuliert ✓");
    phase("companion", "running", "Aktualisiert …");
    status.textContent = "Simulation: Begleitprozessor in den inaktiven App-Slot schreiben und testen.";
    await pause(1350);
    if (fail) {
      phase("companion", "failed", "Zurückgesetzt");
      status.textContent = "Simulierter Abbruch: Begleitprozessor auf Demo 0.1.0 zurückgesetzt. Display & Steuerung bleiben unverändert; ein erneuter Versuch ist möglich.";
      notify("Fehlerfall simuliert. Das S3-Update wurde nicht begonnen.");
      return;
    }
    phase("companion", "done", "Simuliert ✓");
    $("#companion-version").textContent = "Demo 0.2.0";
    phase("display", "running", "Aktualisiert …");
    status.textContent = "Simulation: Display & Steuerung aktualisieren und nach Neustart selbst testen.";
    await pause(1350);
    phase("display", "done", "Simuliert ✓");
    $("#display-version").textContent = "Demo 0.2.0";
    phase("complete", "running", "Bestätigt …");
    await pause(650);
    phase("complete", "done", "Simuliert ✓");
    status.textContent = "Simulation abgeschlossen: Beide Prozessoren melden Demo 0.2.0. Es wurde keine Firmware heruntergeladen oder installiert.";
    notify("Update-Ablauf erfolgreich simuliert.");
  } finally {
    state.otaRunning = false;
    controls.forEach((control) => { control.disabled = false; });
    $("#start-ota").textContent = "Simulation erneut starten ↗";
  }
});

$("#reset-demo").addEventListener("click", () => {
  if (state.otaRunning) return;
  window.location.reload();
});
renderFavorites();
renderRadios();
updateVolumeControls();

'use strict';
const $ = (id) => document.getElementById(id);
let csrf = '', busy = false, last = null;
const labels = {
  waiting_network: 'Der Knob wartet auf seine WLAN-Verbindung.',
  waiting_clock: 'Der Knob synchronisiert die Uhrzeit für eine sichere Verbindung.',
  authorizing: 'Das Gerät prüft die Spotify-Anmeldung …',
  reauth_required: 'Bitte Spotify erneut verbinden.',
  rate_limited: 'Spotify bittet um eine Pause. Das Gerät versucht es später erneut.',
  error: 'Das Gerät meldet ein Spotify-Problem. Bitte erneut prüfen.',
  suspended: 'Spotify ist auf dem Gerät vorübergehend pausiert.',
  disconnecting: 'Die bestehende Verknüpfung wird getrennt …',
};
async function api(path, body) {
  const options = {credentials: 'same-origin', cache: 'no-store', signal: AbortSignal.timeout(22000)};
  if (body !== undefined) Object.assign(options, {method: 'POST', headers: {'Content-Type': 'application/json', 'X-CSRF-Token': csrf}, body: JSON.stringify(body)});
  const response = await fetch(path, options);
  const data = await response.json();
  if (!response.ok) throw new Error(data.message || 'Einrichtungsseite neu laden und erneut versuchen.');
  return data;
}
function message(text) { $('notice').textContent = text; $('notice').hidden = !text; }
function render(state) {
  last = state;
  const spotify = state.spotify || {};
  const ready = !state.error && !state.pending && state.setup_open && spotify.linked === true && spotify.state === 'ready' && ['none', 'confirmed'].includes(state.authorization_status);
  $('indicator').classList.toggle('ready', ready);
  $('authorize').disabled = busy || !!state.error || !state.setup_open || !state.lab_enabled || state.pending;
  $('authorize').textContent = spotify.linked ? 'Spotify erneut verbinden' : 'Mit Spotify verbinden';
  $('cancel').hidden = !state.pending;
  $('cancel').disabled = busy;
  $('choose').disabled = busy || state.pending;
  $('status').textContent = state.error ? state.message : !state.setup_open ? 'Einrichtung am Knob öffnen' : !state.lab_enabled ? 'Spotify ist noch nicht freigeschaltet' : ready ? 'Spotify ist auf deinem Knob verbunden' : state.pending ? 'Anmeldung bei Spotify läuft …' : state.authorization_status === 'failed' ? 'Neue Anmeldung nicht bestätigt' : labels[spotify.state] || (spotify.linked ? 'Spotify ist verknüpft · Verbindung wird geprüft' : 'Bereit für deine Spotify-Anmeldung');
  $('detail').textContent = ready ? 'Der Knob ist bereit. Wähle einen Spotify Connect-Lautsprecher auf dem Gerät.' : labels[spotify.state] || (state.setup_open ? 'USB verbunden. Richte zuerst über den QR-Code am Display dein Heim-WLAN ein. Danach hier Spotify verbinden.' : 'RotaryKnob per USB anschließen. Das Display 3 Sekunden berühren, um die Einrichtung zu öffnen.');
  const ports = state.ports || [];
  $('ports').hidden = ports.length < 2 && state.error !== 'select_port';
  const previous = $('port').value;
  $('port').replaceChildren(...ports.map(item => {
    const option = document.createElement('option'); option.value = item.port; option.textContent = item.label; return option;
  }));
  if (ports.some(item => item.port === previous)) $('port').value = previous;
  else if (state.selected_port) $('port').value = state.selected_port;
  message(state.notice || '');
}
async function refresh() {
  if (busy) return;
  busy = true; $('refresh').disabled = true;
  try {
    if (!csrf) csrf = (await api('/api/session')).csrf;
    render(await api('/api/status'));
  } catch (_) {
    last = null;
    $('indicator').classList.remove('ready');
    $('authorize').disabled = true;
    $('status').textContent = 'Einrichtungshelfer nicht erreichbar';
    message('Helfer geöffnet lassen und erneut prüfen. Die Spotify-Verbindung wurde hier nicht bestätigt.');
  } finally { busy = false; $('refresh').disabled = false; if (last) render(last); }
}
async function action(path, body, redirect = false) {
  if (busy) return;
  busy = true; $('authorize').disabled = true; $('cancel').disabled = true;
  try {
    const result = await api(path, body);
    if (redirect) { window.location.assign(result.authorization_url); return; }
    busy = false; await refresh();
  } catch (error) { message(error.message); }
  finally { busy = false; if (last) $('authorize').disabled = !!last.error || !last.setup_open || !last.lab_enabled || last.pending; $('cancel').disabled = false; }
}
$('refresh').addEventListener('click', refresh);
$('authorize').addEventListener('click', () => action('/api/authorize', {}, true));
$('cancel').addEventListener('click', () => action('/api/cancel', {}));
$('choose').addEventListener('click', () => action('/api/port', {port: $('port').value}));
refresh();
setInterval(refresh, 3000);

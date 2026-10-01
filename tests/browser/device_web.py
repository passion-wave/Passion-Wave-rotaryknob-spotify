#!/usr/bin/env python3
"""Real Chromium checks of firmware/web, with explicitly simulated API responses.

These check browser behavior, not ESP HTTP authentication or physical hardware.
Run with Playwright 1.58.0 and Chromium installed; no personal profile is used.
"""
from __future__ import annotations
import argparse
import copy
import gzip
import json
from pathlib import Path
import tempfile
from urllib.parse import urlsplit
from playwright.sync_api import sync_playwright

WEB = Path(__file__).resolve().parents[2] / 'firmware/web'
BASE = {
    'device': {'name': 'PassionWave', 'version': 'browser-fixture', 'hardware': 'fixture', 'peer_connected': True},
    'network': {'connected': True, 'connecting': False, 'setup_open': True, 'ip': '192.0.2.10', 'ssid': 'Testnetz'},
    'capabilities': {'spotify': False, 'radio_playback': False, 'pair_ota': False, 'secure_lan_write': False},
    'secure_write': True, 'config_revision': 1,
    'settings': {'name': 'PassionWave', 'brightness': 65, 'haptic': True,
                 'weather_enabled': False, 'latitude': None, 'longitude': None,
                 'timezone': 'Europe/Berlin', 'avatar_enabled': True, 'avatar_blond': False,
                 'screensaver_mode': 'weather_photo'},
    'catalog': {'favorites': [], 'stations': []},
    'weather': {'state': 'unconfigured', 'attribution': 'SIMULATED TEST RESPONSE', 'days': []},
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--screenshots', type=Path, default=Path(tempfile.gettempdir()) / 'pw-web-preview')
    args = parser.parse_args()
    args.screenshots.mkdir(parents=True, exist_ok=True)
    state = copy.deepcopy(BASE)
    writes, errors, spotify_calls = [], [], []
    spotify = {
        'enabled': True, 'linked': True, 'product_approved': False, 'state': 'ready', 'error': 'none',
        'session': 19, 'selection_generation': 3, 'revision': 7, 'last_request_id': 0, 'last_command_state': 'none',
        'selected': {'id': 'fixture-roam', 'name': 'Sonos Roam', 'present': True, 'restricted': False, 'supports_volume': True, 'volume_known': True, 'volume_percent': 35},
        'playback': {'known': True, 'is_playing': False, 'title': 'Nur eine Browserfixture', 'artist': 'Simulierter Interpret', 'device_id': 'fixture-roam', 'device_name': 'Sonos Roam', 'position_known': True, 'position_ms': 60000, 'duration_ms': 180000},
        'actions': {'play': True, 'pause': True, 'previous': True, 'next': True, 'volume': True},
    }
    spotify_devices = [
        {'id': 'fixture-roam', 'name': 'Sonos Roam', 'restricted': False, 'supports_volume': True},
        {'id': 'fixture-move', 'name': 'Sonos Move', 'restricted': False, 'supports_volume': False},
        {'id': 'fixture-restricted', 'name': 'Nicht steuerbar', 'restricted': True, 'supports_volume': False},
    ]
    conflict = False
    hold_snapshots = False
    held_snapshots = []

    def route(request):
        nonlocal state
        req = request.request
        parsed = urlsplit(req.url)
        if parsed.hostname != '192.0.2.1':
            errors.append('Unexpected external request: ' + parsed.hostname)
            request.abort()
            return
        path = parsed.path
        response, status = None, 200
        if path == '/api/v1/session':
            response = {'csrf': 'browser-fixture-only', 'secure_write': state['secure_write']}
        elif path == '/api/v1/status':
            response = state
        elif path == '/api/v1/spotify/snapshot':
            assert state['secure_write'], 'Private Spotify endpoint requested from LAN-only fixture'
            response = spotify
            if hold_snapshots:
                held_snapshots.append((request, json.dumps(response)))
                return
        elif path == '/api/v1/spotify/devices':
            assert state['secure_write'], 'Private device list requested from LAN-only fixture'
            response = {'session': spotify['session'], 'devices': spotify_devices, 'truncated': False}
        elif path.startswith('/api/v1/spotify/') and req.method == 'POST':
            assert state['secure_write'] and req.headers.get('x-csrf-token') == 'browser-fixture-only'
            assert req.headers.get('if-match') == str(state['config_revision'])
            body = req.post_data_json
            spotify_calls.append((path, body))
            if path.endswith('/select'):
                assert body['session'] == spotify['session']
                device = next(d for d in spotify_devices if d['id'] == body['device_id'])
                spotify['selected'] = {**device, 'present': True, 'volume_known': device['supports_volume'], 'volume_percent': 35}
                spotify['selection_generation'] += 1
                spotify['actions']['volume'] = device['supports_volume']
            elif path.endswith('/action') and body['action'] != 'refresh':
                assert body['device_id'] == spotify['selected']['id']
                assert body['session'] == spotify['session']
                assert body['selection_generation'] == spotify['selection_generation']
                assert 'uri' not in body and 'token' not in body
                spotify['last_command_state'] = 'uncertain' if body['action'] == 'next' else 'accepted'
                # Accepted is deliberately not an observed playback state change.
            response, status = {'accepted': True, 'request_id': len(spotify_calls)}, 202
        elif path == '/api/v1/settings' and req.method == 'PATCH':
            assert req.headers.get('x-csrf-token') == 'browser-fixture-only'
            writes.append(req.post_data_json)
            if conflict or req.headers.get('if-match') != str(state['config_revision']):
                response, status = {'error': {'code': 'revision', 'message': 'Changed'}}, 409
            else:
                state['settings'].update(req.post_data_json)
                state['config_revision'] += 1
                response = {'config_revision': state['config_revision']}
        elif path == '/places-de.json':
            request.fulfill(body=gzip.decompress((WEB / 'places-de.json.gz').read_bytes()), content_type='application/json')
            return
        elif path in ('/', '/app.js', '/style.css'):
            name = 'index.html' if path == '/' else path[1:]
            kind = {'index.html': 'text/html', 'app.js': 'text/javascript', 'style.css': 'text/css'}[name]
            request.fulfill(body=(WEB / name).read_bytes(), content_type=kind)
            return
        else:
            response, status = {'error': {'code': 'fixture_unknown'}}, 404
        request.fulfill(status=status, body=json.dumps(response), content_type='application/json')

    with sync_playwright() as playwright:
        browser = playwright.chromium.launch()
        page = browser.new_page(viewport={'width': 390, 'height': 844}, device_scale_factor=1)
        page.on('pageerror', lambda error: errors.append(str(error)))
        page.route('**/*', route)
        page.goto('http://192.0.2.1/')
        page.locator('#wifi-ssid').wait_for(state='attached')
        page.wait_for_function('!document.querySelector("#wifi-ssid").disabled && !document.querySelector("#wifi-ssid").closest("fieldset").disabled')
        # The connected-device status intentionally collapses WLAN setup.
        # Wait for initialization, then open it as a user would; observing the
        # initial HTML before that response is a race on slower CI runners.
        assert not page.locator('#wifi-details').evaluate('(element) => element.open')
        page.locator('#wifi-summary').click()
        page.locator('#wifi-ssid').wait_for(state='visible')
        assert page.locator('#security-notice').is_hidden()
        page.screenshot(path=str(args.screenshots / 'mobile-overview.png'), full_page=True)

        for area in ['content', 'weather', 'device']:
            page.locator(f'[data-page="{area}"]').click()
            assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
            page.screenshot(path=str(args.screenshots / f'mobile-{area}.png'), full_page=True)
        page.locator('[data-page="weather"]').click()
        page.locator('#weather-place').fill('10115')
        page.locator('#place-results button').first.wait_for()
        assert page.locator('#place-results button').count() <= 12
        page.locator('#place-results button').first.click()
        assert float(page.locator('#weather-latitude').input_value()) > 52
        page.locator('#weather-enabled').check()
        page.locator('#weather-form button[type="submit"]').click()
        page.wait_for_function('document.querySelector("#message").textContent.includes("aktiviert")')
        assert writes[-1]['weather_enabled'] is True and writes[-1]['latitude'] > 52

        page.locator('[data-page="device"]').click()
        page.locator('#device-name').fill('Mein neuer Name')
        conflict = True
        page.locator('#settings-form button[type="submit"]').click()
        page.locator('#reload-config').wait_for(state='visible')
        assert page.locator('#device-name').input_value() == 'Mein neuer Name'
        assert state['settings']['name'] == 'PassionWave'

        # Lab capability exposes a real picker/player; all backend replies here remain simulated.
        conflict = False
        state['capabilities']['spotify'] = True
        state['catalog']['favorites'] = [
            {'id': 'saved-playlist', 'kind': 'spotify_playlist', 'name': 'Meine Playlist', 'uri': 'spotify:playlist:0123456789012345678901', 'enabled': True},
            {'id': 'saved-show', 'kind': 'spotify_show', 'name': 'Mein Podcast', 'uri': 'spotify:show:0123456789012345678901', 'enabled': True},
            {'id': 'disabled-playlist', 'kind': 'spotify_playlist', 'name': 'Ausgeblendet', 'uri': 'spotify:playlist:1123456789012345678901', 'enabled': False},
        ]
        page.reload()
        page.locator('[data-page="overview"]').click()
        page.locator('#music-player').wait_for(state='visible')
        page.wait_for_function('document.querySelector("#spotify-title").textContent === "Nur eine Browserfixture"')
        assert page.locator('#spotify-play').inner_text() == 'Abspielen'
        page.locator('#spotify-play').click()
        page.wait_for_function('!document.querySelector("#spotify-play").disabled')
        assert spotify_calls[-1][1]['action'] == 'play' and spotify_calls[-1][1]['device_id'] == 'fixture-roam'
        assert page.locator('#spotify-play').inner_text() == 'Abspielen', '202 must not fake playing'
        count = len(spotify_calls)
        # Keep a read from before selection in flight. It must not restore the
        # old target or re-enable Play while the new target is still unconfirmed.
        hold_snapshots = True
        page.locator('#spotify-refresh').click()
        page.wait_for_function('!document.querySelector("#spotify-device").disabled')
        page.wait_for_timeout(100)
        assert held_snapshots
        count = len(spotify_calls)
        page.locator('#spotify-device').select_option('fixture-move')
        page.wait_for_function('document.querySelector("#spotify-play").disabled')
        page.wait_for_timeout(100)
        assert page.locator('#spotify-play').is_disabled()
        assert len(spotify_calls) == count + 1 and spotify_calls[-1][0].endswith('/select')
        hold_snapshots = False
        for held, old_response in held_snapshots:
            held.fulfill(status=200, body=old_response, content_type='application/json')
        held_snapshots.clear()
        page.wait_for_function('!document.querySelector("#spotify-device").disabled')
        assert len(spotify_calls) == count + 1 and spotify_calls[-1][0].endswith('/select'), 'Selection must not transfer/play'
        assert page.locator('#spotify-volume-field').is_hidden(), 'Unsupported target must hide volume'
        assert page.locator('#spotify-device option[value="fixture-restricted"]').is_disabled()
        page.locator('#spotify-play').click()
        page.wait_for_function('!document.querySelector("#spotify-play").disabled')
        assert spotify_calls[-1][1]['device_id'] == 'fixture-move'
        assert spotify_calls[-1][1]['selection_generation'] == 4
        assert page.locator('#spotify-play').inner_text() == 'Abspielen'
        spotify['playback'].update(is_playing=True, device_id='fixture-move', device_name='Sonos Move')
        page.locator('#spotify-refresh').click()
        page.wait_for_function('document.querySelector("#spotify-play").textContent === "Pause"')
        page.locator('#spotify-next').click()
        page.wait_for_function('document.querySelector("#spotify-state").textContent.includes("Bestätigung fehlt")')
        count = sum(body.get('action') == 'next' for _, body in spotify_calls)
        page.wait_for_timeout(3300)
        assert sum(body.get('action') == 'next' for _, body in spotify_calls) == count, 'Uncertain skip must not retry'
        assert page.evaluate('document.documentElement.scrollWidth <= innerWidth')
        page.screenshot(path=str(args.screenshots / 'mobile-spotify-lab.png'), full_page=True)
        page.locator('[data-page="content"]').click()
        assert page.locator('[data-spotify-favorite="saved-show"]').is_disabled()
        assert page.locator('[data-spotify-favorite="disabled-playlist"]').is_disabled()
        page.locator('[data-spotify-favorite="saved-playlist"]').click()
        page.wait_for_function('!document.querySelector("[data-spotify-favorite=saved-playlist]").disabled')
        assert spotify_calls[-1][1]['favorite_id'] == 'saved-playlist' and 'uri' not in spotify_calls[-1][1]
        page.screenshot(path=str(args.screenshots / 'mobile-spotify-favorites.png'), full_page=True)

        # A subsequent LAN-only session must not retain editable protected fields.
        state = copy.deepcopy(BASE)
        state['secure_write'] = False
        state['capabilities']['spotify'] = True
        for key in ['settings', 'catalog', 'weather', 'config_revision']:
            state.pop(key)
        page.reload()
        page.locator('#security-notice').wait_for(state='visible')
        assert page.locator('#music-player').is_hidden()
        assert page.locator('#device-name').is_disabled()
        assert page.locator('#wifi-ssid').is_disabled()
        assert not page.evaluate('Object.keys(localStorage).length || Object.keys(sessionStorage).length')
        assert not errors, errors
        browser.close()
    print('Browser fixtures: mobile layouts, local place selection, CSRF/revision requests, conflict preservation, Spotify explicit targets/no optimistic playback/no skip retry and read-only session: PASS')
    print('No physical device or real backend acceptance claimed.')


if __name__ == '__main__':
    main()

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
    writes, errors = [], []
    conflict = False

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

        # A subsequent LAN-only session must not retain editable protected fields.
        state = copy.deepcopy(BASE)
        state['secure_write'] = False
        for key in ['settings', 'catalog', 'weather', 'config_revision']:
            state.pop(key)
        page.reload()
        page.locator('#security-notice').wait_for(state='visible')
        assert page.locator('#device-name').is_disabled()
        assert page.locator('#wifi-ssid').is_disabled()
        assert not page.evaluate('Object.keys(localStorage).length || Object.keys(sessionStorage).length')
        assert not errors, errors
        browser.close()
    print('Browser fixtures: mobile layouts, local place selection, CSRF/revision requests, conflict preservation and read-only session: PASS')
    print('No physical device or real backend acceptance claimed.')


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Real Chromium + actual helper HTTP; isolated fake USB and intercepted Spotify."""
import argparse
from pathlib import Path
import sys
import tempfile
from urllib.parse import parse_qs, urlencode, urlsplit
from playwright.sync_api import sync_playwright

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_helper import FakeDevice, running_server
import helper


class NetworkDevice(FakeDevice):
    """Network availability is independent of whether Spotify is linked."""
    def __init__(self):
        super().__init__()
        self.network_connected = False

    def request(self, method, **values):
        response = super().request(method, **values)
        if "spotify" in response:
            response["spotify"]["connected"] = self.network_connected
        return response


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--screenshots", type=Path, default=Path(tempfile.gettempdir()) / "pw-spotify-setup-preview")
    args = parser.parse_args()
    args.screenshots.mkdir(parents=True, exist_ok=True)
    device = NetworkDevice()
    device.setup = False
    app = helper.SetupApp(device)
    failures, callbacks = [], []
    with running_server(app), sync_playwright() as playwright:
        browser = playwright.chromium.launch()
        context = browser.new_context(viewport={"width": 390, "height": 844})
        page = context.new_page()
        page.on("pageerror", lambda error: failures.append(str(error)))

        def network(route):
            request = route.request
            parsed = urlsplit(request.url)
            if parsed.netloc == helper.HOST:
                if parsed.path == "/callback":
                    callbacks.append(dict(request.headers))
                route.continue_()
            elif parsed.netloc == "accounts.spotify.com" and parsed.path == "/authorize":
                # This is an intercepted fixture, never a real Spotify login.
                state = parse_qs(parsed.query)["state"][0]
                callback = helper.REDIRECT + "?" + urlencode({"state": state, "code": "browser-fixture-only"})
                route.fulfill(content_type="text/html", body=f'<!doctype html><a id="finish" href="{callback}">Simulierte Anmeldung abschließen</a>')
            else:
                failures.append("Unexpected network destination")
                route.abort()

        context.route("**/*", network)
        page.goto(helper.ORIGIN + "/")
        page.get_by_text("Einrichtung am Knob öffnen", exact=True).wait_for()
        assert page.locator("#authorize").is_disabled()
        assert not any(method == "authorize" for method, _ in device.calls)
        page.screenshot(path=str(args.screenshots / "01-open-on-device.png"), full_page=True)
        cookie = next(item for item in context.cookies() if item["name"] == "pw_setup")
        assert cookie["httpOnly"] and cookie["sameSite"] == "Strict"
        original_cookie = cookie["value"]

        with app.lock:
            device.setup = True
        page.locator("#refresh").click()
        page.get_by_text("Zuerst WLAN am Knob einrichten", exact=True).wait_for()
        assert page.locator("#authorize").is_disabled()
        assert "QR-Code" in page.locator("#detail").inner_text()
        assert "Heim-WLAN" in page.locator("#detail").inner_text()
        assert not any(method == "authorize" for method, _ in device.calls)
        assert not page.locator("#indicator").evaluate("element => element.classList.contains('ready')")
        page.screenshot(path=str(args.screenshots / "02-wlan-required.png"), full_page=True)

        with app.lock:
            device.network_connected = True
        page.locator("#refresh").click()
        page.get_by_text("Bereit für deine Spotify-Anmeldung", exact=True).wait_for()
        assert page.locator("#authorize").is_enabled()
        assert page.evaluate("document.documentElement.scrollWidth <= innerWidth")
        page.screenshot(path=str(args.screenshots / "02-ready.png"), full_page=True)
        page.locator("#authorize").click()
        page.wait_for_url("https://accounts.spotify.com/authorize?*")
        page.locator("#finish").click()
        page.wait_for_url(helper.ORIGIN + "/")
        page.get_by_text("Das Gerät prüft die Spotify-Anmeldung …", exact=True).first.wait_for()
        assert callbacks and "cookie" not in callbacks[0]  # Strict cookie stays off cross-site callback.
        assert "callback" not in page.url and "code=" not in page.url
        assert not page.get_by_text("Spotify ist auf deinem Knob verbunden", exact=True).count()
        assert sum(method == "callback" for method, _ in device.calls) == 1
        assert next(item for item in context.cookies() if item["name"] == "pw_setup")["value"] == original_cookie
        page.screenshot(path=str(args.screenshots / "03-device-checks.png"), full_page=True)

        with app.lock:
            device.linked, device.state = True, "ready"
            device.authorization_id = "ab" * 24
        page.locator("#refresh").click()
        page.get_by_text("Spotify ist auf deinem Knob verbunden", exact=True).wait_for()
        page.screenshot(path=str(args.screenshots / "04-linked.png"), full_page=True)
        assert page.evaluate("localStorage.length === 0 && sessionStorage.length === 0")

        # Even a retained linked/ready snapshot cannot claim readiness without WLAN.
        with app.lock:
            device.network_connected = False
        page.locator("#refresh").click()
        page.get_by_text("Zuerst WLAN am Knob einrichten", exact=True).wait_for()
        assert page.locator("#authorize").is_disabled()
        assert not page.locator("#indicator").evaluate("element => element.classList.contains('ready')")

        with app.lock:
            device.network_connected = True
            device.role = "companion_esp32"
        page.locator("#refresh").click()
        page.get_by_text("Der Display-Chip ist nicht verbunden. USB-Stecker um 180° drehen und erneut prüfen.", exact=True).wait_for()
        assert page.locator("#authorize").is_disabled()
        assert not page.locator("#indicator").evaluate("element => element.classList.contains('ready')")
        assert not failures, failures
        context.close()
        browser.close()
    print("Chromium setup flow passed: real loopback HTTP; USB/Spotify simulated; no real device/account used")


if __name__ == "__main__":
    main()

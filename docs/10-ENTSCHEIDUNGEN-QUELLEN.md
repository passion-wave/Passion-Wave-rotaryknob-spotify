# Entscheidungen, offene Gates und Quellen

Stand 2026-09-30. Für den gewünschten Mehrnutzer-Produktpfad wurden öffentliche Primärquellen und lokale Projektquellen untersucht. Keine Partnerzusage, Spotify-Authentifizierung oder Hardwareprobe ist damit ersetzt.

## Entscheidungsregister

| ID | Entscheidung | Begründung / Status |
| --- | --- | --- |
| ADR-01 | Eigenständiges Repository und Versionen | Keine Abhängigkeit vom HA-/MA-Releasezug; Altprojekt bleibt unverändert |
| ADR-02 | S3 als Produkt-/Web-/Netzwerkcontroller | 16-MB-Flash/8-MB-PSRAM, ein WLAN; tatsächliche Ressourcen messen |
| ADR-03 | ESP32 als schlanker Begleitprozessor | Hardware-/Mute-/Recoveryfunktionen; kein zweiter Kundennetzwerkendpunkt |
| ADR-04 | Pure ESP-IDF-Zielarchitektur, gezielte Portierung | ESPHome-Monolith nicht blind übernehmen; genaue Versionen erst nach Buildspike |
| ADR-05 | Freigegebener Provider hinter Schnittstelle | Controller-Produktzugang ungeklärt; eSDK ist nicht automatisch Fremdlautsprechercontroller |
| ADR-06 | Keine neue Spotify-Suche voraussetzen | Kunden fügen freigegebene Presets/Links über erlaubten Weg hinzu |
| ADR-07 | Radio Browser als Verzeichnis | Offene Suche und URLs; separater Wiedergabeweg erforderlich |
| ADR-08 | Klinke zunächst nur mögliche Line-Ausgabe | Kopfhörerverstärker/Last nicht belegt |
| ADR-09 | Ein Updateprodukt, ESP32 zuerst, S3 danach | Website bleibt während erster Phase auf S3; Zwischenpaar braucht Kompatibilität |
| ADR-10 | Normales Display-Aus statt S3-Deep-Sleep | S3 muss Website/Provider weiterhin bedienen |
| ADR-11 | Sicherer Verwaltungs-AP als Basis | Komfortables LAN-HTTPS ohne Zertifikatsbetrieb noch offen |
| ADR-12 | Externer Linkingdienst nur explizite Option | Kann PKCE vereinfachen, erweitert aber Alles-auf-dem-Gerät-Vorgabe |
| ADR-13 | Sonos zuerst als Connect-Ziel, LAN gesondert prüfen | Lokale Sonos-Route benötigt belegten Zugang/Lizenz; Cloud ist eine abweichende Betriebsoption |
| ADR-14 | Ein Provider pro Wiedergabesitzung | Keine Doppelsteuerung desselben Sonos-Raums über verschiedene APIs |
| ADR-15 | Wetter ausdrücklich im Portumfang | Direkte S3-Provider ersetzen HA; UI, Wetterfotos und Avatar werden portiert |
| ADR-16 | Wetter und Radar haben getrennte Datenverträge | Stundenforecast ist kein Nowcast; ETA/Vektor benötigen eigenes Gate |
| ADR-17 | Komprimierte lokale Wetter-/Avatarassets | Flashbudget mit A/B und Signatur-/Rollbackbindung früh qualifizieren |
| ADR-18 | Bestand, Anpassung und neue Funktionen getrennt | Quellaudit statt pauschalem Kopierplan; Backendautorität wandert auf S3 |

## Quellenindex

| Thema | Primärquelle |
| --- | --- |
| Spotify Hardwareprozess | [Commercial Hardware](https://developer.spotify.com/documentation/commercial-hardware) |
| Hardware API / Suchgrenzen | [Hardware FAQs](https://developer.spotify.com/documentation/commercial-hardware/implementation/faqs) |
| Receiververhalten | [Hardware Buttons](https://developer.spotify.com/documentation/commercial-hardware/implementation/guides/hardware-buttons) |
| SDK-Ressourcen / Verhalten | [Technical Requirements](https://developer.spotify.com/documentation/commercial-hardware/implementation/requirements/technical) |
| SDK-Anmeldung | [ZeroConf](https://developer.spotify.com/documentation/commercial-hardware/implementation/guides/zeroconf) |
| SDK-Wartung | [Updates and support](https://developer.spotify.com/documentation/commercial-hardware/launch/support) |
| Web-API-Verteilung | [Quota Modes](https://developer.spotify.com/documentation/web-api/concepts/quota-modes) |
| Aktueller Änderungsstand | [Juli 2026](https://developer.spotify.com/documentation/web-api/references/changes/july-2026), [Mai 2026](https://developer.spotify.com/documentation/web-api/references/changes/may-2026), [Februar-Migration](https://developer.spotify.com/documentation/web-api/tutorials/february-2026-migration-guide) |
| OAuth | [PKCE](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow), [Redirects](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri) |
| Spotify-Nutzungsregeln | [Developer Policy](https://developer.spotify.com/policy) |
| Radio | [API](https://docs.radio-browser.info/), [FAQ](https://www.radio-browser.info/faq), [Projekt](https://github.com/segler-alex/radiobrowser-api-rust) |
| Originalboard-Familie | [Guition-Handbuch JC3636K518](https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/FTPData/UEditor/file/2026121/1768961095200/JC3636K518%20Instructions-EN.pdf) |
| Vergleichsboard / Schaltplan | [Waveshare Wiki](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8), [Schaltplanarchiv](https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-schematic.zip) |
| DAC-Ausgang | [TI PCM5100A](https://www.ti.com/product/PCM5100A) |
| Espressif Spotify-Plattform | [Aktuelle FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/audio-development-framework.html#do-espressif-modules-support-spotify-connect) |
| Provisionierung | [Espressif Unified Provisioning](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/provisioning/provisioning.html) |
| Firmwareupdate | [IDF OTA](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ota.html), [HTTPS OTA](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/esp_https_ota.html) |
| Credentialspeicher | [NVS Encryption](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/storage/nvs_encryption.html) |
| Browsergrenzen | [Chrome Bluetooth](https://developer.chrome.com/docs/capabilities/bluetooth), [WebKit](https://webkit.org/tracking-prevention/), [Chrome LNA](https://developer.chrome.com/blog/local-network-access) |

## Quellenkonflikte

- Ältere Spotify-Februar-Anleitung nennt eine Client-ID, Juli 2026 nennt 25. Für diesen Aspekt hat der neuere Änderungsstand Vorrang; daraus entsteht keine Vertriebsfreigabe.
- Frühere Espressif-PDFs verneinen Unterstützung, aktuelle HTML-FAQ verweist an den Vertrieb. Es bleibt eine konkrete Partner-/Plattformprüfung, keine positive Kompatibilitätszusage.
- Der Hersteller nennt einen Kopfhöreranschluss; DAC-Datenblatt spezifiziert Line-Last und die Anschluss-Tochterplatine ist unvollständig dokumentiert. Elektrischer Nachweis hat Vorrang vor Produktwortlaut.
- Der meistverwendete lokale RotaryKnob-Ordner steht auf beta.7, neuere lokale Quellen auf beta.16. Commit-Identität und tatsächliche `VERSION` gelten, nicht der Ordnername.
- General-Web-API-Policy und Partner-Hardwareanforderungen beschreiben unterschiedliche Integrationsbedingungen. Radio-/Spotify-Kombination vor Vertrieb unter dem tatsächlichen Produktvertrag prüfen.

## Heute offene Annahmen

Keine Freigabe des Controllerprofils, kein bestätigtes S3-SDK, keine sichere browserübergreifende automatische OAuth-LAN-Rückgabe, keine vermessene Kopfhörerlast, keine kompilierte native Firmware, keine echte Dual-MCU-OTA. Diese Grenzen sind in Tests/Plan verankert und dürfen bei späterer Umsetzung nur durch konkrete Evidenz aufgehoben werden.

## Sonos-Quellen und Abgrenzung

Die [Sonos-Untersuchung](12-SONOS-PRUEFUNG.md) enthält die einzelnen Primärbelege: Spotify-Support, Sonos-Architektur/Terms, Cloud-Autorisierung, Gruppen-/Favoriten-/Stream-API, Subscriptions und Quoten. Insbesondere Cloud-Veröffentlichung und gesonderte LAN-Lizenz nicht verwechseln; Sonos-Favoriten nicht mit Spotify-URIs gleichsetzen. Sämtliche Sonos-Fähigkeiten und die direkte lokale Geräteautorisierung bleiben ungetestet.

## Wetterquellen und Portplanung

[Wetterarchitektur](14-WETTER.md) enthält die offiziellen Belege zu DWD MOSMIX/WMS/Nutzungsrechten, Open-Meteo Customer API und Intervallen, MET Norway und RainViewer. DWD-Direktzugang ist der erste schlüssellose Spike; Open-Meteo bleibt ein funktionaler Komfortkandidat unter Schlüssel-/Produktgate. Die aktuelle DWD-WMS-Layerprobe ist offen. RainViewer-Transitionangaben haben Vorrang vor älteren FAQ-Versprechen zu Zukunftsradar.

[Featuretabellen](13-FEATURE-PORTIERUNG.md) verankern den lokalen Codeaudit auf `9cc5576`. Quelltext und Modelltests ersetzen weder physische Wetter-/Radarabnahme noch neue Produktfreigaben. Die freien Wetterangebote sind nicht pauschal als unbegrenzte kommerzielle Flottenlösung eingeplant.

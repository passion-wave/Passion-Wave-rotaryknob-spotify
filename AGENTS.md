# Arbeitsvertrag

Dieses Repository ist ein eigenständiges Spotify-Edition-Framework. Lies README und das fachlich betroffene Dokument in docs vor Änderungen.

- Produktziel: weitere Nutzer; Spotify-Zulassung und Controller-Fähigkeit sind offene harte Gates. Kein Live-Web-API-Adapter im Produktprofil ohne dokumentierte passende Genehmigung. eSDK-Fähigkeiten nicht erfinden.
- Kein Home Assistant, Music Assistant, MQTT-Broker oder laufender externer Steuerungsserver als Produktabhängigkeit. Auch kein eigener externer Anmeldedienst und keine eigene externe Callbackseite als Ersatz; ein statischer Updatehost bleibt erlaubt.
- Die Konfigurationswebsite muss im normalen USB-Betrieb jederzeit im Heim-WLAN schreibend erreichbar sein. Sicherer browserübergreifender Transport bleibt Gate G2; der geschützte Geräte-AP dient Erstsetup/Recovery und ersetzt nicht den gewählten Alltagsweg.
- S3 besitzt Produktzustand, Website und UI; classic ESP32 ist Begleitprozessor. Ein physisches Gerät, eine Produktversion, zwei Images.
- Bestehende RotaryKnob-Repositories und reale Geräte nur nach passend autorisiertem Auftrag verändern. Keine vorhandenen Secrets oder Gerätekonfigurationen importieren.
- Referenzmodelle und Website-Demo sind keine Geräteimplementierung. Prüfberichte müssen automatisiert, simuliert und physisch beobachtet auseinanderhalten.
- Schema-/Vertragsänderungen: Beispiele und docs aktualisieren, python3 tools/check.py ausführen. Sicherheits- und OTA-Regeln mit Fehlerfällen testen.
- Entwicklungs-Toolchain: ESP-IDF 5.4.3, LVGL 9.2.2. Erfolgreiche Builds als Compiler-Nachweis dokumentieren; sie ersetzen keine Board-, Speicher-, Recovery- oder Produktabnahme. Kein Flash vor Identifikation und verifiziertem Backup des jeweiligen Chips. Stand und verbleibende Gates: docs/16-NATIVE-IMPLEMENTIERUNG.md.
- OTA bleibt signiert, kompatibilitätsgeprüft und transaktional über beide Chips. Kein gemeinsames Factory-Passwort, keine Tokens in Exporten oder Logs.
- Proprietäres SDK darf nur entsprechend Partnervertrag außerhalb öffentlicher Artefakte eingebunden werden. Lizenzhinweise übernommener Quellen erhalten.

- Wetter, Forecast, Wetter-Screensaver, Avatar und Radar sind ausdrücklich im Portumfang. Deutschland zuerst, keine laufenden Wetter-/Radaranbietergebühren. Lies docs/14-WETTER.md; direkte Datenanbieter ersetzen HA. Radar-ETA/-Vektor, kommerzielle Nutzung und Assetbudget bleiben eigene Gates.
- Verbindliche Lieferstaffelung und Nutzerantworten: docs/15-OFFENE-PRODUKTENTSCHEIDUNGEN.md. L3 startet mit zwei RotaryKnobs und vorhandenen Sonos Roam/Move als Connect-Prüfzielen; Revisionen/Generationen sind noch zu erfassen. L4 enthält Spotify, Wetter/Avatar, Radioverwaltung, Website und OTA; Radar folgt in L5, Radioausgabe später. Zwei Pilotgeräte beweisen keine Produktfreigabe oder verfügbare Nutzerzahl. Sonos LAN bleibt Prüfung; Sonos Cloud mit eigenem Authdienst ist keine aktive Umsetzungsroute.
- Vollständige Feature-/Quellzuordnung und Paketplanung: docs/13-FEATURE-PORTIERUNG.md und docs/08-IMPLEMENTIERUNGSPLAN.md. Neue Funktionen nicht als unveränderte Bestandsports ausgeben.

## Ausdrückliche Pilotausnahme vom 5. Oktober 2026

Der Nutzer hat für die zwei Pilotgeräte die „zeitbegrenzte unverschlüsselte Heimnetz-Verwaltung“ freigegeben. Nur `CONFIG_PW_LAN_HTTP_LAB` darf deshalb zusätzlich HTTP-Schreibzugriffe nach physischem Öffnen am Knob und erfolgreichem Einmalcode erlauben: feste zehn Minuten, fünf Codeversuche, ein Client-IP-/Cookie-Paar, Host-/Origin-/CSRF-Prüfung. Im Standardprofil bleibt dies deaktiviert. Nicht als sicherer LAN-Transport oder Produktfreigabe ausgeben; G2 bleibt offen. Keine Tokens oder Codes loggen, keine Freigabe allein per Netzwerk starten.

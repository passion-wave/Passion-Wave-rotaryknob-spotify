# Arbeitsvertrag

Dieses Repository ist ein eigenständiges Spotify-Edition-Framework. Lies README und das fachlich betroffene Dokument in docs vor Änderungen.

- Produktziel: weitere Nutzer; Spotify-Zulassung und Controller-Fähigkeit sind offene harte Gates. Kein Live-Web-API-Adapter im Produktprofil ohne dokumentierte passende Genehmigung. eSDK-Fähigkeiten nicht erfinden.
- Kein Home Assistant, Music Assistant, MQTT-Broker oder laufender externer Steuerungsserver als Produktabhängigkeit.
- S3 besitzt Produktzustand, Website und UI; classic ESP32 ist Begleitprozessor. Ein physisches Gerät, eine Produktversion, zwei Images.
- Bestehende RotaryKnob-Repositories und reale Geräte nur nach passend autorisiertem Auftrag verändern. Keine vorhandenen Secrets oder Gerätekonfigurationen importieren.
- Referenzmodelle und Website-Demo sind keine Geräteimplementierung. Prüfberichte müssen automatisiert, simuliert und physisch beobachtet auseinanderhalten.
- Schema-/Vertragsänderungen: Beispiele und docs aktualisieren, python3 tools/check.py ausführen. Sicherheits- und OTA-Regeln mit Fehlerfällen testen.
- Noch keine Toolchain freigegeben. Keine Firmware als baubar/flashbar markieren, bevor Board-, SDK-, Speicher- und Recovery-Gates belegt sind.
- OTA bleibt signiert, kompatibilitätsgeprüft und transaktional über beide Chips. Kein gemeinsames Factory-Passwort, keine Tokens in Exporten oder Logs.
- Proprietäres SDK darf nur entsprechend Partnervertrag außerhalb öffentlicher Artefakte eingebunden werden. Lizenzhinweise übernommener Quellen erhalten.

- Wetter, Forecast, Wetter-Screensaver, Avatar und Radar sind ausdrücklich im Portumfang. Lies docs/14-WETTER.md; direkte Datenanbieter ersetzen HA. Radar-ETA/-Vektor, kommerzielle Nutzung und Assetbudget bleiben eigene Gates.
- Vollständige Feature-/Quellzuordnung und Paketplanung: docs/13-FEATURE-PORTIERUNG.md und docs/08-IMPLEMENTIERUNGSPLAN.md. Neue Funktionen nicht als unveränderte Bestandsports ausgeben.

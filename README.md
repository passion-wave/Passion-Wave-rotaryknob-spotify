# Passion Wave RotaryKnob · Spotify Edition

Eigenständiges Entwicklungsrepository für einen RotaryKnob mit geräteinterner Konfigurationswebsite, Spotify-Steuerung, freigegebenen Favoriten, Wetter-/Radar-/Avatarfunktionen und koordiniertem OTA. **Kein Home Assistant, kein Music Assistant, kein dauerhafter externer Steuerungsserver und kein eigener externer Anmeldedienst.**

**Stand: 2. Oktober 2026 · Native Firmware in Implementierung (`0.1.0-dev.12`).** Beide ESP-IDF-Projekte sowie ein getrenntes S3-Spotify-Laborprofil lassen sich bauen. Das Laborprofil ergänzt direkte Spotify-API-Steuerung, Lautsprecherauswahl, Playliststart und einen lokalen USB-Anmeldehelfer. Website, Boardtreiber, Wetter und Updateprüfung haben ausführbare Implementierungen. S3-Laborflash, Start, native USB-Statusabfragen und die erste vom Gerät bestätigte Spotify-Anmeldung sind nachgewiesen; die Geräteabnahme läuft. Vollständige Pair-OTA-Koordination und Spotify-Produktfreigabe sind noch offen. Das Ziel bleibt ein Produkt für weitere Nutzer. [Konkreter Implementierungs- und Prüfstand](docs/16-NATIVE-IMPLEMENTIERUNG.md).

Die erste Version umfasst Spotify Connect, Wettervorhersage, Wetterbilder und Avatar für Deutschland, Radioverwaltung, sichere Konfiguration jederzeit im Heim-WLAN und Pair-OTA. Radar und Radioausgabe folgen später. Der überwiegend per USB versorgte Pilot startet mit zwei RotaryKnobs; vorhandene Sonos Roam und Move sind die ersten Connect-Prüfgeräte. Wetter-/Radaranbieter dürfen keine laufenden Gebühren erfordern. Die [bestätigten Produktvorgaben](docs/15-OFFENE-PRODUKTENTSCHEIDUNGEN.md) ersetzen keine technische Abnahme: insbesondere mobile Spotify-Kundeneinrichtung ohne eigenen externen Hilfsdienst und sicherer Heimnetz-Webzugriff bleiben nachzuweisen.

## Zentrale Entscheidung

Ein kommerziell nutzbarer Spotify-Controller für fremde Connect-Lautsprecher ist auf Grundlage der öffentlichen Dokumentation **noch nicht freigegeben oder als Produkt nachgewiesen**. Die Hardware-FAQ verweist auf das Embedded SDK; dieses darf nicht einfach als Ersatz für die Web API eines Ferncontrollers angenommen werden. Vor der Live-Integration braucht es die schriftliche Klärung des konkreten Produkttyps, der Bedienrechte, Favoriten und Plattformunterstützung. Siehe [Machbarkeit und Produktfreigabe](docs/02-SPOTIFY.md).

Der Produktbuild hält Spotify deaktiviert. Nur das explizite Laborprofil enthält den direkten Web-API-Provider. Die erste echte Anmeldung auf dev.8 wurde am 02.10.2026 vom Gerät für den konkreten Anmeldeversuch und vom Nutzer bestätigt. Nach verifiziertem Appupdate auf dev.9 und Neustart gelang der Geräteabruf ohne erneute Anmeldung; zweimal wurden zwei Geräte erfolgreich geparst. Damit ist der Erhalt der gespeicherten Verknüpfung für diesen Ablauf belegt. Sonos-Auswahl auf dem Knob, hörbare Knob-Steuerung, weitere Refresh-/Kontowechsel- und Stromausfalltests bleiben offen. Der [USB-Anmeldehelfer](tools/spotify_setup/README.md) nutzt die am 01.10.2026 freigegeben angelegte „PassionWave RotaryKnob Lab“-App. Der mobile Kunden-Anmeldeweg bleibt noch zu qualifizieren.

Das Framework isoliert diesen offenen Punkt in einem Provider-Adapter. Lokale Bedienung, Konfiguration, Transport, Sicherheit und Updates lassen sich unabhängig entwickeln. **Keine Verteilung eines gemeinsamen Developer-API-Projekts als Produkt und kein Client-ID-Splitting zur Umgehung von Limits.**

Repository: [passion-wave/Passion-Wave-rotaryknob-spotify](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify).

## Einstieg

1. [Gesamtkonzept](docs/01-GESAMTKONZEPT.md): Produktumfang, Architektur, Entscheidungen.
2. [Vollständiger Implementierungsplan](docs/08-IMPLEMENTIERUNGSPLAN.md): Portierung, Lieferstände, Arbeitspakete, Abhängigkeiten, Aufwand und Abnahmen.
3. [Featureweise Umsetzungstabellen](docs/13-FEATURE-PORTIERUNG.md): Bestand, Probleme, kurze Lösungen und Abnahme pro Funktion.
4. [Wetter, Radar und Avatar](docs/14-WETTER.md): direkte Datenquellen und Umsetzung auf dem Gerät.
5. [Spotify](docs/02-SPOTIFY.md), [Onboarding & Website](docs/03-ONBOARDING-WEB.md), [Radio & Klinke](docs/04-RADIO-AUDIO.md).
6. [OTA & Sicherheit](docs/05-OTA-SICHERHEIT.md), [Übernahme aus dem Bestandsprojekt](docs/06-UPSTREAM.md), [Schnittstellen](docs/07-SCHNITTSTELLEN.md), [Testplan](docs/09-ABNAHME.md).
7. [Sonos-Architektur und Prüfplan](docs/12-SONOS-PRUEFUNG.md): Connect-Ziele, lokale Sonos-Anbindung, Favoriten, Gruppen und Radio.
8. [Entscheidungen und Quellen](docs/10-ENTSCHEIDUNGEN-QUELLEN.md) und [Prüfbericht](docs/11-PRUEFBERICHT.md).
9. [Bestätigte Produktentscheidungen und verbleibende Klärungen](docs/15-OFFENE-PRODUKTENTSCHEIDUNGEN.md): alle neun Nutzerantworten und ihre Auswirkungen auf die Umsetzung.
10. [Native Firmware, Build und Geräteabnahme](docs/16-NATIVE-IMPLEMENTIERUNG.md): tatsächlich implementierte Funktionen, Laborgrenzen und Recovery.
11. [Geplantes Setup-Repository](docs/17-SETUP-REPOSITORY.md): Verantwortlichkeiten, gemeinsame Schnittstelle und spätere Auslagerung der Einrichtungs-App.

## Ausprobieren und prüfen

```sh
python3 tools/preview.py
# Im Browser http://127.0.0.1:8765 öffnen.
python3 tools/check.py
```

Diese Vorschau zeigt weiterhin den **interaktiven Entwurf mit Beispieldaten** aus `web/`. Er speichert nur im Browser-Arbeitsspeicher und führt weder Spotify-Anmeldungen noch Updates aus. Die echte Gerätewebsite liegt separat in `firmware/web` und wird vom S3 ausgeliefert. Ihre aktuellen Laborendpunkte sind noch keine vollständige Implementierung des umfangreicheren geplanten Vertrags in `contracts/`.

`tools/check.py` prüft die Referenzlogik für Ausgabefähigkeiten und OTA-Reihenfolge, Beispiele und Dokumentverweise; der C++-Schnittstellenheader wird separat kompiliert, wenn ein Compiler vorhanden ist. Die vollständigen Geräteprüfungen sind in der Abnahme ausdrücklich offen.

## Repositorystruktur

| Ordner | Inhalt |
| --- | --- |
| `docs/` | Vollständiges Framework, Quellen, Entscheidungen, Umsetzung und Abnahmen |
| `contracts/` | JSON-Schemata und geplanter HTTP-API-Vertrag |
| `examples/` | Geheimnisfreie Beispielkonfiguration und **nicht installierbares** OTA-Manifest |
| `firmware/include/` | Plattformunabhängige C++-Schnittstellen für die spätere Firmware |
| `firmware/s3/`, `firmware/companion/` | Native ESP-IDF-Projekte für beide Chips |
| `firmware/components/` | Board, UI, persistente API, Wetter, Transport und Updateprüfung |
| `firmware/web/` | Echte Gerätewebsite, lokale Ort-/PLZ-Suche und Lizenznachweise |
| `web/` | Responsiver deutscher Konfigurationsentwurf |
| `tools/` | Lokale Vorschau, prüfbare Referenzmodelle und Prüfwerkzeug |
| `tests/` | Ablehnungsfälle, OTA-Ausfall- und Kompatibilitätsregeln |
| `research/` | Datiertes Audit der vorhandenen Hardware und Primärquellen |

Separate Historie und Versionslinie `0.1.0-dev.12`; keine unveränderte Kopie der HA-Firmware. Wiederverwendung erfolgt gezielt aus dokumentierten Commits mit erhaltenen Lizenzhinweisen. Quellen werden im verknüpften GitHub-Repository gepflegt. Es ist noch keine Produktfirmware veröffentlicht; die angelegte Spotify-Lab-App erteilt keine Produktfreigabe.

Wetter ist Teil der nativen Implementierung und des vollständigen Portierungsplans. P10 ergänzt weiterhin alle vereinbarten Anzeigeparameter und die Geräteabnahme. P11 liefert Radar später nach und blockiert die erste Produktversion nicht.

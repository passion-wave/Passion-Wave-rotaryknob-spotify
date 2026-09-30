# Passion Wave RotaryKnob · Spotify Edition

Eigenständiges Entwicklungsrepository für einen RotaryKnob mit geräteinterner Konfigurationswebsite, Spotify-Steuerung, freigegebenen Favoriten und koordiniertem OTA. **Kein Home Assistant, kein Music Assistant, kein dauerhafter externer Steuerungsserver.**

**Stand: 30. September 2026 · Framework / Architekturentwurf.** Das Ziel ist von Anfang an ein Produkt für weitere Nutzer. Es ist noch keine lauffähige Gerätefirmware und keine freigegebene Spotify-Hardware entstanden.

## Zentrale Entscheidung

Ein kommerziell nutzbarer Spotify-Controller für fremde Connect-Lautsprecher ist auf Grundlage der öffentlichen Dokumentation **noch nicht freigegeben oder als Produkt nachgewiesen**. Die Hardware-FAQ verweist auf das Embedded SDK; dieses darf nicht einfach als Ersatz für die Web API eines Ferncontrollers angenommen werden. Vor der Live-Integration braucht es die schriftliche Klärung des konkreten Produkttyps, der Bedienrechte, Favoriten und Plattformunterstützung. Siehe [Machbarkeit und Produktfreigabe](docs/02-SPOTIFY.md).

Das Framework isoliert diesen offenen Punkt in einem Provider-Adapter. Lokale Bedienung, Konfiguration, Transport, Sicherheit und Updates lassen sich unabhängig entwickeln. **Keine Verteilung eines gemeinsamen Developer-API-Projekts als Produkt und kein Client-ID-Splitting zur Umgehung von Limits.**

## Einstieg

1. [Gesamtkonzept](docs/01-GESAMTKONZEPT.md): Produktumfang, Architektur, Entscheidungen.
2. [Schrittweiser Implementierungsplan](docs/08-IMPLEMENTIERUNGSPLAN.md): Arbeitspakete, Abhängigkeiten, Abnahmen.
3. [Spotify](docs/02-SPOTIFY.md), [Onboarding & Website](docs/03-ONBOARDING-WEB.md), [Radio & Klinke](docs/04-RADIO-AUDIO.md).
4. [OTA & Sicherheit](docs/05-OTA-SICHERHEIT.md), [Übernahme aus dem Bestandsprojekt](docs/06-UPSTREAM.md), [Schnittstellen](docs/07-SCHNITTSTELLEN.md), [Testplan](docs/09-ABNAHME.md).
5. [Entscheidungen und Quellen](docs/10-ENTSCHEIDUNGEN-QUELLEN.md) und [Prüfbericht](docs/11-PRUEFBERICHT.md).

## Ausprobieren und prüfen

```sh
python3 tools/preview.py
# Im Browser http://127.0.0.1:8765 öffnen.
python3 tools/check.py
```

Die Website ist ein **interaktiver Entwurf mit Beispieldaten**. Sie speichert Änderungen nur im Browser-Arbeitsspeicher, nimmt keine echten Passwörter an und führt weder Spotify-Anmeldungen noch Updates aus. Der Vorschau-Server bindet ausschließlich an Loopback. Die Produktions-API liegt als Vertrag vor, nicht als fertiger Netzwerkdienst.

`tools/check.py` prüft die Referenzlogik für Ausgabefähigkeiten und OTA-Reihenfolge, Beispiele und Dokumentverweise; der C++-Schnittstellenheader wird separat kompiliert, wenn ein Compiler vorhanden ist. Die vollständigen Geräteprüfungen sind in der Abnahme ausdrücklich offen.

## Repositorystruktur

| Ordner | Inhalt |
| --- | --- |
| `docs/` | Vollständiges Framework, Quellen, Entscheidungen, Umsetzung und Abnahmen |
| `contracts/` | JSON-Schemata und geplanter HTTP-API-Vertrag |
| `examples/` | Geheimnisfreie Beispielkonfiguration und **nicht installierbares** OTA-Manifest |
| `firmware/include/` | Plattformunabhängige C++-Schnittstellen für die spätere Firmware |
| `web/` | Responsiver deutscher Konfigurationsentwurf |
| `tools/` | Lokale Vorschau, prüfbare Referenzmodelle und Prüfwerkzeug |
| `tests/` | Ablehnungsfälle, OTA-Ausfall- und Kompatibilitätsregeln |
| `research/` | Datiertes Audit der vorhandenen Hardware und Primärquellen |

Separate Historie und Versionslinie `0.1.0-framework.1`; keine unveränderte Kopie der HA-Firmware. Wiederverwendung erfolgt gezielt aus dokumentierten Commits mit erhaltenen Lizenzhinweisen. Keine Veröffentlichung, Geräteänderung oder Spotify-Registrierung durch dieses Framework.

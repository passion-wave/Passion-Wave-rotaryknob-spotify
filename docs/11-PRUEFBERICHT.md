# Prüfbericht Framework

Datum: 30. September 2026. Dies ist die Prüfung des Framework-Repositories, nicht des späteren Geräts.

## Ausgeführt

- `python3 tools/check.py`: **35 Tests bestanden** (23 Referenzmodelltests, 12 Vertragsprüfungen), JSON-Beispiele, zusätzliche Semantik, interne Markdownlinks und OpenAPI-Referenzen geprüft.
- C++17-Header mit `-Wall -Wextra -Werror -pedantic -fsyntax-only`: bestanden.
- JavaScript-Syntaxprüfung mit Node: bestanden.
- HTML-Struktur: eindeutige IDs, Label-/ARIA-Verknüpfungen und fehlende Passwortfelder geprüft; keine externen Scripts/Fonts.
- Kurzlebige lokale HTTP-Vorschau: HTML, JavaScript und CSS erfolgreich ausgeliefert; `no-store`, Content-Security-Policy und Ablehnung fremder Hostheader geprüft. Danach Vorschauprozess beendet.
- Separate fachliche Prüfung von Hardware-/Prozessorrollen, Klinkenannahmen, OTA-Abfolge und Vertragsabbildung; erkannte Versions-/Manifestbindungsfälle in Referenztests ergänzt.
- Einfache Prüfung auf private Schlüssel und bekannte Zugangstokenmuster: keine Treffer. Keine privaten Gerätekonfigurationen importiert.

## Grenzen

Keine visuelle Browserabnahme möglich, da kein Browser für die Vorschau aktiviert war. Responsive Gestaltung wurde strukturell vorbereitet, noch nicht auf echten Mobilbrowsern betrachtet. Die UI-Abläufe sind Simulationen mit Arbeitsspeicherzustand; nach Neuladen geht dieser verloren.

Keine echte Spotify-Anmeldung, kein SDK-Port, keine Radio-Suche im Netz aus der Demo, kein Firmwarebuild, keine Kryptografieverifikation, kein Flashen, kein Schalt-/Hör-/Stromausfalltest auf dem Gerät. Es gibt noch keinen produktiven Web-API-Server auf dem S3. Das strukturierte HTTP-API-Dokument ist kein laufendes Backend.

Der Schema-Prüfer deckt die hier verwendeten Schlüsselwörter ab, nicht den vollständigen JSON-Schema-/OpenAPI-Standard. Der CI-Workflow führt dieselben Prüfungen auf GitHub aus; sein laufender Status ist in den Repository-Actions sichtbar und von diesem lokalen Nachweis zu unterscheiden. Alle Produktgates aus [Abnahme](09-ABNAHME.md) bleiben offen.

## Ergänzung Sonos / Repositoryanbindung

Die Sonos-Erweiterung betrifft Architektur, Quellen, Onboarding, Inhalts-/Gruppenmodell sowie Implementierungs- und Abnahmeplan. Es wurde kein Sonos-Provider als funktionsfähig implementiert und kein Gerät angesprochen. Die bestehenden 35 Host-/Vertragsprüfungen und Syntax-/Linkprüfungen bleiben der Prüfmaßstab für diesen Dokumentationsstand; sie beweisen keine Sonos-Kompatibilität.

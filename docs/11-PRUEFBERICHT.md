# Prüfbericht Framework

Datum: 30. September 2026. Dies ist die Prüfung des Framework-Repositories, nicht des späteren Geräts.

**Historischer Framework-Nachweis:** Die folgenden Abschnitte betreffen die Planungsphase vor der nativen Implementierung. Inzwischen ergänzen reale Firmwarebuilds, Signatur-/Hosttests und Browserprüfungen diesen Stand. Der aktuelle Nachweis einschließlich weiterhin offener physischer Abnahmen steht in [Native Implementierung und Geräteabnahme](16-NATIVE-IMPLEMENTIERUNG.md).

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

## Vollständige Portplanung mit Wetter

Die Ergänzung inventarisiert Medien/UI/Backend und Wetter einschließlich Radar/Avatar am Quellcommit `9cc5576`. Umsetzungstabellen, Arbeitspakete, Ressourcen-/Quellen-/Produktgates und zusätzliche Soll-Abnahmen sind dokumentiert. Bestehende Quellartefakte wurden nur gelesen; weder Bestandsfirmware noch Geräte verändert.

Es wurden keine Featureimplementierungen, Wetter-/Spotifykonten oder Anbieterabonnements angelegt. Die bestehenden Verträge und Webdemo wurden nicht um vorgetäuschte Wetterfunktionen erweitert. Host-/Syntax-/Linkprüfungen prüfen weiterhin das Framework; die neuen WT-Abnahmen sind offene Sollfälle. Eine grüne CI bestätigt keine Geräte-, Radar-, Partner- oder Produktfreigabe.

Für diesen Dokumentationsstand erneut ausgeführt: `python3 tools/check.py` mit 35 bestandenen Tests sowie C++-/JavaScript-Syntax und internen Links; `git diff --check`. Zusätzlich temporär geprüft: 129 eindeutige Feature-/Abgrenzungs-IDs und sämtliche expliziten Abnahmeverweise auf vorhandene Test-IDs. Fachreview: Reihenfolge, Herkunft, Wetterfrische, Radar-Metadatengate und OTA-Assetbindung konsistent. Keine neuen WT-/Hardwaretests als ausgeführt gewertet.

## Übernahme der neun Produktantworten

Die Antworten vom 30. September 2026 stehen im Entscheidungsregister und sind in Architektur, Arbeitspakete, Featuretabellen und Abnahmen übernommen: Anmeldung ohne eigenen externen Dienst, sichere Heimnetz-Verwaltung, USB-Normalbetrieb, gebührenfreie Wetterquellen für Deutschland, zuerst zwei Pilotknobs mit Roam/Move sowie getrennte erste Produktversion und Radar-Nachlieferung. Radioausgabe bleibt später. Exakte Gerätegenerationen und Erweiterungsmenge/-termin sind weiterhin offen.

Für diese reine Planaktualisierung erneut ausgeführt: `python3 tools/check.py` mit **35 bestandenen Tests**, JSON-/API-/Linkprüfung, C++17- und JavaScript-Syntaxprüfung; `git diff --check` ohne Befund. Keine Tests hinzugefügt und keine Runtime-Verträge, Firmware oder Webdemo verändert. Technische und vertragliche Gates, insbesondere G0/G2, bleiben unbestanden. Die Entscheidung gegen einen Anmeldedienst ist kein Nachweis eines bereits funktionierenden alternativen Anmeldewegs.

## Nachtrag 01.10.2026: direkter Spotify-Laborprovider

`0.1.0-dev.2` ergänzt native Spotify-Steuerung und Lautsprecherauswahl, geschützte UI-/Webaktionen und den lokalen USB-PKCE-Helfer. Die drei Firmwareprofile wurden erfolgreich gebaut. 285 Spotify-Modell- und 529 Provider-Assertions, 36 USB-Parserprüfungen, 17 Helfertests, 35 Frameworktests und beide isolierten Chromium-Szenarien bestanden. Netz-, NVS-, USB- und Spotifyantworten dieser neuen Funktionstests sind simuliert; keine physische Wiedergabe wird daraus abgeleitet. Der vollständige neue Umfang und ausstehende reale Prüfablauf stehen im [nativen Implementierungsstand](16-NATIVE-IMPLEMENTIERUNG.md#spotify-laborstand-dev2-und-nächste-geräteprobe).

Die bestehende Spotify-App wurde im angemeldeten Dashboard als **HomeAssistant / Development mode / 180 Tage Refresh-Token-Laufzeit** gelesen, unverändert belassen. Die getrennte Lab-App ist vorbereitet, nicht angelegt. Produktzulassung und kundenfähiger mobiler Anmeldeweg bleiben offen.

## Nachtrag 01.10.2026: Lab-App, native USB-Verbindung und erster Geräteflash

Nach ausdrücklicher Zustimmung wurde die getrennte **PassionWave RotaryKnob Lab**-
App angelegt; Appdaten und Redirect sind im öffentlichen Profil dokumentiert.
Die Struktur für ein späteres Setup-Repository sowie gemeinsame USB-Verträge und
ausführbare C-/Python-Goldens sind vorbereitet. Noch kein zusätzliches Repo.

Nach bestätigtem USB-Steckerwechsel wurden 16 MiB S3-Flash vollständig gesichert
und gegen den Chip verifiziert. Erst danach wurde dev.3 installiert. Der reale
Start zeigte einen Beleuchtungsfehler; dessen Korrektur ist als dev.4 geschrieben,
verifiziert und ohne den Fehler gestartet. Zehn echte USB-HELLO-Antworten und der
Setupstatus bestätigten dev.4. Originalbackup und Diagnoseimages liegen privat
außerhalb von Git. Keine eFuses geändert, Companion noch auf Originalfirmware.

Die neue Prüfung umfasst 46 Framework-/Vertragstests, 37 gemeinsame native
Requestvektoren, 17 Helfertests, 8 Backup-Fehlerfälle, 285 Spotify-Modell- und
623 Worker-Assertions sowie 36 USB-Parserprüfungen. Das isolierte Login-Browser-
szenario bestand mit simuliertem USB/Spotify. Alle drei Profile wurden gebaut;
die fehlende CMake-Abhängigkeit auf VERSION ist korrigiert. Exakte Imagehashes,
physische Beobachtungen und verbleibende Abnahmen stehen im
[Geräteversuch](16-NATIVE-IMPLEMENTIERUNG.md#geräteversuch-dev3--dev4-am-01102026).
Echte Spotify-Anmeldung und hörbare Ausgabe sind weiterhin unbestätigt.

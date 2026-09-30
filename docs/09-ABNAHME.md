# Test- und Abnahmekatalog

Befundarten: **automatisiert am Host**, **simuliert**, **auf echter Hardware gemessen**, **physisch gesehen/gehört**, **vertraglich bestätigt**. Sie ersetzen einander nicht. Die heutigen Hosttests beweisen nur Referenzverhalten und Dokument-/Vertragskonsistenz. Alle folgenden Produktgates sind noch offen.

## Lieferbezogene Abnahme nach den Nutzerantworten

- **L3:** zunächst zwei RotaryKnobs des Bestandsprojekttyps, Sonos Roam und Move als Connect-Ausgaben. Generationen/Revisionen vor Testbeginn erfassen. Pflicht: sichere Heimnetz-Website ohne WLAN-Wechsel, zugelassene Anmeldung ohne eigenen externen Dienst, Spotify, Radioverwaltung, Wetter/Avatar, USB-Erreichbarkeit und Pair-OTA.
- **L4:** erste Produktversion nach erweiterter Abnahme desselben Umfangs. Fünf Onboarding-Erstnutzer und insgesamt zehn Personen für die Produktprobe sind Ziele für spätere Tests, auch nacheinander an zwei Geräten möglich. Die kleine Pilotgruppe ersetzt diese UX-Abnahme nicht. Hersteller-/Modellzusagen nur für tatsächlich geprüfte Ziele.
- **L5:** Radar als Nachlieferung. WT-10/11 und W-RAD-Gates erst hierfür erforderlich; WT-12/PERF-01 sowie Dauer-/OTA-Tests um die reale Radarlast ergänzen und erneut ausführen. Offene Metadaten bleiben offen.
- **Spätere Optionen:** AUD-01/02 sowie Abnahmen nativer Sonos-Funktionen werden erst für diese Erweiterungen releasepflichtig. SON-01/07 sind für die vorhandenen portablen Connect-Testziele bereits relevant. Radioverwaltung und Ablehnung ungeeigneter Radioausgaben bleiben in L3/L4 Pflicht.

Siehe [bestätigte Produktentscheidungen](15-OFFENE-PRODUKTENTSCHEIDUNGEN.md). Ein nicht ausgelieferter Funktionspfad ist „nachgelagert/nicht anwendbar“, niemals automatisch „bestanden“.

## Prüfmatrix

| ID | Test / Störung | Akzeptanz |
| --- | --- | --- |
| G0-01 | Genehmigter Controller für fremde Connect-Lautsprecher | Schriftliches Produktprofil und verwendbarer SDK-/API-Zugang |
| G0-02 | Mehrnutzer, Presets, Podcasts, Radio im selben Produkt | Nutzung/Verteilung ausdrücklich geklärt |
| HW-01 | Board-/Flash-/PSRAM-/Chiprevision | Protokolliert, Partitionen passen mit Reserve |
| HW-02 | EC1 langsam/schnell, Gegenrichtung, Störimpulse | Keine Doppelerfassung durch EC2, gleiche Richtung wie Anzeige |
| UI-01 | Netzlast + schneller Ring + Touch | Ziel p95 lokale Eingabeantwort <50 ms, p99 <100 ms; gemessen, kein aktueller Befund |
| UI-02 | verspätetes Cover/Metadaten nach Trackwechsel | Keine gemischten Präsentationen, veraltete Generation verworfen |
| UI-03 | 360-px-Website, Tastatur/Fokus, Screenreader-Grundtest | Alle Eingaben erreichbar und beschriftet, kein horizontales Layoutproblem |
| NET-01 | Falsches Passwort, SSID fehlt, Routerwechsel, 5-GHz-only | Erklärter Zustand und lokale Recovery ohne Konto-/Favoritenverlust |
| NET-02 | S3-Standby | Website/Provider im normalen Standby erreichbar; Deep Sleep ausdrücklich offline |
| ON-01 | iOS/Android Erststart und captive mini-browser | Vollständige freigegebene Anmeldung ohne Entwicklerwerkzeuge und ohne eigenen externen Anmelde-/Callbackdienst |
| ON-02 | OAuth state falsch/abgelaufen/erneut verwendet | Ablehnung, kein Konto ersetzt |
| ON-03 | Tokenrefresh fehlt neuer Refresh-Token / 401 / Widerruf | Gültiger Token erhalten bzw. verständlich neu anmelden |
| SEC-01 | Fremde Origin, DNS-Rebinding, CSRF, Sitzung abgelaufen | Schreibzugriff abgelehnt, keine sensitiven GETs/CORS-Freigaben |
| SEC-02 | Export, Log, Diagnose, Factory Reset | Keine Geheimnisse sichtbar; Eigentümerwechsel löscht Konto-/Sitzungsgeheimnisse; Herstelleridentität und Firmware-Vertrauensanker bleiben |
| SP-01 | Zunächst Sonos Roam und Move, mehrere Konten; später weitere Modelle | Generation/Firmware und sichtbare/steuerbare Fähigkeiten einzeln dokumentiert; zwei Sonos-Familien belegen keine herstellerübergreifende Kompatibilität |
| SP-02 | `is_restricted`, fehlende Lautstärke, verschwundene ID | Aktion deaktiviert, keine zufällige andere Ausgabe |
| SP-03 | 429 / QUOTA_EXCEEDED / 403 / 204 | Backoff und Erklärung, kein Requeststurm |
| SP-04 | Schnelle Playlists, parallele Spotify-App-Steuerung | Neueste Auswahl gewinnt lokal, Status wird nachgeführt |
| SP-05 | Eigene/fremde/editorielle Playlists | Metadaten, Start und Trackbrowser getrennt geprüft |
| SP-06 | Podcaststart aus Ruhe, Episode/Resume, Marktfehler | Wirklicher gewünschter Start oder klar fehlende Fähigkeit |
| CAT-01 | Nichtfreigegebene ID und manipuliertes URI | Server lehnt ab; Freigabe nur Knob-Katalog, keine Kontosperre behauptet |
| RAD-01 | Spiegel down, DNS-Wechsel, kaputte Streams | Fallback mit Limits, gespeicherte Liste bleibt |
| RAD-02 | Radio-URL zu Connect-only-Ausgang | Ablehnung vor Netzwerkbefehl |
| RAD-03 | Redirect auf internes Netz / riesige Playlist / fremdes Schema | Vor Verbindung bzw. nach begrenztem Lesen ablehnen |
| AUD-01 | Reales DAC/Mux/Mute und Klinkenlast | Hardwarefunktion und Last nachgewiesen, Pegel/Rampen geprüft |
| AUD-02 | Radio decoder + LVGL + Netzverlust | Kein unsicherer Pegel/Crash; Unterlauf gemutet, UI reagiert |
| OTA-01 | Falsche Rolle/Hardware/Signatur/zu großes Image | Vor Aktivierung ablehnen, aktuelles Paar weiterhin nutzbar |
| OTA-02 | Altes S3 + neues ESP32 | Handshake, Status, Recovery und Kerneingaben funktionieren |
| OTA-03 | Power Cut vor/nach jeder Journal-/Slottransition | Sicherer Boot oder eindeutiger Recoveryzustand; nie falsches complete |
| OTA-04 | ESP32-Bootloop | S3 bleibt alt/erreichbar, ESP32-Rollback bzw. USB-Recovery |
| OTA-05 | S3-Bootloop mit neuem ESP32 | S3-Rollback funktioniert und versteht Peer/Journal |
| OTA-06 | Browser geschlossen, UART-Chunk doppelt/verloren | Gerätejob deterministisch, keine Doppelaktivierung, Resume geprüft |
| OTA-07 | Migration + Rückfall | Alte Konfiguration/Secrets bleiben verwendbar |
| OTA-08 | Unter Sicherheitsversion / widerrufener Schlüssel | Abgelehnt; dokumentierter zulässiger Rettungsweg |
| SON-01 | Sonos über Connect nach Kaltstart/Standby | Ziel sichtbar/steuerbar oder erklärter fehlender Zustand, keine fremde Ausgabe |
| SON-02 | Offizielle lokale Sonos-Produktanbindung | LAN-Zugang, Lizenz und sichere Authentifizierung belegt |
| SON-03 | Gruppe/Koordinator/Raumname ändert sich | Playeranker korrekt aufgelöst, keine still hinzugefügten Räume |
| SON-04 | Derselbe Raum über Connect und Sonos | Genau eine Befehlsautorität, alte Generationsantworten verworfen |
| SON-05 | Spotify-/Radiofavorit, Podcast, freie URL | Inhalts-/Queuewirkung und jede Wiedergabefähigkeit separat nachgewiesen |
| SON-06 | Mehrere Haushalte, Konto getrennt, Favorit entfernt | Keine fremde Referenz, verständliche Wiederverknüpfung |
| SON-07 | Portable Modelle/WLAN/Bluetooth/Schlaf und ältere Generation | Tatsächliche Modell-/Firmwarematrix, keine pauschale Wake-/S1-Zusage |
| SON-08 | Falls Cloud gewählt: Refresh/Events/Quota | Kein geteiltes Secret im Gerät; sicherer dauerhafter Weg und appweites Budget |
| SOAK-01 | 72 h gemischter Betrieb | Keine wachsenden Lecks/Queues, keine verlorene Verwaltung |
| PILOT-00 | Erster Pilot mit zwei RotaryKnobs, Roam und Move | Getrennte Identitäten, paralleler Betrieb, Wetterorte/Konten, Netzwechsel, Dauerlauf und Pair-OTA nachgewiesen; noch keine allgemeine Produktfreigabe |
| PILOT-01 | Erweiterte Abnahme mit zehn neuen Nutzern vor L4 | Einrichtung ohne Entwicklerhilfe; Ziel median <3 min und p90 <5 min erst hier belastbar messen; keine Forderung nach zehn gleichzeitigen Geräten |

## Nachweisformat

Pro Gate: Commit, Build-/SDKversion, Produkt-/Chiprevision, beide Firmwarehashes, Datum, Testkonto anonymisiert, Lautsprechermodell/-firmware, reproduzierbare Schritte, Soll/Ist, Ergebnis und Nachweisart. Keine Kontozugänge dokumentieren. Bei UI/Audioschritten Beobachter, Foto/Messung bzw. tatsächlichen Hörtest notieren; API-Status ist kein Hörbeweis.

## Referenztests in diesem Framework

`python3 tools/check.py` führt Tests der Hostmodelle aus und prüft Vertragsbeispiele/Referenzen. Die Modelle erhalten Signatur-/Freigabeevidenz als Parameter, **prüfen diese nicht kryptografisch**. Produktionsadapter müssen diese Evidenz aus verifizierten, nicht vom Browser gesetzten Quellen bilden. Die Webdemo simuliert ebenfalls nur Zustände.

Ein einzelner erfolgreicher Hosttest gilt nie als bestandenes Produktgate. Vor dem jeweiligen Release sind sicherer Heimnetz-Webzugriff, der tatsächlich freigegebene Anmeldeweg ohne eigenen externen Dienst, der zugelassene Produktadapter und Flash-/Bootloaderverhalten nachzuweisen. PKCE-/OAuth-Fälle gelten, soweit der freigegebene Flow sie verwendet; ein anderer Partnerflow benötigt entsprechende eigene Authprüfungen. Audioelektrik wird erst vor Freischaltung lokaler Audioausgabe Pflicht und blockiert L3/L4 nicht.

Details und Reihenfolge der Sonos-Gates: [Sonos-Prüfplan](12-SONOS-PRUEFUNG.md). Noch keine Sonos-Hardwareabnahme durchgeführt.

## Wetterport: konkrete Abnahmen

Alle Nachweise sind offen; Zuordnung zu Funktionen in [Featuretabellen](13-FEATURE-PORTIERUNG.md), Quellen-/Produktgates in [Wetterarchitektur](14-WETTER.md).


| Test-ID | Prüfgegenstand | Erforderlicher Nachweis |
|---|---|---|
| WT-01 | Wetteroberfläche und Navigation | Temperaturbogen, Tageskontext und zwei Folgetage passen auf das Runddisplay; Touch und Ring bleiben unter definierter Mischlast bedienbar. |
| WT-02 | Datenqualität und Aktualität | Fehlende Werte, falsche Einheiten, alte Zeitstempel und Offlinezustände erzeugen nachvollziehbare Unbekannt-/Veraltet-Anzeigen statt erfundener Messwerte. |
| WT-03 | Forecast-Zeitzuordnung | UTC-Offsets, Datumswechsel, Sommerzeit, Lücken, Duplikate und unvollständige Vierstundenfenster werden mit Fixtures korrekt behandelt. |
| WT-04 | Direkter Wetteradapter für Deutschland ohne laufende Anbietergebühren | Produktgeeignete Rechte/Quoten und Abdeckung nachgewiesen; Antwortgrößen und Abrufraten begrenzt; TLS-, Timeout-, Rate-Limit- und Providerfehler führen zu Backoff und konsistentem Cache. |
| WT-05 | Standort und Persistenz | Manuelle Standort-/Zeitzoneneinrichtung funktioniert; Änderungen bleiben nach Neustart erhalten; Antworten für den alten Standort werden verworfen. |
| WT-06 | Assets und OTA-Budget | Alle Wetterfotos und Avatar-Schlüssel sind korrekt zugeordnet und geprüft; keine Randartefakte; reale Firmware-, Asset-, A/B- und Rollback-Größen sind dokumentiert. |
| WT-07 | Avatarregeln und Konfiguration | Temperaturgrenzen, Mischwetter, Neutral-/Jetzt-Fallback, 20-Minuten-Frische und beide Identitäten bestehen die übernommenen und ergänzten Tests. |
| WT-08 | Morgenzeit und Wakeup | 06-/10-Uhr-Grenzen, Boot im Fenster, 23-/25-Stunden-Tage, ungültige Uhr und Wecken im gewählten Schlafprofil funktionieren nachvollziehbar. |
| WT-09 | Ansichtshoheit und Power | Manuelle Bedienung, offene Dialoge und OTA behalten Vorrang; keine durchfallende Schließ-/Wake-Geste; Display-off und Host-Sleep verhalten sich wie konfiguriert. |
| WT-10 | Radarbild und Zoom, L5-Nachlieferung | Gebührenfreie geeignete direkte Quelle für Deutschland, Standortzentrierung und drei Zoomstufen belegt; Ladefehler, Bildalter und Standortwechsel korrekt dargestellt. |
| WT-11 | Radar-Metadatengate, L5-Nachlieferung | Herkunft, Zeitbezug und Semantik von ETA, Richtung und Geschwindigkeit nachgewiesen; ohne gültige Metadaten bleiben diese Angaben offen und werden nicht erfunden. |
| WT-12 | Speicher und Mischlast je Lieferung | L3/L4: Wetterabruf, Spotify, Website, Cover und Avatar gemeinsam ohne Watchdog, Eingabeverlust, inkonsistente Bild-/Textwechsel oder wachsenden Speicherverbrauch. Vor L5 erneut mit realer Radarlast. |


## Zusätzliche Abnahmen aus dem vollständigen Port

| ID | Test / Störung | Akzeptanz |
| --- | --- | --- |
| HW-03 | ADC/Batterie/Versorgung und OTA-Energiegate | Batterieschätzung nicht mit gesicherter Netzversorgung verwechseln; Freigabeschwelle real vermessen |
| UI-04 | Ring-/Touch-/Modalnavigation, Swipe und Wake | Genau eine passende Aktion; aktive Ansicht besitzt Eingabe; keine Aktion unter Overlay oder Wake |
| UI-05 | Haptik/Effekt/Rate, schnelles Drehen | Passend zu ausgeführter Eingabe, begrenzte Rate, deaktiviert tatsächlich still |
| UI-06 | Coveransicht, Auto-Cover, Arc/Textsicherheit | Keine verlorene Auswahl oder Überlagerung von Modal/OTA/Morgenansicht; freigegebenes Bildlayout |
| SP-07 | Shuffle, Repeat one/off, gegebenenfalls Kontext, Fortschritt | Bekannte Fähigkeiten/Status bestätigt; fehlende Dauer/Radio ohne erfundene Fortschrittswerte |
| SP-08 | Neuer Seek-Pfad, ungültige Position, nicht seekbares Medium | Nur nach Capability und Positionsprüfung; alter bestätigter Anker ersetzt, keine Blindwiederholung |
| CAT-02 | Lange Listen, Prefetch, Kategorie/Trackwechsel während Fetch | Begrenzte Speicherbelegung, stabiler Fokus, alte Seiten abgewiesen, Playlistkontext erhalten |
| CAT-03 | 64/65 Favoriten oder Sender, Entzug/Umbenennen im offenen Picker | Konfigurationslimit erklärt, 65 abgelehnt; Entzug greift backendseitig, IDs bleiben stabil |
| SYS-01 | Web/Knob parallel speichern, kaputte NVS, Save-Power-Cut | Dauerhafter atomarer Commit oder alter gültiger Stand; Konflikte gemeldet; Migration rückrollbar |
| SYS-02 | Dim/Fade/Display-Aus/Wake/DEV-Modus unter Netzlast | Normales Display-Aus erhält Server; Offline-Tiefschlaf ausdrücklich; keine durchfallende Eingabe |
| SYS-03 | Netz fällt aus, Demomodus aus/ein | Echte Ausfälle bleiben sichtbar; Demo nur explizit und erkennbar, keine echte Wiedergabebehauptung |
| SYS-04 | Ungültige Uhr, UTC-Offset, DST/Zeitsprung, Offlineboot | Lokale monotone Timer getrennt, keine falsche Zeit-/TLS-/Morgenannahme |
| SYS-05 | WLAN ok/Internet weg/Auth abgelaufen/Ziel weg/Wetter alt/Peer weg | Unterschiedliche verständliche Zustände; keine pauschal grüne Produktgesundheit |
| WEB-01 | Vollständige Parameter-/Featureliste aus docs13 | Jede ausgelieferte Einstellung im Backend validiert, dauerhaft gespeichert und auf Web/Knob konsistent; Mockaktionen entfernt |
| WEB-02 | Schreibende Verwaltung im Heim-WLAN auf iOS/Android/Desktop | Ohne WLAN-Wechsel erreichbar, vertrauenswürdiger Transport und Gerätidentität; kein Zertifikatswarnungs-Workaround, Erneuerung/Router-/Besitzwechsel qualifiziert |
| OTA-09 | Firmware-, Web- und Wetter-/Avatarassetversion nach Abbruch/Rollback | Signatur/Versionsbindung gültig; altes Image kann zugehörige Assets weiter laden, keine halbe Datenpartition |
| PERF-01 | Ring+Touch, Spotifyrefresh, Website, Cover, Wetter und Avatar; Radar zusätzlich vor L5 | UI-Ziele aus UI-01 je Lieferumfang, keine wachsenden Queues/Lecks/Watchdogs; interne RAM-/DMA-/PSRAM-Spitzen dokumentiert |
| PILOT-02 | Mehrere Geräte/Konten/Wetterorte und zwei Browser | Keine übergreifenden Credentials, Standorte, Kataloge oder Kommandos; Geräteidentität prüfbar |

W0/W-AUTH/W-RAD-0/W-RAD-META sind Quellen-/Betriebsgates aus [Wetter](14-WETTER.md); WT-01–12 sind die funktionalen Abnahmen dazu. W-AUTH ist nur bei tatsächlicher Credentialpflicht anwendbar und erlaubt keinen kostenpflichtigen Wettervertrag. L3/L4 benötigen W0/W1 und die dazugehörigen Tests ohne Pflicht-Radar; L5 zusätzlich die jeweiligen W-RAD-Gates und wiederholte Integrationstests. Vor vollständigem Portabschluss alle angefragten Funktionen einzeln mit Evidenz versehen; gesperrte Fähigkeiten nicht als bestanden zählen. Wenn Radar-ETA/-Vektor fehlen, bleibt dieser Portteil offen, auch wenn L4 bereits abgenommen ist.

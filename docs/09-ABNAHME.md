# Test- und Abnahmekatalog

Befundarten: **automatisiert am Host**, **simuliert**, **auf echter Hardware gemessen**, **physisch gesehen/gehört**, **vertraglich bestätigt**. Sie ersetzen einander nicht. Die heutigen Hosttests beweisen nur Referenzverhalten und Dokument-/Vertragskonsistenz. Alle folgenden Produktgates sind noch offen.

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
| ON-01 | iOS/Android Erststart und captive mini-browser | Vollständige Anmeldung ohne Entwicklerwerkzeuge |
| ON-02 | OAuth state falsch/abgelaufen/erneut verwendet | Ablehnung, kein Konto ersetzt |
| ON-03 | Tokenrefresh fehlt neuer Refresh-Token / 401 / Widerruf | Gültiger Token erhalten bzw. verständlich neu anmelden |
| SEC-01 | Fremde Origin, DNS-Rebinding, CSRF, Sitzung abgelaufen | Schreibzugriff abgelehnt, keine sensitiven GETs/CORS-Freigaben |
| SEC-02 | Export, Log, Diagnose, Factory Reset | Keine Geheimnisse sichtbar; Eigentümerwechsel löscht Konto-/Sitzungsgeheimnisse; Herstelleridentität und Firmware-Vertrauensanker bleiben |
| SP-01 | Zwei Speakerfamilien, mehrere Konten | Sichtbare und steuerbare Modelle einzeln dokumentiert |
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
| PILOT-01 | Zehn neue Nutzer | Einrichtung ohne Entwicklerhilfe; Ziel median <3 min und p90 <5 min, erst messen |

## Nachweisformat

Pro Gate: Commit, Build-/SDKversion, Produkt-/Chiprevision, beide Firmwarehashes, Datum, Testkonto anonymisiert, Lautsprechermodell/-firmware, reproduzierbare Schritte, Soll/Ist, Ergebnis und Nachweisart. Keine Kontozugänge dokumentieren. Bei UI/Audioschritten Beobachter, Foto/Messung bzw. tatsächlichen Hörtest notieren; API-Status ist kein Hörbeweis.

## Referenztests in diesem Framework

`python3 tools/check.py` führt Tests der Hostmodelle aus und prüft Vertragsbeispiele/Referenzen. Die Modelle erhalten Signatur-/Freigabeevidenz als Parameter, **prüfen diese nicht kryptografisch**. Produktionsadapter müssen diese Evidenz aus verifizierten, nicht vom Browser gesetzten Quellen bilden. Die Webdemo simuliert ebenfalls nur Zustände.

Ein einzelner erfolgreicher Hosttest gilt nie als bestandenes Produktgate. Vor Release sind insbesondere echte HTTPS-/PKCE-Interoperabilität, der SDK-Port, Audioelektrik und Flash-/Bootloaderverhalten erforderlich.

Details und Reihenfolge der Sonos-Gates: [Sonos-Prüfplan](12-SONOS-PRUEFUNG.md). Noch keine Sonos-Hardwareabnahme durchgeführt.

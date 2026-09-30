# Native Implementierung und Geräteabnahme

Stand 30.09.2026, Entwicklungsstand `0.1.0-dev.1`. Dieses Dokument ergänzt den Gesamtplan; es erklärt **keine vollständige Feature-, Pilot- oder Produktabnahme**.

## Reale Implementierung

Die ESP-IDF-Projekte in `firmware/s3` und `firmware/companion` ersetzen für diesen Port die HA-/ESPHome-Laufzeit. ESP-IDF ist auf 5.4.3, LVGL auf 9.2.2 fixiert. Beide Images beziehen ihre App-Version aus `VERSION`. Die Website in `firmware/web` wird in das S3-Image eingebettet und verwendet echte Geräteendpunkte. `web/` und `tools/preview.py` bleiben ausdrücklich die ältere Entwurfsvorschau mit Beispieldaten.

| Bereich | Implementierter Stand | Noch erforderlicher Nachweis |
| --- | --- | --- |
| Board | ST77916-Initialisierung aus MIT-Referenz, Display/DMA, Touch, Encoder, Haptik | Tatsächlicher S3-Speicher, Display/Touch/Ausrichtung/Haptik und Latenz am angeschlossenen Board |
| Bedienung | LVGL-Seiten, physischer Setup-Aufruf, QR-Führung, Helligkeit, Displayruhe bei laufendem Netzwerk | Erstnutzerprobe und reale Reaktions-/Speichermessung |
| Website | WLAN, persistente Einstellungen, Playlists/Podcasts als Referenzen, Radioverwaltung, Wetter/Avatar | HTTP-/NVS-Gerätetests, Sitzungslaufzeit, WLAN-Wechsel und Stromausfall |
| Standort | 23.297 lokal durchsuchbare deutsche Ort-/PLZ-Einträge, manuelle Koordinaten als Rückfall | Genauigkeit am Pilotstandort; Quellenabdeckung ist nicht vollständig garantiert |
| Wetter | Direkter MET-Abruf, begrenztes gzip/deflate, Cache/304, Fehler-/Veraltet-Zustände, Stunden/Tage und Avatarentscheidung | Aktueller Abruf auf S3; Flotten-/Quellenqualifikation und DWD-Ausbau bleiben im Wetterplan |
| Wetterbilder/Avatar | 67 Schlüssel, 55 unveränderte JPEGs, begrenzter Decoderworker, echte Wetterbindung, Fotos/analoge Uhr, Morgenavatar/Haarwahl | Physische Geräte-, Leerlauf- und Speichermessung; Hostdecoder samt unabhängiger Farbprüfung bestanden |
| Chipkommunikation | Begrenzte COBS/CRC-Frames, Rollen/Sitzung/Sequenz und Heartbeat | Tatsächliche UART-Verbindung; Heartbeat allein ist keine Pair-OTA-Gesundheitsbestätigung |
| Updateprüfung/Staging | ECDSA-P256/SHA256 über exakte Manifestbytes, zwei Rollen, Hardware/Protokoll/Schema/Version, gestreamte Imagehashes, authentifizierter Upload, persistentes Journal, Companion-Empfänger | Provisionierter Trust, vollständige S3-Aktivierungskoordination und reale Fehler-/Rollbacktests. Staging ist kein ausgeführtes Update; Aktivierung bleibt gesperrt |
| Spotify | Inhalte vorbereitbar, Produktadapter weiter deaktiviert | App-Quotenmodus, kommerzielle Controller-Freigabe, gewählter Kunden-Anmeldeweg, echte Konten-/Roam-/Move-Tests |
| Weitere Ports | Radar, native Sonos-Option und Klinke bleiben im Gesamtplan | Keine Wiedergabe-, Radar- oder Kopfhörerfähigkeit aus dieser Basis ableiten |

## Einrichtung und Sicherheitsgrenze dieses Laborstands

Der physisch geöffnete Geräte-AP hat ein individuell erzeugtes WPA2-Passwort, einen Teilnehmer und ein zeitlich begrenztes Einrichtungsfenster. Die Daten erscheinen am Display. HTTP-Schreibzugriffe prüfen zusätzlich Host/Origin, tatsächliche AP-Zuordnung des TCP-Peers, Sitzung, CSRF und Konfigurationsrevision. Ungültige/mehrdeutige JSON-Eingaben werden vor der atomaren Speicherung abgewiesen. WLAN-Zugangsdaten werden erst nach bestätigter Verbindung gemeinsam gespeichert.

Im normalen Heimnetz ist dieser Stand **nur lesbar und ohne private Konfigurations-/Standortdaten**. Das erfüllt die geforderte sichere, jederzeit schreibbare Heimnetz-Website (Q02/G2) noch nicht. Ein selbstsigniertes Zertifikat mit Browserwarnung wird nicht als fertiges Kunden-Onboarding ausgegeben. Die vorgeschlagene mobile Spotify-Einrichtungs-App ist eine offene Nutzerentscheidung und löst G2 nicht automatisch.

`pw_storage` hält NVS-Verschlüsselung eingeschaltet und initialisiert `nvs`, `settings` und `journal` ausdrücklich mit AES-XTS. Vor der Initialisierung ersetzt es den registrierten Standardprovider durch lokale Flash-Key-Callbacks. Schlüssel entstehen mit aktivierter Hardware-Entropie vor Board-/ADC-/WLAN-Start; kein HMAC-/eFuse-Schlüssel wird erzeugt. Der IDF-Standardgenerator für Flashverschlüsselung wird hier nicht verwendet, da er ohne diese Verschlüsselung feste Rohmuster ergeben würde. Neue Schlüssel werden nur bei vollständig gelöschter Keypartition und drei vollständig gelöschten Ziel-NVS-Partitionen erzeugt; vorhandene Schlüssel benötigen den eigenen Formatmarker. Sonst stoppt der Start, bevor fremde Daten entschlüsselt werden. Kein automatischer Factory-Erase oder Schlüsselersatz. Der IDF-NVS-Treiber kann im regulären Betrieb intern beschädigte Seiten reparieren/verwerfen; er ist kein unveränderlicher Backup-Speicher. **Der Schlüssel liegt im Labor noch in einer auslesbaren Flashpartition.** Schutz gegen physisches Auslesen, Secure Boot, Flashverschlüsselung und sichere Produktion bleiben ein separates Fertigungsgate. Diese irreversiblen Funktionen werden nicht auf einem Recovery-Gerät aktiviert.

## Reproduzierbar bauen und prüfen

```sh
. /Pfad/zu/esp-idf-v5.4.3/export.sh
export UBSAN_OPTIONS=halt_on_error=1
idf.py -C firmware/companion -B build/companion build
idf.py -C firmware/s3 -B build/s3 build
python3 tools/check.py
sh tests/app_native/run.sh
sh tests/storage_native/run.sh
sh tests/weather_native/run.sh
python tests/update_native/run.py
python tests/companion_update_native/run.py
python tests/update_service_native/run.py
python firmware/components/pw_assets/tests/run_host.py --lvgl firmware/s3/managed_components/lvgl__lvgl --cc cc
```

`tests/native/test_protocol.c` wird zusätzlich als Host-C-Test gebaut. Die GitHub-Prüfung führt die nativen Tests und beide ESP-IDF-Builds aus. Erfolgreiche Compiler-/Hosttests ersetzen keine Geräteabnahme. Signaturtests verwenden nur temporäre Testschlüssel und synthetische Image-Fixtures; diese werden nicht auf Geräte geschrieben.

`tests/browser/device_web.py` prüft die echte Website in einem frischen Chromium mit **simulierten API-Antworten**: mobiles Layout, lokale Ortssuche, Speicherversuche mit CSRF/Revision, Erhalt eigener Eingaben bei Konflikten und gesperrte Felder im Nur-Lese-Zustand. Benötigt Playwright 1.58.0 und dessen Chromium. Browserbilder sind Layoutnachweise, keine Fotos eines laufenden Knobs.

Der lokale Abschlusslauf am 30.09.2026 hat beide vollständigen Firmwareprojekte gebaut: S3-App 2.750.432 Byte, Companion-App 278.064 Byte. Zusätzlich bestanden 156 native Konfigurationsprüfungen, 14 Storage-Fehlerszenarien, 91 Wetterprüfungen, 51 Signatur-/Manifestfälle, 1.140 Companion-OTA-Assertions, 98 Staging-Assertions, Protokoll-/Assettests und 35 Frameworktests. Der Browserlauf bestand mit simulierten Geräteantworten. Flash, NVS, ESP-Image-Endprüfung und Stromausfälle werden in den nativen OTA-Tests durch Testcallbacks modelliert; die kryptographische Prüfung verwendet echten C-Code und temporäre Testschlüssel. Die oben genannten physischen Nachweise bleiben offen.

## USB und Recovery

Der Nutzer hat Firmwarearbeit am angeschlossenen JC3636K518C_I_YR1 autorisiert. Bisher über USB identifiziert: ESP32-U4WDH rev3.1, 4 MiB Flash. Die ursprüngliche Firmware wurde vollständig in privaten 128-KiB-Blöcken gelesen und mit `esptool verify_flash` gegen das Gerät verglichen. Große Einzelabrufe scheiterten zuvor mit Übertragungsfehlern. `tools/backup_device.py` macht den Vorgang wiederaufnehmbar und meldet Erfolg erst nach vollständiger Verifikation.

Backups liegen außerhalb des Repositories im privaten Recovery-Verzeichnis. Sie können Zugangsdaten enthalten und dürfen nicht als Buildartefakte veröffentlicht werden. Der Display-Chip benötigt das vom Nutzer angebotene Drehen des USB-Steckers um 180°. Vor dessen erstem Schreiben: tatsächlichen Chip/Flash identifizieren und ebenso vollständig sichern. Ein umgestecktes Gerät wird nicht ohne Bestätigung angenommen.

Die erste Installation dieser neuen Partitionierung benötigt explizit vorbereitete, leere NVS-/Key-/Journalbereiche nach dem verifizierten Backup. Ein bloßes Schreiben von App und Partitionstabelle über bestehende HA-NVS-Daten reicht nicht. Der Laufzeitcode löscht diese Altbereiche nicht selbst. Erstinstallation und spätere OTA sind getrennte Vorgänge; normale Updates erhalten die verschlüsselten Einstellungen und Schlüssel.

Es wurde bis zu diesem dokumentierten Stand **keine neue Firmware auf das Gerät geschrieben und keine eFuse geändert**. Ausstehende reale Prüfungen werden später mit Datum, Imagehash und konkretem Ergebnis ergänzt; nicht nachträglich aus Builds als bestanden abgeleitet.

## Noch offene Kundenanmeldung

Normale Spotify-Anmeldedaten gehören ausschließlich zur Spotify-Anmeldeseite. Käufer sollen weder Entwicklerkonten noch Client-IDs/Secrets eingeben. Der vorhandene private Dashboardlink ist noch kein Nachweis des Quotenmodus oder der Verkaufsfreigabe. Unter „kein eigener externer Dienst“ wird eine mobile Einrichtungs-App mit PKCE zur Entscheidung vorgelegt; die reine lokale HTTP-Website bietet derzeit keinen zulässigen Rückweg. Auch erneute Anmeldung nach Ablauf/Widerruf gehört zum Bedienkonzept. Details und Primärquellen: [Spotify](02-SPOTIFY.md).

# Native Implementierung und Geräteabnahme

Stand 02.10.2026, Entwicklungsstand `0.1.0-dev.15`. Dieses Dokument ergänzt den Gesamtplan; es erklärt **keine vollständige Feature-, Pilot- oder Produktabnahme**.

## Reale Implementierung

Die ESP-IDF-Projekte in `firmware/s3` und `firmware/companion` ersetzen für diesen Port die HA-/ESPHome-Laufzeit. ESP-IDF ist auf 5.4.3, LVGL auf 9.2.2 fixiert. Beide Images beziehen ihre App-Version aus `VERSION`. Die Website in `firmware/web` wird in das S3-Image eingebettet und verwendet echte Geräteendpunkte. `web/` und `tools/preview.py` bleiben ausdrücklich die ältere Entwurfsvorschau mit Beispieldaten.

| Bereich | Implementierter Stand | Noch erforderlicher Nachweis |
| --- | --- | --- |
| Board | ST77916-Initialisierung aus MIT-Referenz, Display/DMA, Touch, Encoder, Haptik; S3 rev0.2 mit 16 MiB Flash und 8 MiB PSRAM über USB identifiziert | Display/Touch/Ausrichtung/Haptik und Latenz am angeschlossenen Board |
| Bedienung | LVGL-Seiten, physischer Setup-Aufruf, QR-Führung, Helligkeit, Displayruhe bei laufendem Netzwerk | Erstnutzerprobe und reale Reaktions-/Speichermessung |
| Website | WLAN, persistente Einstellungen, Playlists/Podcasts als Referenzen, Radioverwaltung, Wetter/Avatar; dev.7-Seitenaufbau und Heim-WLAN-Einrichtung vom Nutzer am iPhone bestätigt | Weitere HTTP-/NVS-Gerätetests, Sitzungslaufzeit, WLAN-Wechsel und Stromausfall |
| Standort | 23.297 lokal durchsuchbare deutsche Ort-/PLZ-Einträge, manuelle Koordinaten als Rückfall | Genauigkeit am Pilotstandort; Quellenabdeckung ist nicht vollständig garantiert |
| Wetter | Direkter MET-Abruf, begrenztes gzip/deflate, Cache/304, Fehler-/Veraltet-Zustände, Stunden/Tage und Avatarentscheidung | Aktueller Abruf auf S3; Flotten-/Quellenqualifikation und DWD-Ausbau bleiben im Wetterplan |
| Wetterbilder/Avatar | 67 Schlüssel, 55 unveränderte JPEGs, begrenzter Decoderworker, echte Wetterbindung, Fotos/analoge Uhr, Morgenavatar/Haarwahl | Physische Geräte-, Leerlauf- und Speichermessung; Hostdecoder samt unabhängiger Farbprüfung bestanden |
| Chipkommunikation | Begrenzte COBS/CRC-Frames, Rollen/Sitzung/Sequenz und Heartbeat | Tatsächliche UART-Verbindung; Heartbeat allein ist keine Pair-OTA-Gesundheitsbestätigung |
| Updateprüfung/Staging | ECDSA-P256/SHA256 über exakte Manifestbytes, zwei Rollen, Hardware/Protokoll/Schema/Version, gestreamte Imagehashes, authentifizierter Upload, persistentes Journal, Companion-Empfänger | Provisionierter Trust, vollständige S3-Aktivierungskoordination und reale Fehler-/Rollbacktests. Staging ist kein ausgeführtes Update; Aktivierung bleibt gesperrt |
| Spotify | Direkter S3-Web-API-Provider im expliziten Laborprofil; USB-PKCE, Refresh, Ausgabewahl, Play/Pause/Next/Previous/Volume, Playliststart und echte UI-/Websitebindung; erste reale Anmeldung auf dev.8 geräte- und nutzerbestätigt; nach Appupdate/Neustart auf dev.9 ohne Neuanmeldung zweimal zwei Geräte erfolgreich geparst; Standardprofil deaktiviert | Weitere Refresh-/Kontowechsel- und Stromausfalltests, Knob-Auswahl und Roam-/Move-Steuerung, erweiterte Medienfunktionen, kommerzielle Controller-Freigabe und mobiler Kunden-Anmeldeweg |
| Weitere Ports | Radar, native Sonos-Option und Klinke bleiben im Gesamtplan | Keine Wiedergabe-, Radar- oder Kopfhörerfähigkeit aus dieser Basis ableiten |

## Einrichtung und Sicherheitsgrenze dieses Laborstands

Der physisch geöffnete Geräte-AP hat ein individuell erzeugtes WPA2-Passwort, einen Teilnehmer und ein zeitlich begrenztes Einrichtungsfenster. Die Daten erscheinen am Display. HTTP-Schreibzugriffe prüfen zusätzlich Host/Origin, tatsächliche AP-Zuordnung des TCP-Peers, Sitzung, CSRF und Konfigurationsrevision. Ungültige/mehrdeutige JSON-Eingaben werden vor der atomaren Speicherung abgewiesen. WLAN-Zugangsdaten werden erst nach bestätigter Verbindung gemeinsam gespeichert.

Im Standardprofil ist dieser Stand im normalen Heimnetz **nur lesbar und ohne private Konfigurations-/Standortdaten**. Die ausdrücklich autorisierte dev.15-Pilotausnahme mit physischer Freigabe ist unten beschrieben. Das erfüllt die geforderte sichere, jederzeit schreibbare Heimnetz-Website (Q02/G2) noch nicht. Ein selbstsigniertes Zertifikat mit Browserwarnung wird nicht als fertiges Kunden-Onboarding ausgegeben. Die vorgeschlagene mobile Spotify-Einrichtungs-App ist eine offene Nutzerentscheidung und löst G2 nicht automatisch.

`pw_storage` hält NVS-Verschlüsselung eingeschaltet und initialisiert `nvs`, `settings` und `journal` ausdrücklich mit AES-XTS. Vor der Initialisierung ersetzt es den registrierten Standardprovider durch lokale Flash-Key-Callbacks. Schlüssel entstehen mit aktivierter Hardware-Entropie vor Board-/ADC-/WLAN-Start; kein HMAC-/eFuse-Schlüssel wird erzeugt. Der IDF-Standardgenerator für Flashverschlüsselung wird hier nicht verwendet, da er ohne diese Verschlüsselung feste Rohmuster ergeben würde. Neue Schlüssel werden nur bei vollständig gelöschter Keypartition und drei vollständig gelöschten Ziel-NVS-Partitionen erzeugt; vorhandene Schlüssel benötigen den eigenen Formatmarker. Sonst stoppt der Start, bevor fremde Daten entschlüsselt werden. Kein automatischer Factory-Erase oder Schlüsselersatz. Der IDF-NVS-Treiber kann im regulären Betrieb intern beschädigte Seiten reparieren/verwerfen; er ist kein unveränderlicher Backup-Speicher. **Der Schlüssel liegt im Labor noch in einer auslesbaren Flashpartition.** Schutz gegen physisches Auslesen, Secure Boot, Flashverschlüsselung und sichere Produktion bleiben ein separates Fertigungsgate. Diese irreversiblen Funktionen werden nicht auf einem Recovery-Gerät aktiviert.

## Reproduzierbar bauen und prüfen

```sh
. /Pfad/zu/esp-idf-v5.4.3/export.sh
export UBSAN_OPTIONS=halt_on_error=1
idf.py -C firmware/companion -B build/companion build
idf.py -C firmware/s3 -B build/s3 -D SDKCONFIG="$PWD/build/s3/sdkconfig" build
# Nur explizite Laborevaluierung, keine Produktfreigabe:
idf.py -C firmware/s3 -B build/s3-lab-usb -D SDKCONFIG="$PWD/build/s3-lab-usb/sdkconfig" \
  -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.spotify-lab' build
python tools/check_firmware_versions.py build/companion build/s3 build/s3-lab-usb
python3 tools/check.py
sh tests/app_native/run.sh
sh tests/http_native/run.sh
sh tests/storage_native/run.sh
sh tests/weather_native/run.sh
sh tests/setup_native/run.sh
sh firmware/components/pw_spotify/tests/run.sh
python tests/update_native/run.py
python tests/companion_update_native/run.py
python tests/update_service_native/run.py
python firmware/components/pw_assets/tests/run_host.py --lvgl firmware/s3/managed_components/lvgl__lvgl --cc cc
```

`tests/native/test_protocol.c` wird zusätzlich als Host-C-Test gebaut. Die GitHub-Prüfung ist um Spotify-Provider, USB-Protokoll, Anmeldehelfer und das getrennte S3-Laborprofil erweitert. Den tatsächlich ausgeführten CI-Stand immer am zugehörigen Commit prüfen. Erfolgreiche Compiler-/Hosttests ersetzen keine Geräteabnahme. Signaturtests verwenden nur temporäre Testschlüssel und synthetische Image-Fixtures; diese werden nicht auf Geräte geschrieben.

`tests/browser/device_web.py` prüft die echte Website in einem frischen Chromium mit **simulierten API-Antworten**: mobiles Layout, lokale Ortssuche, Speicherversuche mit CSRF/Revision, Erhalt eigener Eingaben bei Konflikten und gesperrte Felder im Nur-Lese-Zustand. Benötigt Playwright 1.58.0 und dessen Chromium. Browserbilder sind Layoutnachweise, keine Fotos eines laufenden Knobs.

Der lokale Abschlusslauf am 30.09.2026 hat beide vollständigen Firmwareprojekte gebaut: S3-App 2.750.432 Byte, Companion-App 278.064 Byte. Zusätzlich bestanden 156 native Konfigurationsprüfungen, 14 Storage-Fehlerszenarien, 91 Wetterprüfungen, 51 Signatur-/Manifestfälle, 1.140 Companion-OTA-Assertions, 98 Staging-Assertions, Protokoll-/Assettests und 35 Frameworktests. Der Browserlauf bestand mit simulierten Geräteantworten. Flash, NVS, ESP-Image-Endprüfung und Stromausfälle werden in den nativen OTA-Tests durch Testcallbacks modelliert; die kryptographische Prüfung verwendet echten C-Code und temporäre Testschlüssel. Die oben genannten physischen Nachweise bleiben offen.

## USB und Recovery

Der Nutzer hat Firmwarearbeit am angeschlossenen JC3636K518C_I_YR1 autorisiert. Bisher über USB identifiziert: ESP32-U4WDH rev3.1, 4 MiB Flash. Die ursprüngliche Firmware wurde vollständig in privaten 128-KiB-Blöcken gelesen und mit `esptool verify_flash` gegen das Gerät verglichen. Große Einzelabrufe scheiterten zuvor mit Übertragungsfehlern. `tools/backup_device.py` macht den Vorgang wiederaufnehmbar und meldet Erfolg erst nach vollständiger Verifikation.

Backups liegen außerhalb des Repositories im privaten Recovery-Verzeichnis. Sie können Zugangsdaten enthalten und dürfen nicht als Buildartefakte veröffentlicht werden. Der Nutzer hat das Drehen des USB-Steckers am 01.10.2026 bestätigt. Danach wurde der Display-Chip tatsächlich als ESP32-S3 rev0.2, 16 MiB Flash und 8 MiB eingebetteter PSRAM am nativen USB-Serial/JTAG-Anschluss identifiziert. Nach einem bestätigten Neuanschließen wurde sein vollständiger Originalflash mit `esptool verify_flash` erfolgreich verglichen: **16.777.216 Byte**, SHA-256 `eaf165e86339406afe671d50a16d2227c6111150f63db3e6420a738150c39586`. Die anfangs durch Neustarts veränderten Original-NVS-Blöcke `0x9000`/`0xa000` wurden erneut gelesen; ältere Bytes blieben privat erhalten. Der verbesserte Backuphelfer lässt die Originalapp zwischen Reads und Verify nicht starten und verwirft veraltete Erfolgsmarker. Die rein lesende Sicherheitsprüfung ergab Secure Boot und Flashverschlüsselung deaktiviert.

Die erste Installation dieser neuen Partitionierung benötigt explizit vorbereitete, leere NVS-/Key-/Journalbereiche nach dem verifizierten Backup. Ein bloßes Schreiben von App und Partitionstabelle über bestehende HA-NVS-Daten reicht nicht. Der Laufzeitcode löscht diese Altbereiche nicht selbst. Erstinstallation und spätere OTA sind getrennte Vorgänge; normale Updates erhalten die verschlüsselten Einstellungen und Schlüssel.

Am 01.10.2026 wurde nach dem qualifizierten Backup der S3-Flash gelöscht und die Laborfirmware `0.1.0-dev.3` samt Bootloader, Partitionstabelle und OTA-Initialdaten geschrieben. Alle vier Images wurden gegen den Flash verifiziert. App: **2.785.312 Byte**, SHA-256 `c78376de22bce6ffdca2be94b1c74143ea5017c90a857262c999c25de176d4b0`. Lokale Diagnoseinstallation, keine Produktlieferung. **Keine eFuse geändert.** Der Companion trägt weiterhin seine Originalfirmware; gemeinsamer UART-/Pair-OTA-Betrieb ist noch nicht abgenommen. Start-, USB-, Anzeige- und WLAN-Ergebnisse werden separat ergänzt.

## Noch offene Kundenanmeldung

Normale Spotify-Anmeldedaten gehören ausschließlich zur Spotify-Anmeldeseite. Käufer sollen weder Entwicklerkonten noch Client-IDs/Secrets eingeben. Die vorhandene HomeAssistant-App wurde unverändert belassen. Nach ausdrücklicher Nutzerzustimmung wurde am 01.10.2026 die getrennte **PassionWave RotaryKnob Lab**-App angelegt und geprüft: ausschließlich Web API, Development Mode, 180 Tage Refresh-Laufzeit, Redirect `http://127.0.0.1:8766/callback`. Öffentliche Metadaten stehen in [spotify-lab.json](../profiles/spotify-lab.json); Client-Secrets wurden nicht aufgerufen. Verkaufsfreigabe fehlt weiterhin. Unter „kein eigener externer Dienst“ wird eine mobile Einrichtungs-App mit PKCE zur Entscheidung vorgelegt; die reine lokale HTTP-Website bietet derzeit keinen zulässigen Rückweg. Auch erneute Anmeldung nach Ablauf/Widerruf gehört zum Bedienkonzept. Details und Primärquellen: [Spotify](02-SPOTIFY.md).

## Spotify-Laborstand dev.2 und nächste Geräteprobe

Der S3 ruft Spotify selbst per HTTPS auf. PKCE-Verifier und Tokens bleiben auf ihm; nur der einmalige Autorisierungscode wird vom vorübergehend laufenden [Desktop-USB-Helfer](../tools/spotify_setup/README.md) weitergereicht. Kundenpasswörter werden ausschließlich auf der Spotify-Seite eingegeben. Der Helfer ist ein Pilotwerkzeug und ersetzt noch keine qualifizierte mobile Kundeneinrichtung. Die passende App-/Redirect-Registrierung ist seit dev.3 vorhanden; der echte Geräteversuch bleibt offen.

Auf dem Display: Lautsprecher wählen, Play/Pause, vor/zurück, Lautstärke am Ring und gespeicherte Playlists. In der geschützten Gerätewebsite: dieselben gezielten Aktionen, Geräte-/Verbindungsstatus und Trennen der Verknüpfung. Podcasts werden weiterhin nur verwaltet; nicht belegte Startaktionen sind sichtbar deaktiviert. Tokens, Verifier und rohe Spotify-Fehlertexte gelangen nicht in die Website, Konfigurationsexporte oder Logs. Die normale Heimnetz-Website bleibt bis G2 lesend; die lokale Gerätebedienung benötigt kein offenes Setupfenster.

Prüfstand am 01.10.2026:

- S3-Standardprofil: **2.774.064 Byte**, Spotify deaktiviert. S3-Laborprofil: **2.789.808 Byte**. Companion: **278.064 Byte**. Alle drei ESP-IDF-Builds erfolgreich; lokale Diagnoseartefakte, keine veröffentlichte oder geflashte Produktlieferung. Beide S3-Profile behalten NVS-Verschlüsselung.
- **285 Modell-Assertions** mit echten cJSON-/mbedTLS-Implementierungen sowie **529 Worker-Assertions** mit dem echten Provider-C und simulierten IDF-HTTP-/NVS-/Taskgrenzen; ASan/UBSan. Die Workerprüfungen umfassen explizite Zieladressen, veraltete/in-flight Auswahl, `204` ohne erfundene Wiedergabe, fehlende Bestätigung ohne Next-Wiederholung, `401`/Refresh, `429`, Tokenwiderruf, Speicherfehler, OAuth-Abbruch und konkrete Anmeldebestätigung.
- **36 USB-Parserprüfungen**, **17 Python-/HTTP-/Serialtests**, **35 Frameworktests**. HTTP-Tests binden tatsächlich auf Loopback; USB und Spotify bleiben simuliert.
- Beide echten Websites in isoliertem Chromium geprüft: geschützte Spotify-Bedienung, während des Zielwechsels zurückgehaltene alte Statusantwort, keine automatische Skip-Wiederholung, Playlistreferenzen, gesperrte Podcasts, Nur-Lese-Zugriff sowie OAuth-Rückkehr mit Strict-Cookie. Mobiles Layout und native 360×360-LVGL-Renderings visuell geprüft; keine Gerätefotos.

Nächste Schritte bis zum ersten echten Wiedergabenachweis:

1. Lab-App und Redirect sind angelegt, die Client-ID in Firmware/Helfer/öffentlichem Profil automatisch abgeglichen. Testkonto und Premium-Voraussetzung beim tatsächlichen Login prüfen.
2. S3 identifiziert, vollständiges Originalbackup und erste Laborinstallation verifiziert. Boot, native USB-Kommunikation und Verhalten beim erneuten Anschließen qualifizieren. Keine eFuses ändern.
3. WLAN, Display/Touch/Ring/Haptik und Speicher real prüfen. Spotify per geschütztem USB-Setup anmelden; falsche/abgebrochene Anmeldung und Stromverlust ebenfalls prüfen.
4. Sonos Roam und Move in der von Spotify gelieferten Geräteliste wählen. Auf jedem Ziel Playliststart, Pause, Next/Previous, Lautstärke und tatsächliche hörbare Ausgabe prüfen. Trennen und Neustart dürfen keine selbsttätige Wiedergabe auslösen.
5. Kunden-Onboarding, sichere schreibende Heimnetz-Website, Cover/erweiterte Medienbedienung und vollständiges Pair-OTA gemäß Gesamtplan vervollständigen; Radar folgt in L5.

**Offen:** kein reales OAuth-Ergebnis und kein hörbarer Connect-Nachweis. Dieser Zwischenstand ist keine Bestätigung aller Features und keine Freigabe zum Verkauf.

## Geräteversuch dev.3 / dev.4 am 01.10.2026

Die getrennte Lab-App ist angelegt. Client-ID und Redirect werden zwischen
Firmware, Desktophelfer und öffentlichem Profil abgeglichen. Der tatsächliche
Display-S3 verwendet **native USB Serial/JTAG**, nicht UART0. Das Laborprofil
wählt diesen Anschluss auch für die Konsole; genau ein Task liest Setupframes.
Das künftige [Setup-Repository](17-SETUP-REPOSITORY.md) erhält einen gepinnten
USB-Vertrag und dasselbe öffentliche Profil. Noch kein zweites Repo angelegt.

Der erste reale dev.3-Start erkannte 8 MiB PSRAM, CST816 `0xb6` und DRV2605 und
startete die lokalen Dienste. Fünf aufeinanderfolgende HELLO-Antworten und der
geschützte USB-Status waren gültig. Die Hintergrundbeleuchtung meldete dabei
einen fehlenden LEDC-Fade-Service. dev.4 initialisiert den von der threadsicheren
IDF-Duty-API benötigten Dienst und Kanal bei 0 Prozent vor den App-/UI-Tasks.
Zusätzlich verhindert dev.4 einen ungültigen öffentlichen HTTP-Status nach einem
Timeout vor dem ersten Antwortheader. Der Helfer korreliert positive Antworten
jetzt mit der angefragten Methode; `accepted:false` oder eine HELLO-Antwort sind
kein Callback-ACK.

dev.4 wurde am S3 **nur als Appupdate bei erhaltenen Einstellungen/Schlüsseln**
geschrieben und verifiziert: **2.786.528 Byte**, SHA-256
`690ea1b66736bee639a4bd4b4a6546b8f34f8d277a176ab5fc983e0ea01455c6`.
Der nachfolgende Start erreichte `app_main` und lokale Dienste ohne LEDC-Fehler,
Panic oder Abbruch im zwölfsekündigen USB-Logfenster. Zehn anschließende echte
HELLO-Abfragen sowie der geschützte Status bestätigten dev.4 und ein offenes
Setupfenster. Spotify meldete korrekt `unlinked`. Diese kurzen Beobachtungen
sind kein Dauerlauf; lesbares Display, Touchbedienung, WLAN-Einrichtung und
hörbare Wiedergabe brauchen weiterhin eine physische Bestätigung.

Alle drei damals gebauten Artefakte tragen nach erneuter Konfiguration **dev.4**:
Standard-S3 **2.775.264 Byte**, Labor-S3 **2.786.528 Byte**, Companion
**278.064 Byte**. Beim Test fiel auf, dass ein bestehender Buildordner Änderungen
an der gemeinsamen VERSION-Datei zuvor nicht automatisch übernommen hatte.
Beide CMake-Projekte führen sie jetzt ausdrücklich als Konfigurationsabhängigkeit.
`tools/check_firmware_versions.py` prüft zusätzlich die Version/Projektidentität
aller drei tatsächlichen App-Binaries mit `esptool image_info` gegen Root-VERSION
und CMake-Metadaten; dieser Lauf bestand. Zwölf Hosttests sichern insbesondere
veraltete Metadaten, abweichende Images und fehlende Artefakte ab. CI führt die
Prüfung nach jedem Build aus.
Der erste CI-Lauf mit esptool 4.10.0 fand einen Unterschied zum lokalen 4.12.0:
ältere `image_info`-Ausgaben enthalten abschließende NUL-Paddingbytes in den
Textfeldern. Die Auswertung entfernt nur dieses Padding. Beide Toolversionen
bestanden danach den Vergleich aller drei realen Binaries; abweichende Versionen,
eingebettete NUL-Zeichen und zusätzlicher Text bleiben Fehler.
Die früher genannten Dateigrößen allein belegten keine aktualisierte eingebettete
Versionsnummer.

Neue Prüfungen: **46 Framework-/Vertragstests**, darin gemeinsame synthetische
USB-/OAuth-Goldens gegen den echten Python-Helfer und **37 Requestvektoren** gegen
den echten C-Decoder mit ASan/UBSan; **17 Helfer-/Loopbacktests**, **8 Backup-
Fehlerfalltests**, **285 Spotify-Modell- und 623 Worker-Assertions**, **36 native
USB-Parserprüfungen**. Die aktualisierte Einrichtungswebsite bestand das isolierte
Chromium-Szenario mit simuliertem USB/Spotify. Die tatsächlich ausgeführten
USB-HELLO-/Statusabfragen oben sind davon getrennte Gerätenachweise.

Der Desktophelfer gibt den Anmeldeknopf erst frei, wenn der echte USB-Status die
WLAN-Verbindung bestätigt. Davor führt er zum WLAN-QR-Code am Display. Fällt WLAN
aus, erscheint auch bei noch gespeicherter Spotify-Verknüpfung keine grüne
Bereitschaftsanzeige. Beide Übergänge und der anschließende OAuth-Ablauf wurden
im isolierten Chromium mit simulierten Netzwerkzuständen geprüft.

## Gerätewebsite-Korrektur dev.5 am 01.10.2026

Der Nutzer erreichte `http://192.168.4.1/`, erhielt aber `400` / Fehlercode `host`
statt der Konfigurationsseite. Ursache im tatsächlich konfigurierten IDF 5.4.3:
HTTPD öffnet bei aktivem IPv6 einen Dualstack-Listener; lwIP liefert dessen IPv4-
Verbindungen bei `getsockname`/`getpeername` als `::ffff:a.b.c.d`. Der bisherige
`sockaddr_in`-Puffer war zu klein und las statt der IPv4-Adresse das IPv6-Flowfeld.
Zusätzlich lehnte die AP-Prüfung die unerwartete Adressfamilie ab.

`pw_http_socket.c` liest jetzt beide Endpunkte vollständig in `sockaddr_storage`
und akzeptiert ausschließlich IPv4 oder echte IPv4-Mapped-IPv6-Adressen. Der Host
muss weiterhin zur tatsächlichen lokalen Adresse passen. Setupfenster, lokale
AP-Adresse, Subnetz, assoziierter DHCP-Client, Sitzung, Origin und CSRF bleiben
verpflichtend. Native IPv6, verkürzte Adressen, fremde Hosts und andere Ports
werden nicht durch diese Korrektur freigegeben. Alle JSON-Ausgabepfade deklarieren
zusätzlich UTF-8, auch für die direkte Fehlerseitenanzeige. Die gemeldeten
verunstalteten Umlaute entsprechen einer falschen Interpretation korrekt
kodierter UTF-8-Quellbytes; der verwendete Handy-Browser wurde nicht instrumentiert.

**209 native HTTP-Adress-/Hostprüfungen mit ASan/UBSan bestanden**, darunter eine
echte reine Loopback-Verbindung von IPv4 zu einem IPv6-Dualstack-Socket. Sie zeigt
den abgeschnittenen alten Puffer und prüft die tatsächlichen neuen Sockethelfer.
Außerdem bestanden erneut 156 Konfigurationsprüfungen und 46 Framework-/Vertrag-
tests. Alle drei Profile gebaut und tatsächliche Binaryversionen mit dev.5
abgeglichen: Standard-S3 **2.775.456 Byte**, Labor-S3 **2.786.720 Byte**, Companion
**278.064 Byte**. Der neue Sockettest ist Teil der nativen CI.

Das S3-Laborimage wurde bei erhaltenen Einstellungen/Schlüsseln geschrieben und
gegen den Flash verifiziert: SHA-256
`125533d6a1fbb239fba75ead6c715cce83fea93e95e4f0b80b3d55801bac85a6`.
Start erreichte Board-/lokale Dienste und `app_main`; echtes USB-HELLO bestätigte
dev.5 mit offenem Setupfenster. Das Originalbackup blieb erhalten, keine eFuses
geändert. Nach dem Neustart muss das Handy den neu erzeugten AP-QR-Code verwenden.
**Erneuter Webaufruf, schreibende WLAN-Einrichtung und Spotify-Anmeldung sind
noch vom Nutzer bzw. am Gerät zu bestätigen.** Ein bestandener Hosttest ersetzt
diese Rückmeldung nicht. Der vorherige Commit `85bb206` hatte vor dieser Korrektur
zwei vollständig erfolgreiche GitHub-Läufe; sie belegten diesen Gerätefehler nicht.


## Langsamer Safari-Seitenaufbau / dev.6 am 01.10.2026

Der Nutzer bestätigte den Aufruf mit iPhone/Safari im PassionWave-WLAN: Nach
dev.5 kam die Website an, aber extrem langsam und mit unvollständiger Gestaltung.
Das ist noch keine erfolgreiche Webabnahme. Im ausgelesenen S3-Log standen
HTTP-Sendezeitüberschreitungen (`errno 11`) und abgebrochene Verbindungen
(`errno 104`). Der beim Öffnen der USB-Diagnose beobachtete Neustart trug den
Grund `USB_UART_CHIP_RESET`; dieser Mitschnitt zeigte keine Firmware-Panic.
Die Ursache des schlechten Funk-/Browsertransports ist damit noch nicht
abschließend isoliert.

dev.6 verkleinert HTML, CSS und JavaScript durch deterministische Kompression
beim Build von **88.893 auf 24.560 Byte (−72,4 Prozent)**. Der S3 liefert fertige
Binär-gzip-Dateien mit ihrem ursprünglichen MIME-Typ und exakter Länge; er
komprimiert nicht während der Anfrage. Externe Dateien und Fonts gibt es nicht.
Die große Ortsdatenbank wird weiterhin erst bei der Wetterseite/-suche geladen.
`TCP_NODELAY` vermeidet zusätzliche Wartezeiten zwischen den vielen kleinen
HTTPD-Header-Sendungen. Sechs HTTP-Verbindungen statt vier und 16 globale
lwIP-Sockets lassen Platz für den Browser sowie DNS/NTP/Provider. Sendezeitlimit,
Host-/AP-/Sitzungsregeln und CSP bleiben erhalten. Die Firmware protokolliert
pro statischer Webdatei nur Namen, komprimierte Bytezahl, Dauer und Sendestatus.
Keine Cookies, URLs mit Suchparametern, Zugangsdaten oder Benutzereinstellungen.

WLAN-Stromsparmodus wird nicht als Ursache dieses AP-Fehlers behauptet oder
auf Verdacht verändert: Das [gepinnt dokumentierte Modem-Sleep-Verhalten](https://docs.espressif.com/projects/esp-idf/en/v5.4.3/esp32s3/api-guides/wifi.html#esp32-s3-wi-fi-power-saving-mode)
betrifft verbundene Stationen im reinen STA-Modus.

Prüfungen: **216 native Socket-/Hostprüfungen** mit ASan/UBSan einschließlich
realem TCP_NODELAY-Abgleich; **51 Frameworktests**, davon fünf Kompressions-/
Rebuild-/Fehlerfalltests. Echte temporäre Loopback-HTTP-Tests mit **Chromium und
WebKit** bestätigen vollständige Gzip-Dekodierung, MIME/nosniff/CSP, CSS,
JavaScript und mobiles Layout. Die dortigen API-Antworten sind simuliert.
Alle drei Profile bauen mit dev.6; die Gzip-Bytefolgen und exakten Start-/Endsymbole
wurden in den finalen Standard-/Labor-ELFs und App-Binaries abgeglichen.
**Ein schneller, vollständiger Safari-Aufruf am realen Gerät bleibt zu messen
und vom Nutzer zu bestätigen.**


Das S3-Laborimage wurde anschließend als Appupdate mit erhaltenen Einstellungen
und Schlüsseln geschrieben und vollständig gegen den Flash verifiziert:
**2.722.640 Byte**, SHA-256
`f9cf610fac19c3836d525a6908b2a21d14ccf51a1f1ef3323764c5b3e8109723`.
Board und lokale Dienste starteten; echtes USB-HELLO bestätigte dev.6 mit offenem
Setupfenster. Die Originalbackup-Prüfsumme wurde vor dem Schreiben erneut
abgeglichen. Keine eFuses oder Companion-Firmware geändert. Eine offen gehaltene
USB-Diagnose erfasst jetzt die Webdatei-Übertragungen beim angefragten
Safari-Retest. Beim Wechsel zwischen USB-Programmen wurde auf diesem Rechner ein Reset
beobachtet; deshalb bleibt die Verbindung während dieser Probe geöffnet.


### Gesicherter Abendstand / Fortsetzung am 02.10.2026

Auf Nutzerwunsch endet die Gerätearbeit hier bis morgen früh. dev.6 bleibt auf
dem S3 installiert; Quellstand `4db058d` ist im verbundenen Repository und
Entwurfs-PR gesichert. Die USB-Diagnose wurde geordnet beendet und ihre Protokolle
privat neben dem verifizierten Image abgelegt. Bis zum Ende der Aufzeichnung
wurden **keine neuen Webdatei-Abrufe** beobachtet; daher liegt noch kein gemessener
Safari-Ladezeitnachweis für dev.6 vor. Der lokale Spotify-Helfer ist ebenfalls
beendet. Es läuft kein Flash-/Updatevorgang; das Gerät darf vom USB getrennt werden.

Fortsetzung: Einrichtung am Knob bei Bedarf durch drei Sekunden Berühren erneut
öffnen, aktuellen WLAN-QR-Code verwenden und `http://192.168.4.1/` mit iPhone/Safari
aufrufen. Zuerst vollständigen, schnellen Seitenaufbau und erfolgreiche
WLAN-Einrichtung bestätigen. Anschließend lokalen USB-Helfer wieder starten,
echtes Spotify-OAuth und danach gezielte Roam-/Move-Wiedergabe prüfen. Ein Start
für morgen wurde nicht automatisch terminiert. Die vollständige Feature- und
Produktabnahme bleibt offen.


## Safari-Übertragungsstau / dev.7 am 02.10.2026

Der reale dev.6-Retest mit iPhone/Safari im geschützten Geräte-WLAN scheiterte
weiterhin. Drei aufgezeichnete `app.js`-Übertragungen (je 14.582 komprimierte
Byte) brachen nach **10.851 / 10.678 / 10.874 ms** mit `errno 11` und
`ESP_ERR_HTTPD_RESP_SEND` ab. HTML (5.408 Byte) und CSS (4.570 Byte) meldeten
vorher kurze Sendaufrufe. Diese passen aber in den **5.760-Byte-TCP-Sendepuffer**;
`ESP_OK` bestätigt die Annahme in den Netzwerkstack, nicht den vollständigen
Empfang im Browser. lwIP gibt beim größeren Sendaufruf nach dem ersten
Fünfsekundenlimit einen Teilfortschritt zurück, beim nächsten ohne Fortschritt
einen Timeout. Das erklärt die beobachteten rund elf Sekunden, beweist allein
aber noch keine konkrete Funkursache.

Als gezielter Vergleichskandidat deaktiviert dev.7 `ESP_WIFI_AMPDU_RX_ENABLED`
und `ESP_WIFI_AMPDU_TX_ENABLED` entsprechend der [Espressif-Empfehlung für
SoftAP-/Smartphone-Kompatibilitätsprobleme](https://github.com/espressif/esp-faq/blob/master/docs/en/software-framework/wifi.rst).
Der sonstige WLAN-PHY-Modus bleibt erhalten. Die Option wirkt auch auf die
Heimnetz-Station; möglichen Einfluss auf späteren OTA-Durchsatz separat messen.
HTTP-Code, Webdateien, TCP-Puffer, Timeouts und Stromsparmodus bleiben für diesen
Vergleich unverändert. Das Bootlog nennt die tatsächlich an `esp_wifi_init`
übergebenen RX-/TX-Werte. Die effektiven Konfigurationen beider S3-Buildordner
wurden auf deaktiviertes AMPDU und unveränderte 5.760 TCP-Bytes geprüft;
`SDKCONFIG_DEFAULTS` allein hätte vorhandene Buildwerte nicht überschrieben.

Alle drei Profile wurden gebaut und ihre tatsächlichen Binärversionen auf
dev.7 abgeglichen. Ein erfolgreicher iPhone-Abruf bleibt nach dem Schreiben
physisch zu bestätigen. Dieser Kandidat ist noch kein nachgewiesener Abschluss
des Ladezeitfehlers; größere Sendepuffer oder längere Wartezeiten wurden nicht
als Ersatz für funktionierenden Transport eingeführt.


Zusätzlich bestanden 25 native HTTP-Antwortszenarien mit ASan/UBSan. Sie verwenden
die unveränderten aktuellen Asset-/Headerfunktionen und den gepinnten originalen
IDF-Antwortwriter, aber einen simulierten Sendekanal. Alle vier Gzip-Assets
bleiben bei kurzen Writes bytegenau vollständig; simulierte Abbrüche liefern
korrekt einen Fehler. In diesen Szenarien zeigte sich kein Format- oder Partial-Write-Fehler.
Der Test simuliert jedoch keine Funkverbindung. Dieser Test läuft künftig in CI.
Die 51 Frameworkprüfungen bestanden ebenfalls.

dev.7 wurde auf demselben identifizierten S3 geschrieben und gegen den Flash
verifiziert, ohne Einstellungen/Schlüssel zu ändern: **2.722.656 Byte**, SHA-256
`15b68eca9c840f6d3f49367f8f1e789f149c354c2dcde90178cf4886b77ef602`.
Bootlog bestätigt **AMPDU RX=0 / TX=0**, lokale Dienste und Rückkehr aus
`app_main`; USB-HELLO bestätigt dev.7 und offenes Setupfenster. Die Original-
Recovery-Prüfsumme wurde vor dem Schreiben erneut abgeglichen. Companion bleibt
auf Originalfirmware. Der angefragte Safari-Vergleich wird separat protokolliert.


### Erfolgreicher iPhone-Vergleich und WLAN-Einrichtung

Der Nutzer bestätigte anschließend ausdrücklich **„Ja, jetzt schnell und
vollständig“** für dev.7. Parallel meldete der reale S3: `index.html` 5.408 Byte
in **14 ms**, `style.css` 4.570 Byte in **16 ms**, `app.js` 14.582 Byte in
**1.779 ms**, jeweils `ESP_OK`. Das zuvor wiederholt nach rund elf Sekunden
abgebrochene JavaScript wurde nun vollständig an den Netzwerkstack übergeben;
die unabhängige Nutzerbestätigung belegt den vollständigen sichtbaren
Seitenaufbau. Diese Zeiten messen die Sendefunktion, keine Browser-Renderzeit.

Im anschließenden Schritt bestätigte der Nutzer **„Heim-WLAN erfolgreich
verbunden“**. Zugangsdaten wurden ausschließlich in der Gerätewebsite eingegeben.
Damit ist dieser konkrete Ladezeit-/Einrichtungsversuch erfolgreich; andere
Handys, Funkbedingungen, Dauerlauf und alle übrigen Features sind dadurch nicht
abgenommen. Die Paketbündelung bleibt für diesen Pilotstand deaktiviert.

### Spotify-Rückgabe: Fehler eingrenzen

Der folgende reale Anmeldeversuch endete im Desktophelfer mit „Diese Anmeldung
gehört nicht zum aktuellen Einrichtungsvorgang.“ Der damalige Code verwendete
diesen Text auch nach erfolgreichem Statevergleich für unerwartete Queryfelder,
ungültige beziehungsweise überlange Codes und einen geänderten USB-Port. Aus
diesem Text allein lässt sich die tatsächliche Ursache nicht bestimmen; der
alte Helfer hat bewusst keine Callbackdaten protokolliert. USB-Status bestätigte
dev.7, vorhandenes WLAN und noch keine Spotify-Verknüpfung.

Der Helfer unterscheidet jetzt diese Fehler und bietet die ausdrücklich
aktivierbare Option `--diagnostics`: feste Gründe, Parameteranzahl, Codelänge
und boolesche Formprüfungen, niemals Codes, States, URLs, Cookies oder Tokens.
Statevergleich, PKCE, Einmaligkeit und die bisherige Codegrenze bleiben
unverändert. 21 App-/Serial-Tests einschließlich Diagnose-Geheimnisfreiheit,
1024-/1025-Zeichen-Grenze und fehlschlagender Diagnoseausgabe sowie die 51
Frameworkprüfungen bestanden. Keine neue Firmware wurde dafür geschrieben.
Der Helfer wurde für einen frischen realen Versuch gestartet; dessen Diagnose,
der ursächliche Fix und die erfolgreiche Spotify-Verknüpfung sind noch offen.

### Korrektur der OAuth-Antwortprüfung

Die nächsten beiden realen Versuche meldeten jeweils `fields_invalid` bei drei
Parametern, 366 druckbaren ASCII-Codezeichen und passendem USB-Port. Ein zu
langer Code oder falscher State war damit nicht der Ablehnungsgrund. Die
Diagnose protokolliert bewusst keine Namen unbekannter Felder oder deren Werte;
die alte Diagnose unterschied außerdem ein zusätzliches `error` nicht von
einem unbekannten Feld. Eine erfolgreiche Verknüpfung ist noch nicht belegt.

Die bisherige pauschale Ablehnung unbekannter OAuth-Antwortfelder wurde nach
[RFC 6749 §4.1.2](https://www.rfc-editor.org/rfc/rfc6749.html#section-4.1.2)
korrigiert. Solche Zusatzfelder werden jetzt ignoriert; nur geprüfte Code-/State-
Werte gelangen über USB. Die Autorisierungs-URL vom S3 bleibt exakt an das
Spotify-Profil gebunden. Doppelte Felder, `code` gleichzeitig mit `error`, falscher
State, Wiederholung, TTL sowie Größen- und Portgrenzen bleiben unverändert
abgesichert. Zwei neue boolesche Diagnosewerte unterscheiden Code- und
Fehlerfeld, ohne deren Inhalt auszugeben. Es ist kein Firmwareupdate erforderlich.

Die lokalen Helper-/HTTP-Tests verwenden nun ausschließlich eigene kurzlebige
Loopbackports, damit ein echter laufender Einrichtungshelfer nicht mit einer
simulierten Sitzung verwechselt werden kann. Isolierte Browser leiten jede
Testanfrage dorthin oder zur synthetischen Spotify-Seite um; der echte Port 8766
wird dabei nie angesprochen. Nur im Testprozess werden Origin und Redirect auf
den Testport gesetzt; Browserheader und Cookies bleiben unverändert. 29 Helfer-
und HTTP-Tests sowie beide Browserabläufe in Chromium und WebKit bestanden:
zusätzliches Rückgabefeld, einmalige Code-/State-Übergabe, fehlender Strict-Cookie
beim tatsächlichen Cross-Site-Callback und erst anschließende Statusbestätigung.
Die 51 Frameworkprüfungen bestanden ebenfalls. Beide Browser werden künftig im
Browserprüfjob ausgeführt. Der korrigierte echte Helfer läuft für den angefragten
Retest; dessen erfolgreicher Geräteabschluss bleibt separat zu bestätigen.

### Callback angenommen, geräteseitige Verbindung noch fehlerhaft

Die beiden nächsten realen Callbacks wurden angenommen: jeweils drei Parameter,
366 druckbare ASCII-Codezeichen, passender State/USB-Anschluss, `has_error: false`.
Damit ist die zuvor abgelehnte zusätzliche Rückgabeangabe für diese Versuche
behoben. Der Knob meldete anschließend jedoch `linked: false`,
`connected: true`, `state: unlinked`, `error: network`; die neue Verknüpfung
wurde nicht bestätigt. Das grenzt den Fehler auf den Gerätetransport ein,
beweist aber noch keine Ursache bei DNS, TCP, TLS oder beim Antwortempfang.

Der Helfer ergänzt deshalb bei ausdrücklich aktivierter Diagnose die bereits
vorhandenen, vollständig gefilterten IDF-Fehlernummern und Änderungen des
tokenfreien Status. USB-Rohlogs und Zugangsdaten bleiben ausgeschlossen. Ein
Netzwerkfehler beim Abschluss erhält einen zutreffenden eigenen Hinweis.
40 isolierte Helfer-, HTTP- und Diagnoseprüfungen bestanden. dev.7 bleibt
unverändert installiert; der eigentliche Netzwerkfehler und eine erfolgreiche
Verknüpfung sind noch zu bestätigen beziehungsweise zu beheben.

### TLS-Speicherfehler und Korrektur dev.8

Der nächste reale Versuch lieferte `tls_setup: -32512` (`-0x7F00`), danach
`connection_open` und erneut `linked: false`, `error: network`, HTTP-Status 0.
Die gepinnte mbedTLS-Version bezeichnet diesen Code als
`MBEDTLS_ERR_SSL_ALLOC_FAILED`: Der TLS-Kontext konnte keinen Speicher
reservieren. Es ist damit kein belegter Spotify-Konto- oder Callbackfehler.

Beide S3-Profile verwenden ab dev.8 die von ESP-IDF vorgesehene
`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`-Strategie für den vorhandenen 8-MiB-PSRAM.
Der Companion bleibt unverändert bei internem Speicher. PSRAM-Initialisierung
bleibt zwingend; TLS-Taskstacks und DMA-Deskriptoren bleiben intern. Vollständige
Zertifikatsprüfung und bisherige TLS-Recordgrößen werden beibehalten. Ein
Startlog nennt ausschließlich Allokator und freie/größte PSRAM-Blöcke.
Der Schalter betrifft alle mbedTLS-Nutzer des S3, auch Wetter und Signaturprüfung.
Das Laborgerät hat weiterhin keinen physischen Flash-/PSRAM-Ausleseschutz;
keine eFuses werden dafür geändert.

51 Framework-/Vertragstests und alle drei ESP-IDF-Builds bestanden; die tatsächlichen
App-Binaries und Metadaten tragen dev.8. Standard-S3: **2.711.472 Byte**,
Labor-S3: **2.722.752 Byte**, Companion: **278.064 Byte**. Die wirksamen
S3-Konfigurationen bestätigen externen TLS-Speicher, unveränderte Zertifikats-
und Recordeinstellungen sowie weiterhin deaktiviertes AMPDU; der Companion
verwendet unverändert internen TLS-Speicher.

Nach erneuter Chipidentifikation und Hashprüfung des verifizierten Originalbackups
wurde ausschließlich die S3-App bei `0x10000` geschrieben und vollständig gegen
den Flash verglichen. Laborimage-SHA-256:
`35e24a9191a66fa1fa4a1969d9f587863a8aab72ceda4b1fb70b66ca7bb02020`.
Einstellungen und Schlüssel bleiben erhalten. Der reale Start bestätigte
`TLS allocator=PSRAM`, **8.335.568 Byte frei**, größter Block **8.257.536 Byte**
und Rückkehr aus `app_main`. USB-HELLO bestätigte dev.8 und aktives Laborprofil;
das Einrichtungsfenster ist nach dem Neustart erwartungsgemäß geschlossen.
Dies ist eine Startmessung, keine TLS-Spitzen- oder Dauerlaufmessung.

Die vollständige CI für dev.8-Commit `c197e16` bestand anschließend in beiden
Läufen: [36988465681](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify/actions/runs/36988465681)
und [36988462086](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify/actions/runs/36988462086).
Jeweils erfolgreich waren Vertrags-/Modellprüfungen, native Tests und
Firmwarebuilds sowie Website- und USB-Anmeldetests mit simuliertem Gerät/Spotify
in Chromium und WebKit. Dies bestätigt keine echte Spotify-Verknüpfung.

Beim Wechsel vom erfolgreichen Bootmonitor zum Einrichtungshelfer liefen
HELLO-Anfragen zunächst in einen Timeout. Ein anschließendes zwölfsekündiges
Lesefenster empfing keine Bytes; auch die ROM-Synchronisation mit esptool erhielt
keine Antwort. Nach vom Nutzer bestätigtem Abziehen und Wiedereinstecken in
derselben Orientierung wurde direkt der Helfer als einziger USB-Besitzer
gestartet. Die echte HELLO-Antwort bestätigte wieder dev.8,
`lab_enabled: true` und `setup_open: false`. Die Ursache der USB-Unterbrechung
ist nicht bewiesen; dafür wurde keine zusätzliche Firmwareänderung vorgenommen.

Zum Zeitpunkt dieses USB-Nachweises stand der frische OAuth-Retest noch aus.
TLS-Aufbau, Verknüpfung, Tokenpersistenz nach Neustart und hörbare Connect-Ausgabe
wurden als getrennte Gerätenachweise weitergeführt.

### Erste erfolgreiche Geräteanmeldung auf dev.8

Der anschließende reale Versuch am 02.10.2026 war erfolgreich; der Nutzer
bestätigte ausdrücklich „erfolgreich“. Die gefilterte Helferdiagnose verzeichnete
einen angenommenen Callback mit drei Parametern, 366 druckbaren ASCII-Codezeichen
und keinem Fehlerfeld. Auf `authorizing` folgten `state: ready`, `error: none`,
`linked: true`, `network_connected: true`, `http_status: 204` und
`attempt_confirmed: true`. Im neuen Diagnoseprotokoll trat kein freigegebenes
Transportfehlerereignis auf. Codes, Tokens und States wurden nicht aufgezeichnet.

Die Bestätigung gilt für genau diesen neuen Anmeldeversuch. Sie wird laut
Implementierung erst nach erfolgreichem Tokenaustausch und NVS-Speicherung
gesetzt. Damit sind der echte TLS-/Tokenabschluss und die Geräteverknüpfung
belegt; **Persistenz nach einem Neustart wurde noch nicht geprüft**. HTTP 204
ist der zuletzt beobachtete Providerstatus nach dem Abschluss, keine belegte
Antwort des Token-Endpunkts und kein Nachweis gestarteter oder hörbarer
Wiedergabe. Eine spätere HELLO-Abfrage
bestätigte weiterhin dev.8 bei inzwischen geschlossenem Setupfenster; dieses
HELLO enthält keinen erneuten Spotify-Status.

Sonos-Roam-/Move-Auswahl und hörbare Connect-Steuerung, Kontowechsel/Refresh,
mobile Kundeneinrichtung sowie kommerzielle Freigabe bleiben offen. Dieser
erfolgreiche Anmeldeversuch ersetzt weder die USB-Dauerprüfung noch die übrigen
Geräte- und Produktabnahmen.

### Ausgabeliste und getrennte Geräteabruf-Diagnose dev.9

Der Nutzer meldete nach der erfolgreichen Anmeldung, dass der Sonos nicht in
der Knob-Ausgabeliste erscheint. Ob er gleichzeitig in der Spotify-App sichtbar
ist und ob die Knob-Liste vollständig leer ist, wird separat abgefragt. Eine
fehlende API-Unterstützung für das konkrete Sonos-Modell ist damit nicht belegt.

Der Codeaudit fand einen reproduzierbaren Anzeigefehler: Die Liste wurde nach
Öffnen beziehungsweise Aktualisieren einmal nach zwei Sekunden aufgebaut.
Später eintreffende Spotify-Geräte aktualisierten zwar den Snapshot, aber nicht
die offene Liste. dev.9 zieht neue Snapshotrevisionen nach. Während Berührung
und bis 750 ms Eingaberuhe bleiben die sichtbaren Geräte-IDs unverändert;
Sitzungswechsel schließen den Picker. Bei kürzeren Listen wird die aktuelle
Seite begrenzt. Favoritenpaginierung bleibt unabhängig. 21 Assertions mit
ASan/UBSan prüfen die echten Pickerfunktionen gegen simulierte LVGL-/Provider-
Grenzen, einschließlich verspäteter Antwort und ausstehendem Touch-Release.

Zusätzlich protokolliert ausschließlich das Laborprofil fest begrenzte numerische
Metadaten pro Geräteabruf: HTTP-/Transportstatus, Formatprüfung und Anzahl
gelieferter beziehungsweise erfolgreich geparster Geräte. Der optionale Helferdiagnose-
Filter erkennt nur dieses exakte Format; Namen, IDs und Antwortinhalte bleiben
ausgeschlossen. Das ist erforderlich, weil eine spätere Playbackabfrage den
allgemeinen letzten HTTP-/Fehlerstatus ersetzen kann. Der Geräteabruf wird so
getrennt von einer Playbackantwort mit Status 204 nachweisbar.

285 Modell- und 791 Worker-Assertions bestanden, darunter leere/ungültige
Gerätelisten, HTTP 403 gefolgt von Playback 204, Transportfehler und Geheimnis-
freiheit. Alle 41 Helfer-/HTTP-/Diagnosetests bestanden. Die neue UI-Prüfung
ist in CI aufgenommen. Die Diagnosezahlen belegen keine Veröffentlichung des
Snapshots, falls während des Abrufs die Kontositzung gewechselt wurde.

Alle drei ESP-IDF-Profile und 51 Frameworkprüfungen bestanden; tatsächliche
Appversionen und CMake-Metadaten wurden mit dev.9 abgeglichen. Standard-S3:
**2.711.552 Byte**, Labor-S3: **2.723.296 Byte**, Companion: **278.064 Byte**.
Laborimage-SHA-256:
`2a24414d8a6aa8f3ef15709779d619f1d185293c0652caa6d8928ff0f338debc`.
Vor einer Installation war der S3 nicht mehr als USB-Gerät erreichbar; der
Nutzer wurde um Wiederanschließen in der zuletzt funktionierenden Orientierung
gebeten. Zu diesem Zwischenstand war dev.9 noch nicht geflasht. Der gefundene
UI-Fehler war noch nicht als alleinige Ursache des gemeldeten fehlenden Sonos
belegt.

### dev.9 installiert: Geräteabruf nach Neustart ohne Neuanmeldung

Beide vollständigen CI-Läufe für Commit `d5527b6` bestanden:
[PR-Lauf 36991665093](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify/actions/runs/36991665093)
und [Push-Lauf 36991661279](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify/actions/runs/36991661279).
Vertrags-/Modellprüfungen, Browserprüfungen, native Tests einschließlich
Pickerregression und Firmwarebuilds waren jeweils erfolgreich.

Anschließend wurde das oben identifizierte dev.9-Laborimage ausschließlich als
S3-App bei `0x10000` geschrieben und mit explizitem Flashvergleich erfolgreich
verifiziert. Auf den Hard-Reset folgte direkt der Einrichtungshelfer als einziger
USB-Besitzer, ohne zwischengeschalteten Monitor. Die echte HELLO-Antwort meldete
`version: 0.1.0-dev.9`, `lab_enabled: true` und `setup_open: false`.

Die gefilterte Diagnose belegte danach zweimal den Geräteabruf mit
`http_status: 200`, `transport: 0`, `valid: 1`, `listed: 2` und `count: 2`.
Seit dem Flash gab es keinen OAuth-Callback und keine erneute Anmeldung. Damit
ist erstmals nachgewiesen, dass die im NVS gespeicherte Spotify-Verknüpfung
dieses Appupdate und den Neustart überstanden hat und für einen authentifizierten
Geräteabruf nutzbar blieb. Das ist keine vollständige Refresh-, Kontowechsel-
oder Stromausfallabnahme. Die Rohbelege bleiben im privaten Recovery-Verzeichnis.

Zwei erfolgreich geparste Einträge beweisen weder, dass einer davon der Roam
ist, noch ihre Anzeige oder Auswahl auf dem Knob. Der Nutzer bestätigte separat
hörbare Roam-Wiedergabe über die Spotify-App. Auswahl und hörbare Steuerung
durch den Knob werden noch geprüft; auch die alleinige Ursache des zuvor
fehlenden Sonos ist damit noch nicht abschließend belegt.

### Leerer Geräteeintrag / dev.10

Der Nutzer sieht auf dev.9 einen iPhone-Eintrag und eine weitere Schaltfläche
ohne lesbaren Namen. Das beweist weiterhin nicht, dass der zweite Eintrag der
Roam ist. Der genaue Roam-Name in Spotify und die Anzeige nach Auswahl der
leeren Schaltfläche wurden zur Eingrenzung abgefragt; eine bloße Auswahl
startet beziehungsweise verschiebt keine Wiedergabe.

Ein reproduzierbarer Fehler im bisherigen Modell ist die unveränderte Übernahme
leerer beziehungsweise ausschließlich unsichtbarer Gerätenamen. dev.10 ergänzt
dafür zentral den neutralen Ersatztext `Ausgabe ohne Namen`, damit Geräteliste,
Wiedergabezustand und die daraus gespeiste Website keine leere Gerätebezeichnung
erhalten. Geräte-ID und Steuerrechte bleiben unverändert; weder Hersteller noch
Modell werden aus dem fehlenden Namen geraten. Sichtbare gültige UTF-8-Namen
bleiben erhalten. Fehlende, nicht-stringförmige und ungültig kodierte Namensfelder
bleiben Antwortfehler. Das konkrete Verhalten der betroffenen Zeile am Gerät
und die Identität des zweiten Eintrags sind weiterhin zu prüfen.


Vor dem Flash bestanden 419 Modell- und 791 Worker-Assertions mit ASan/UBSan,
21 Picker-Assertions und 51 Frameworkprüfungen. Der Modelltest deckt insbesondere
zwei Einträge (`iPhone` plus namenlose Ausgabe), Leerraum/Formatmarker,
sichtbare Unicode-Namen, ungültiges UTF-8 sowie unveränderte IDs und Rechte ab.
Ein isolierter Renderer mit den echten LVGL-9.2.2-Helfern und DE-Schriften zeigt
auch die zweite Zeile für `Sonos Roam`, `Küche`, einen gekürzten langen ASCII-Namen
und den Ersatztext korrekt. Das prüft Layout und Schrift im nativen Vollbild;
es ist kein Nachweis des physischen Display-Teilrefreshs oder der tatsächlich
von Spotify gelieferten Namen. Ein vorläufiger Host-Teilrefresh zeigte bei
späteren Frames fehlende andere statische Elemente und bleibt als separate,
nicht auf das Gerät übertragene Beobachtung offen.

Alle drei ESP-IDF-Builds bestanden; CMake-Metadaten und binäre Appdeskriptoren
melden dev.10. Standard-S3: **2.711.552 Byte**, Labor-S3: **2.723.600 Byte**,
Companion: **278.064 Byte**. Laborimage-SHA-256:
`86d11b72bde5d41b0d7d9da9f1329db72b99fec4609b2aa40503907faee160e3`.


dev.10 wurde anschließend ausschließlich als S3-App bei `0x10000` installiert.
Der erneute Chipnachweis bestätigte ESP32-S3 rev0.2 mit 16 MiB Flash und 8 MiB
PSRAM; der vollständige Originalbackup-Hash wurde vor dem Schreiben erneut
geprüft. Der explizite Flashvergleich meldete `verify OK (digest matched)`.
Nach dem Hard-Reset startete unmittelbar der USB-Helfer; HELLO bestätigte
`version: 0.1.0-dev.10`, `lab_enabled: true`, `setup_open: false`. Companion,
Bootloader, Partitionstabelle, Einstellungen und Schlüssel wurden nicht
beschrieben. Der konkrete API-Namensinhalt und die Roam-Steuerung sind noch
nicht bestätigt; die nachfolgende Nutzerrückmeldung ergänzt den Displaynachweis.


Der Nutzer bestätigte danach den Ersatztext auf dem physischen Display
(als „Ausgaben ohne Namen“ wiedergegeben). Zu diesem Zeitpunkt lief nach seiner
Angabe keine Spotify-Connect-Musik. Nach der Aufforderung, Spotify auf dem Roam
zu starten und die Liste zu aktualisieren, meldete er erneut `iPhone` und
`Ausgabe ohne Namen`; der tatsächliche Roam-Start und dessen Spotify-Name wurden
in dieser Antwort nicht ausdrücklich bestätigt. Der Anzeige-Fallback ist damit
physisch beobachtet. Eine Zuordnung des namenlosen Eintrags zum Roam oder ein
Nachweis der Knob-Steuerung folgt daraus weiterhin nicht.

Die vollständigen dev.10-CI-Läufe für Firmwarecommit `c962160` und Nachweiscommit
`e0c67a0` bestanden jeweils als Push- und PR-Lauf. Aktueller PR-Nachweis:
[Lauf 36994971139](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify/actions/runs/36994971139),
[Push-Lauf 36994966770](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify/actions/runs/36994966770).

Bei der anschließenden USB-Diagnose meldete der Helper `usb_unavailable`.
Die Portinventur zeigte den S3 weiterhin; `lsof` wies einen anderen
Python-Prozess aus einem separaten VS-Code-Codex-Aufruf als aktuellen
Portbesitzer aus. Dieser Prozess wurde nicht beendet, der Port nicht parallel
geöffnet und keine weitere Firmware geschrieben. Dieser Hostkonflikt ist kein
Nachweis eines erneuten Geräte- oder Spotify-Fehlers. Der Test über die sichtbare
Musikseite kann unabhängig fortgesetzt werden.


### Wiedergabediagnose / dev.11

Nach Auswahl des namenlosen Eintrags meldete der Nutzer die Standardüberschrift
`Deine Musik`. Diese erscheint sowohl bei unbekanntem Playback als auch bei
bekanntem Playback ohne Titel; sie unterscheidet den konkreten Fehler nicht.
Die erneute USB-Abfrage gelang inzwischen wieder und bestätigte dev.10 sowie
zwei weitere Geräteabrufe mit HTTP 200, gültigem Format und zwei Einträgen.
Die direkte Spotify-Connect-Wiedergabe auf dem Roam wird für den aktuellen Test
noch ausdrücklich gegenüber AirPlay/Bluetooth beziehungsweise Stille abgefragt.

Die bisherige Diagnose erfasst nur den Geräteendpunkt. dev.11 ergänzt deshalb
eine getrennte, feste numerische Wiedergabezeile: HTTP-/Transportstatus,
Formatprüfung, bekannter/spielender Zustand und Übereinstimmung des aktiven
Geräts mit Liste und Auswahl. Antwortinhalte, Gerätenamen, Geräte-IDs und
Anmeldedaten werden nicht protokolliert. Die Diagnose ändert weder Geräteauswahl
noch Abspielbefehle; eine API-Rückmeldung beweist keine hörbare Wiedergabe.
Vor der Installation bestanden 419 Modell- und 1104 Worker-Assertions mit
ASan/UBSan sowie 46 Helfer- und 51 Frameworktests. Die Regressionen trennen
HTTP 204, gültiges/ungültiges HTTP 200, HTTP 403 und Transportfehler; sie decken
veralteten beziehungsweise teilweise geparsten Arbeitszustand, andere Auswahl,
fehlenden Listeneintrag und Kontositzungswechsel ab. Listen-/Auswahlvergleich
erfolgt unter Mutex nur für die passende Sitzung gegen den veröffentlichten
Zustand. Die Parserflags beschreiben die Antwort selbst; diese Korrelationen
beweisen weder die spätere UI-Anzeige noch erfolgreiche Steuerung.

Alle drei ESP-IDF-Profile bauen; CMake und Appdeskriptoren bestätigen dev.11.
Standard-S3: **2.711.552 Byte**, Labor-S3: **2.723.984 Byte**, Companion:
**278.064 Byte**. Laborimage-SHA-256:
`8cfbb99eb40b56e85b01b65d04f93a00dad6bfab4589113e2bf0babdd2103369`.
dev.11 wurde danach als reines S3-Appupdate bei `0x10000` installiert. Erneute
Chipidentifikation und Originalbackup-Hashprüfung bestanden; der explizite
Flashvergleich meldete `verify OK (digest matched)`. Der direkt danach gestartete
USB-Helfer erhielt dreimal HELLO mit dev.11, aktivem Laborprofil und geschlossenem
Setupfenster. Einstellungen, Schlüssel und Companion wurden nicht geschrieben.

Der Nutzer bestätigte nun ausdrücklich hörbare Roam-Wiedergabe nach direkter
Auswahl in Spotify. Die erste getrennte Diagnose nach dem Neustart ergab einen
gültigen Geräteabruf mit HTTP 200 und zwei Einträgen, aber für Playback
`http_status: 0`, `transport: -1`, `valid: 0`. Damit liegt für diesen Abruf kein
bekannter HTTP-Erfolgsstatus und kein gültiger Wiedergabezustand vor. Der genaue
Transportfehler und seine Wiederholbarkeit werden weiter eingegrenzt; weder
fehlende Modellunterstützung noch eine falsche Kontoverknüpfung sind damit bewiesen.


Die Folgemessung während bestätigter hörbarer Roam-Wiedergabe zeigte unter den
erfassten Playbackabrufen mehrfach `http_status: 200`, `transport: 0`,
`valid: 0`; zusätzlich traten einzelne Status-0-/Transport-−1-Fehler auf. Damit
verwirft der Knob auch erfolgreich übertragene HTTP-200-Antworten. Der JSON-/
Feldvalidierungspfad wird nun getrennt eingegrenzt. HTTP 0 bedeutet mit dem
verwendeten IDF nicht zwingend, dass keine Statuszeile empfangen wurde: Der
öffentliche Status wird erst nach vollständigen Headern gesetzt.


### Validierungsgrund und adresslose Playbackgeräte / dev.12

Die Playbackvalidierung erhält feste numerische Fehlergründe, um die tatsächlich
abgelehnte Feldgruppe ohne Antwortinhalte, Namen oder IDs zu erkennen. Außerdem
wird die von Spotify dokumentierte nullable Playback-Geräte-ID unterstützt:
Metadaten eines solchen Geräts dürfen angezeigt werden, verleihen aber keine
adressierbare Steuerberechtigung. Die Ausgabeliste lässt Geräte ohne ID weiterhin
aus. Diese getrennt reproduzierbare Vertragskorrektur ist noch nicht als Ursache
des realen Roam-Problems nachgewiesen. Die vorhandene Transport-/Playbackdiagnose
bleibt erhalten. 623 Modell- und 1115 Worker-Assertions mit ASan/UBSan, 48 Helfertests und
51 Frameworktests bestanden. Geprüft sind Anzeige mit explizit nullförmiger ID,
unveränderte Ablehnung fehlender/leerer/ungültiger IDs und die Trennung von
Wiedergabemetadaten gegenüber adressierbaren Steuerrechten. Die Grundcodes 1–16
benennen Feldgruppen; 31 bedeutet abgelehntes JSON vor der Feldvalidierung.

Alle drei ESP-IDF-Profile bauen; binäre Appdeskriptoren und CMake melden dev.12.
Standard-S3: **2.711.552 Byte**, Labor-S3: **2.724.224 Byte**, Companion:
**278.064 Byte**. Laborimage-SHA-256:
`3f3e9c2e8d996a6e5ea0bed88c0f68a8acd3cf1eff8788a7689091496ae757e0`.
Das Appimage wurde nach erneutem Backup-Hash- und Chipabgleich bei `0x10000`
geschrieben; esptool bestätigte `Hash of data verified`. Der anschließend
aufgerufene eigenständige `verify_flash`-Schritt konnte den bisherigen S3-Port
nicht mehr öffnen (`No such file or directory`). Die Inventur enthielt zu diesem
Zeitpunkt keinen S3 mit `303a:1001`. Deshalb sind unabhängiger Flashvergleich,
Neustart/USB-HELLO und der tatsächliche Validierungsgrund noch offen. Der Nutzer
wurde gebeten, den Knob in derselben Ausrichtung wieder mit dem Mac zu verbinden.
Der fremde sichtbare USB-Port wurde nicht geöffnet oder beschrieben.

Alle vollständigen dev.11-CI-Läufe für `fcc5f52` und `ea68932` bestanden;
[PR-Lauf 37001704091](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify/actions/runs/37001704091).
Die dev.12-CI für `fb08362` läuft zu diesem Zwischenstand noch.


### dev.12 nach Wiederanschluss verifiziert

Nach der Nutzerrückmeldung „wieder angeschlossen“ erschien der S3 erneut als
`303a:1001`. Das unveränderte dev.12-Image wurde nur gelesen/verglichen, nicht
erneut geschrieben. Der vollständige Vergleich bei `0x10000` bestätigte
`verify OK (digest matched)`; nach dem Hard-Reset erhielt der direkt gestartete
Helfer viermal HELLO mit `version: 0.1.0-dev.12`, `lab_enabled: true` und
`setup_open: false`. Der zuvor ausstehende Installationsnachweis ist damit
abgeschlossen.

Die anschließenden Abrufe lieferten dreimal HTTP 204 ohne Transportfehler und
mit gültiger Verarbeitung (`known: 0`, `playing: 0`) sowie eine gültige
Geräteliste mit einem Eintrag. Dieser aktuelle Zustand reproduziert die früheren
HTTP-200-Validierungsfehler noch nicht. Der Nutzer wurde um erneuten hörbaren
Roam-Start direkt in Spotify und Aktualisierung am Knob gebeten. Die genaue
Ursache der früher verworfenen Roam-Antwort ist weiterhin unbestätigt.

Alle vier vollständigen dev.12-CI-Läufe für `fb08362` und `1d99be5` bestanden;
[PR-Lauf 37002673144](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify/actions/runs/37002673144),
[Push-Lauf 37002667851](https://github.com/passion-wave/Passion-Wave-rotaryknob-spotify/actions/runs/37002667851).


Bei einer weiteren Beobachtung wurden erneut ausschließlich gültige HTTP-204-
Playbackantworten und ein gültiger Listeneintrag erfasst. Währenddessen brach
der USB-Zugriff mit `usb_unavailable`, anschließend `no_port` ab. Die neue
Inventur zeigte nur ein anderes USB-Seriell-Gerät (`1a86:55d3`), keinen S3 mit
`303a:1001`. Es wurde weder dieser andere Port geöffnet noch erneut geflasht.
Ob der Nutzer umgesteckt hat oder die Verbindung unverändert blieb, wird nun
separat geklärt. Der bereits erfolgreiche Flashvergleich und dev.12-HELLO-
Nachweis bleiben davon getrennt gültig.


### 5. Oktober: gültiges Playback und Diagnose dev.13

Beim Wiederanschluss war zunächst der bekannte USB-UART-Anschluss des
Begleitprozessors sichtbar. Nach der angefragten 180°-Drehung erschien wieder der
S3 (`303a:1001`); HELLO bestätigte dev.12. Der Begleitprozessor wurde nicht
angesprochen. Die temporäre Arbeitskopie wurde aus dem sauberen Repositorystand
`fbab72d` wiederhergestellt; vorhandene Firmware und Einstellungen blieben dabei
unverändert.

Nach zunächst gültigen HTTP-204-Antworten verarbeitete dev.12 erstmals beobachtet
HTTP 200 mit `valid: 1`, `known: 1` und anschließend `playing: 1`. Die aktive
Ausgabe passte jedoch nicht zur veröffentlichten Liste (`listed: 0`); eine
Auswahl war nicht bestätigt (`selected: 0`). Die Geräteliste enthielt inzwischen
zwei gültige Einträge. Der Nutzer bestätigte danach „Roam läuft“. Das belegt
hörbare App-Wiedergabe und verarbeitete Playbackdaten, noch keine Knob-Steuerung.
Die physische Titelanzeige ist zusätzlich abgefragt. Wiederholte Status-0-/
Transport-−1-Fehler bleiben ein zweiter offener Fehlerpfad.

dev.13 ergänzt zwei feste, ausschließlich numerische Laborereignisse: eine
Aussage, ob die aktive Ausgabe eine nichtleere ID hat und ob Spotify sie als
steuerungsgesperrt meldet, sowie bei Playback-Transportfehlern die Phase
(0 Vorbereitung, 1 Öffnen, 2 Senden, 3 Header, 4 Antwortkörper), den ursprünglichen
negativen Rückgabecode, ein begrenztes Socket-errno und die Laufzeit in ms.
Die Metadaten werden vor dem Schließen des HTTP-Clients gesichert; unbekannte
errno-Werte werden als 0 ausgegeben, Detailcodes unter −65535 und Laufzeiten über
600000 ms begrenzt. Es werden weder Namen/IDs noch Header, Antwortinhalte oder
Anmeldedaten ausgegeben. Timeout, Wiederholungs- und Steuerverhalten bleiben
unverändert.

Prüfung: 623 Modell- und 1324 Worker-Assertions unter ASan/UBSan, 49
Helper-Tests einschließlich 16 Diagnosefilter-Tests sowie 51 Framework-Tests
bestanden. Standard-S3, Begleitprozessor und S3-Laborprofil wurden mit ESP-IDF
5.4.3 gebaut; alle drei Binärversionen sind `0.1.0-dev.13`. Beim parallelen
Erstkonfigurieren fehlte vorübergehend die LVGL-Komponentenprüfsumme; der
anschließende sequenzielle Laborbuild bestand. Native USB-Konsole, PSRAM-TLS
und deaktiviertes WLAN-AMPDU bleiben bestätigt.

Labor-App SHA-256:
`c8074299ca2d6ce4e7bf7e34f62d862a88109c98b50adec071bf658f17d39d3d`.
Installation am 5. Oktober erfolgt: Originalbackup erneut per SHA-256 geprüft,
S3 mit 16 MiB Flash und 8 MiB PSRAM identifiziert, ausschließlich die 2.724.816
Bytes große App an `0x10000` geschrieben. Schreibprüfung und separater vollständiger
Flashvergleich bestanden. Zehn USB-HELLO-Abfragen bestätigten anschließend
`0.1.0-dev.13`, aktiviertes Laborprofil und geschlossenes Setupfenster. NVS,
Schlüssel, Partitionstabelle und Begleitprozessor wurden nicht verändert.
Die erste kurze Beobachtung lieferte nur generische Verbindungsfehler, noch keine
neuen Playbackereignisse; diese Fehler lassen sich keinem Request sicher
zuordnen. Erneutes Aktualisieren der Ausgabeliste und sichtbarer Musikstatus sind
beim Nutzer abgefragt. Adressierbarkeit, konkrete Transportursache und hörbare
Knob-Steuerung bleiben offen. Private Recovery-Belege liegen außerhalb von Git.


### dev.13 Livebefund und dev.14 Parserdiagnose

Der Nutzer bestätigt weiterhin „Deine Musik“. Nach dem Neustart wurden mehrfach
drei Ausgaben erfolgreich geparst. Playback scheiterte dagegen reproduzierbar in
Phase 3 (HTTP-Header), ursprünglicher Rückgabecode −1, Socket-errno 0 und
Laufzeiten von 455 bis 916 ms. Das ist noch keine Aussage über eine Spotify-
Steuerungssperre und kein JSON-Fehler. Der gepinnte IDF-Headerabruf meldet sowohl
Streamabbrüche als auch Folgen eines Parserfehlers auf diesem Weg unspezifisch.

dev.14 ergänzt daher ausschließlich im Laborbuild eine lesende Beobachtung des
HTTP-Parsers per Linker-Wrapping. Der Originalparser wird unverändert einmal
aufgerufen; Rückgabewert, Eingabe und Parserzustand werden nicht verändert. Nur
der erstmalige Übergang einer HTTP-Antwort in einen Fehlerzustand erzeugt einen
begrenzten numerischen Fehlercode und gegebenenfalls HTTP-Status. Keine Header-
oder Antwortinhalte werden gespeichert. Das Ereignis ist transportübergreifend
und muss zeitlich korreliert werden.

Prüfung: sieben Fälle mit dem echten IDF-HTTP-Parser unter ASan/UBSan,
byteweise fragmentiert und mit unverändertem Rückgabewert sowie Parserzustand;
623 Modell- und 1324 Worker-Assertions, 50 Helper- und 51 Frameworktests bestanden.
Alle drei Profile tragen `0.1.0-dev.14`. Symbolprüfung bestätigt die zusätzliche
Beobachtung nur im Labor-ELF, nicht im Standard-S3. App-SHA-256:
`5d8e2ee4839ac15388821d2cbc6e39b61a6cb29a655f6e107d172f8b161131ea`.
Installation ist unten belegt. Beide vollständigen CI-Läufe für dev.13-Quellcommit
`6ec69d2` bestanden.


dev.14-Gerätebeleg am 5. Oktober: Identifikation und erneut geprüfter
Originalbackup-Hash vor App-only-Flash, 2.724.800 Bytes an `0x10000`, Schreibprüfung
und separater vollständiger Flashvergleich erfolgreich. USB-HELLO bestätigt
`0.1.0-dev.14`. Einstellungen und Begleitprozessor unverändert.

Während des vom Nutzer bestätigten Roam-Tests lieferten vier gültige HTTP-200-
Playbackantworten `playing=1`, jeweils `addressable=0, restricted=1`; die aktive
Ausgabe hat also keine nichtleere Spotify-ID und ist ausdrücklich für Web-API-
Steuerung gesperrt. Die Liste hatte drei gültige Einträge, keiner entsprach der
aktiven Ausgabe. Diese Messung korreliert mit dem laufenden Roam-Test, beweist
aber nicht die Identität eines namenlosen Listeneintrags oder Eigenschaften
aller Roam-/Move-Geräte. Laut [Spotify-Referenz](https://developer.spotify.com/documentation/web-api/reference/get-information-about-the-users-current-playback)
werden bei `is_restricted=true` keine Web-API-Befehle angenommen. Es wurden keine
Umgehungs-/globalen Wiedergabebefehle gesendet.

Daneben traten zwei Headerabrufabbrüche auf (436/504 ms, Detail −1, errno 0),
ohne beobachtetes HTTP-Parserfehlerereignis. Das bestätigt weiterhin die frühe
Abbruchphase, aber keinen konkreten Netzwerk-/Serververursacher. Erfolgreiche
Antworten danach belegen Erholung ohne erneute Anmeldung; keine Behebung der
Abbrüche. Die sichtbare Titelanzeige unter dev.14 ist separat abgefragt. Hörbare
Knob-Steuerung bleibt unbestätigt. Sonos-LAN-Prüfung siehe Dokument 12; kein
stiller Anbieterwechsel und keine Produktfreigabe aus diesem Laborbefund.


Physische Rückmeldung nach dev.14: Nutzer bestätigt „Ja, Songtitel sichtbar“.
Damit ist die Titelanzeige für diesen laufenden Roam-Test separat vom API-
Messwert belegt. Als nächste Abnahme wurde Play/Pause mit dem in Spotify aktiv
gewählten iPhone und derselben Knob-Auswahl angefragt; Ergebnis noch offen.


Abnahme des Kontrolltests am 5. Oktober: Auf die Aufforderung, Spotify auf
„Dieses iPhone“ leise abzuspielen, dieselbe Ausgabe am Knob zu wählen und dort
Pause sowie anschließend Play zu testen, bestätigt der Nutzer „funktiooniert“.
Damit sind hörbare Pause und Wiederaufnahme durch den Knob für diese iPhone-
Sitzung unter dev.14 physisch bestätigt. Dies ist eine Nutzerbeobachtung, keine
zusätzliche protokollierte HTTP-Abnahme. Lautstärke, Next/Previous, Playliststart,
Dauerbetrieb und weitere Ausgaben sind dadurch nicht abgenommen. Die gemessene
Roam-Restriktion und sporadischen Headerabbrüche bleiben separat offen.


Anschließender Titelwechseltest am 5. Oktober: Auf die Aufforderung, mit dem
iPhone als Ausgabe am Knob „Nächster Titel“ und „Vorheriger Titel“ zu testen,
bestätigt der Nutzer „funktioniert“. Beide Bedienbefehle sind damit für diese
Sitzung unter dev.14 durch Nutzerbeobachtung bestätigt; „Vorheriger Titel“ darf
entsprechend der vorherigen Testanleitung zunächst den aktuellen Titel neu
starten. Zusammen mit dem vorherigen Kontrolltest sind Pause, Play und beide
Titelwechselbefehle am iPhone abgenommen. Lautstärke, Playliststart, Dauerbetrieb,
weitere Ausgaben sowie die Roam-Steuerung bleiben offen.


### dev.15: Bibliotheksauswahl, Website und autorisierter Heimnetz-Pilot

Ausgangsbefund am realen S3 unter dev.14, Adresse vom Nutzer bestätigt:
Drei sequenzielle LAN-Messreihen zeigten beim 14.582-Byte-JavaScript einen
20-Sekunden-Timeout sowie 9,931/7,066 Sekunden. HTML benötigte 0,178 bis 19,971
Sekunden, CSS 0,043 bis 2,048 Sekunden. Session-/Statusantworten lagen bei
0,032–0,076 Sekunden. Die gesperrten Felder im Heimnetz waren zusätzlich eine
bewusste Zugriffsbeschränkung der bisherigen Firmware, kein Touchdefekt.

Änderungen: USB-orientierter WLAN-Betrieb ohne Modemschlaf (`WIFI_PS_NONE`),
weiterhin AMPDU RX/TX aus; Hintergrund-JSON-Lesezugriffe serialisiert, direkte
Nutzer-Schreibaktionen warten nicht hinter einem ausstehenden Statusabruf.
Spotify-/OTA-Polling auf zehn Sekunden reduziert und auf sichtbare Bereiche
beschränkt; AP-Sitzung vor Ablauf erneuert, physisch geöffnetes AP-Fenster bei
autorisierter Nutzung als Inaktivitätsfenster verlängert. Der Heimnetz-Pilot
bleibt dagegen fest auf zehn Minuten begrenzt. Unveränderte Katalogdaten bauen
keine Eingabefelder neu auf. 16-Pixel-Eingabeschrift verhindert Safari-Fokuszoom,
native Dropdowns und Schaltflächen umbrechen auf kleinen Displays.

Spotify-Bibliothek: `POST /api/v1/spotify/library` reiht `{kind, offset, session}`
ein; `GET /api/v1/spotify/library?kind=playlist|show` liest einen RAM-Snapshot.
Der Spotify-Worker lädt feste `/v1/me/playlists`-/`/v1/me/shows`-Pfade mit zehn
Elementen; fremde `next`-URLs werden nie verfolgt. Pagination, URI-Typ, IDs,
UTF-8 und Größen werden validiert. Browser-Auswahl bis 1.000 Einträge pro Typ,
lokaler Katalog weiterhin maximal 64 Favoriten. Neue Anmeldung fordert zusätzlich
`playlist-read-private` und `user-library-read`; 403 der Bibliothek sperrt nicht
die bestehende Wiedergabesteuerung. OAuth-Profil, Helper und Golden Fixtures
sind abgestimmt. Quellen: [Playlists](https://developer.spotify.com/documentation/web-api/reference/get-a-list-of-current-users-playlists),
[gespeicherte Podcasts](https://developer.spotify.com/documentation/web-api/reference/get-users-saved-shows).

Der Nutzer autorisierte explizit die unverschlüsselte Pilotverwaltung im Heimnetz.
`CONFIG_PW_LAN_HTTP_LAB` aktiviert die physische Taste „Web freigeben“, Einmalcode,
feste Ablaufzeit und getrennte LAN-Cookies/CSRF. Der Zugriff verlangt lokale
STA-Adresse, gleiches IPv4-Subnetz, gebundene Client-IP, korrekten Host und bei
Änderungen passenden Origin/CSRF. Fünf Fehlversuche schließen die Freigabe. Keine
Freigabe durch Remote-Aufruf. Die Website bietet die Code-Eingabe und kennzeichnet
den unverschlüsselten Transport. Produktprofil ohne diese Option; G2 bleibt offen.

Prüfung bisher: 623 Modell-/1470 Worker-Assertions, sieben reale HTTP-Parserfälle,
50 Helpertests, elf USB-Vertragstests, 51 Frameworktests; 25 HTTP-Response-Szenarien
und 20 exakte gzip-Roundtrips mit realem IDF-Sendecode. Native Pilot-Gate-Tests
prüfen Ablauf, Wiederholung, Fehlversuche und Widerruf; extrahierte reale HTTP-
Autorisierung prüft Host, Origin, CSRF, Cookie, Peer, Subnetz und lokalen Socket.
Chromium/WebKit bestehen neue Bibliotheks-/Link-/Pagination-/403-/Fokus-/Pilot-
Pairing-Prüfungen bei 360, 390 und 1280 Pixeln; bestehende Browserregression sowie
komprimierte WebKit-HTTP-Auslieferung bestehen. Diese Browserantworten sind
simuliert und kein physischer Performancebeleg. Alle drei Firmwareprofile bauen.

Die reale Nachmessung nach Abschalten des Modemschlafs allein zeigte weiterhin
0,788–4,886 Sekunden für JavaScript. TCP-Metadaten zeigten sofortige Bestätigungen
vom Rechner, trotzdem etwa einsekündige Pausen nach dem ersten Sendefenster.
Der finale HTTP-Sendeadapter begrenzt Schreibportionen auf 1440 Byte und gibt
nach einem vollen Segment für zwei Millisekunden Rechenzeit frei. IDFs reale
`send_all`-Schleife übernimmt partielle Writes; Content-Length und gzip bleiben
exakt. Der Hosttest führt auch diesen Adapter aus, begrenzt die simulierten
Socket-Writes und prüft weiterhin Abbrüche sowie vollständige HTTP-Antworten.
Dies ist ein am Pilot vermessener Workaround; die genaue Ursache im WLAN-/TCP-
Zusammenspiel ist damit noch nicht abschließend bewiesen.

Finaler lokaler Diagnosekandidat: 2.733.344 Bytes, SHA-256
`ff557e105ceeaddae01f64fdac5c42179f52c3afdf84f28c5cee0fbc1cc7fcc4`,
gebaut aus Quellbaum `ba0351d6666b95df055312f260b6692597d9c6d6`
auf Basis `3d4216c`. Nach erneuter S3-/16-MiB-Identifikation ausschließlich
Appbereich `0x10000` geschrieben; separater vollständiger Flashvergleich und
USB-HELLO mit `0.1.0-dev.15` bestanden. NVS/Schlüssel, Bootloader und Partitionen
wurden nicht überschrieben; der Begleitchip trägt weiterhin die Originalfirmware.
Originalbackup vor dem ersten dev.15-Flash erneut per SHA-256 geprüft. Die
installierte Binärdatei, Quellpatches und Messprotokolle bleiben im privaten
Recoveryverzeichnis, nicht im Repository.

Fünf sequenzielle LAN-Messrunden am laufenden Gerät ergaben:

| Abruf | Übertragene gzip-/JSON-Bytes | Vollständiger Abruf |
| --- | ---: | ---: |
| HTML | 5.982 | 43–72 ms |
| CSS | 4.928 | 39–55 ms |
| JavaScript | 16.771 | 58–74 ms |
| Sitzung | 68 | 28–34 ms |
| Status | 468 | 28–39 ms |

Alle 25 Abrufe HTTP 200, kein Timeout; die 15 Assetantworten nach gzip-
Dekompression bytegenau gegen die Quellen geprüft. Drei vollständige mobile
Chromium-Läufe direkt gegen das Gerät mit jeweils neuem Browserkontext waren
nach 0,875 / 0,256 / 0,268 Sekunden bereit. Kein horizontaler Überlauf,
JavaScriptfehler oder fehlgeschlagener Request; Freigabeformular bedienbar,
private Felder vor Paarung korrekt gesperrt. Screenshot visuell geprüft.
Der isolierte WebKit-Prozess erhielt am Mac keinen direkten LAN-Zugriff;
WebKit-Layout/Bedienung ist bisher gegen simulierte Antworten geprüft.
Der Nutzer bestätigte anschließend auf seinem iPhone in Safari im Heim-WLAN
nach „Gerät → Web freigeben“ und Code-Eingabe: „Ja, schnell und Felder
bedienbar“. Damit sind der schnelle Seitenaufbau und die Pilotfreigabe in dieser
Sitzung auch physisch beobachtet. Anzeige der echten Bibliothek nach Zustimmung
zu den zusätzlichen Spotify-Leserechten bleibt separat zu bestätigen. Keine
Aussage über jede WLAN-Umgebung oder Dauerbetrieb.

Die erste GitHub-Browserprüfung des Implementierungscommits scheiterte am
macOS-spezifischen Screenshotpfad des neuen Testskripts. Der Standardpfad nutzt
jetzt das Betriebssystem-Tempverzeichnis; CI setzt ausdrücklich `RUNNER_TEMP`.
Alle sechs Chromium-/WebKit-Läufe bestehen lokal erneut. Diese Korrektur betrifft
nur Tests/Workflow/Dokumentation, nicht den installierten Firmwarekandidaten.
Beim echten Bibliotheksabruf bestätigte der Nutzer den erwarteten Hinweis auf
fehlende Spotify-Leserechte. Erneute Anmeldung mit den vier Scopes ist angeleitet;
die echte Listenanzeige ist dadurch noch nicht abgenommen.

### dev.16: begrenztes Weiterlesen beim Spotify-Tokenabruf

Nach dev.15 nimmt der Helfer mehrere echte OAuth-Rückmeldungen korrekt an
(`accepted`), aber der S3 bestätigt keine neue Verknüpfung. Dabei erscheinen
HTTP-Empfangstimeouts und `http_status=0`; die alte Verknüpfung bleibt nutzbar.
Ein späterer erfolgreicher Playbackabruf kann den allgemeinen Fehler überlagern,
weshalb der Helfer dann nur die generische Nichtbestätigung anzeigen kann.

Der Tokenpfad behandelt `-ESP_ERR_HTTP_EAGAIN` der gepinnten IDF-Funktion nun als
vorübergehende Lesepause: höchstens 30 Sekunden innerhalb derselben Verbindung,
mit begrenzter verbleibender Socketwartezeit. Kein erneuter POST mit demselben
Autorisierungscode, keine Änderung an PKCE, Zertifikaten oder Bestätigungsbindung.
Andere HTTP-Fehler werden nicht automatisch wiederholt. Feste numerische
Token-Diagnosen unterscheiden HTTP-Code, Transportphase, Fehlernummer, Laufzeit
und bestätigte Speicherung; keine Codes, Tokens, Antworten oder Kontodaten.

623 Modell-/1791 Worker-Assertions und 51 Helpertests bestehen. Ein zusätzlicher
Test mit dem echten IDF-Headerempfänger und HTTP-Parser bestätigt Weiterempfang
nach Timeout sowohl vor als auch mitten in fragmentierten Antwortheadern.
Native Workerfälle prüfen verzögerte Antwort, Deadline, abgelehnten Grant,
Abbruch, unveränderte Altverknüpfung und genau einen POST. Alle drei Profile bauen.
Der gezielte Kandidat ist nach erneut geprüfter Originalbackup-SHA und
S3-/16-MiB-Identifikation ausschließlich als App an `0x10000` installiert:
2.733.824 Bytes, SHA-256
`2552d397c15ac106b6a535a9c5c4640709b2ecb3e487b4ad612d55e15e6159b1`.
Separater vollständiger Flashvergleich und USB-HELLO mit dev.16 bestehen.
Quellbaum vor Installation: `9116bde575c4b13b64b7734092080616270ba5db` auf
`1bfc944`. Fünf LAN-Messrunden liefern alle 25 Antworten erfolgreich; JavaScript
62–88 ms, alle 15 Assets nach Dekompression exakt gegen die Quellen geprüft.
Die neue Anmeldung mit Nutzerzustimmung steht noch aus; eine Behebung des
realen Tokenproblems wird damit noch nicht behauptet. Die GitHub-CI des
vorherigen dev.15-Standes `1bfc944` ist inzwischen vollständig erfolgreich.


#### Erfolgreiche Neuanmeldung und Zuordnung der namenlosen Ausgabe

Der Nutzer bestätigt unter dev.16 „verbunden“. Die begrenzte Diagnose belegt den
neuen Vorgang separat von der Altverknüpfung: Callback angenommen, Tokenabruf
`initial=1`, HTTP 200, Transport 0, Phase 5, 435 ms, `confirmed=1`; danach
`ready`, `linked=true`, `error=none` und `attempt_confirmed=true` im Helfer.
Damit ist diese neue Anmeldung am Gerät und durch den Nutzer bestätigt. Dieser
Versuch brauchte keine verlängerte Empfangswartezeit; er beweist daher nicht,
dass alle zuvor beobachteten Transportabbrüche ursächlich behoben sind. Einzelne
spätere Playbackabrufe scheitern noch vor den Headern bzw. im Body; erfolgreiche
Abrufe danach belegen Erholung. Keine Geheimnisse oder Antwortinhalte protokolliert.
Beide GitHub-CI-Läufe für Implementierungscommit `48e2431` sind erfolgreich.

Der Nutzer ordnet „Ausgabe ohne Namen“ seiner älteren **Yamaha Pianocraft** zu.
Das ist eine Nutzerzuordnung, keine aus Spotify-Metadaten abgeleitete Geräte-
Identität und keine Zuordnung zum Sonos Roam. Exakte Pianocraft-Modellnummer,
Connect-Empfänger und hörbare Knob-Steuerung dieser Anlage sind damit noch nicht
nachgewiesen. Die Firmware erhält keine hart codierte Umbenennung oder Geräte-ID-
Zuordnung. Die echte Playlist-/Podcast-Bibliotheksanzeige wird nach der neuen
Anmeldung als eigener nächster Schritt geprüft.


### dev.17-Diagnose und dev.18-Speicherfix: Webfreigabe/Ladezeiten

Der Nutzer meldet nach „Web freigeben“ erneut endloses Laden in iPhone/Safari.
Die früheren schnellen dev.15/dev.16-Kurzserien waren keine Zuverlässigkeits-
abnahme. Auf dev.16 bei offenem LAN-Fenster laufen alle fünf JS-Abrufe einer
25-Anfragen-Serie nach etwa 12 s in ein Timeout; Status und Sitzung antworten
meist in 28–63 ms. Nach Ablauf des Fensters bleiben Ausreißer bis 10,625 s.
Das QR-Fenster allein ist damit nicht als Ursache bewiesen.

Ein unabhängiger Browserfehler ist deterministisch reproduziert: Ein laufender
Statusabruf verschluckt das nach erfolgreichem Pair-POST angeforderte Session-
Refresh. Der alte Read-only-Zustand bleibt erhalten. Der neue Regressionstest
schlägt gegen den alten Code fehl und besteht mit der serialisierten expliziten
Aktualisierung in Chromium/WebKit bei 360, 390 und 1280 px. Die UI bestätigt
Freigabe erst bei tatsächlich schreibfähiger Sitzung; automatische Visuals
bleiben während des LAN-Freigabefensters geschlossen.

Der dev.17-Diagnoseflash ist nach erneuter Originalbackupprüfung und
S3-/16-MiB-Identifikation separat vollständig verglichen und über HTTP als
dev.17 bestätigt: 2.734.608 Bytes, SHA-256
`47db212a69d40d874e8dd02dd0d08329d902cfbc4beb52c5aea52038dd22ae50`,
Quellbaum `0a1b0c30dbac09e9ba260d1f192842caf8cad326` auf `a434004`.
Die neuen numerischen Laborwerte zeigen nach 16 s nur 11.555 Bytes freien
internen Heap; nach 36 s 4.663 Bytes, größter Block 1.600 Bytes. Das Minimum
sinkt auf 1.067 Bytes. RSSI liegt bei etwa −62/−63 dBm, Stromsparmodus ist 0
(`WIFI_PS_NONE`). Bereits der erste JS-Abruf läuft in ein Timeout; in den
zuerst ausgewerteten 33 Antworten scheitern 20. Der USB-HELLO bleibt ohne
Antwort. dev.17 ist ausdrücklich kein erfolgreich abgenommener Performancefix.

Das gelinkte Programm enthält einen 65.536-Byte-LVGL-Pool in `.dram0.bss`.
dev.18 aktiviert die von IDF vorgesehene externe BSS-Unterstützung und legt
nur das LVGL-Allocatorobjekt über `pw_ui/lvgl_psram.lf` in `.ext_ram.bss`.
ESP-IDF ordnet damit zusätzlich seine dafür vorgesehenen Netzwerk-BSS-Daten
extern zu. Die bestehenden Display-DMA- und Stackplatzierungen bleiben
unverändert. Keine Vergrößerung der Puffer, keine erneute Änderung des
TCP-Sendetimings. Die neue ELF-Prüfung lehnt den vorherigen dev.17-Standardbuild
ab und bestätigt 65.536 Bytes im PSRAM des neuen Laborbuilds; sie läuft künftig
auch in CI für beide S3-Profile.

Der dev.18-Laborkandidat ist privat archiviert: 2.734.784 Bytes, SHA-256
`d2b19fd3cf23fb6ca24e3e00d336e01aa471369cec386934c7877e5748778748`,
Quellbaum `b57b52ca0a7182e1ea83921f5c697718f2c04e18` auf `a434004`.
Der Vergleichsflash steht noch aus: Der USB-Port antwortet bei zwei
Identifikationsversuchen nicht; der Nutzer wurde um Wiedereinstecken in
unveränderter Orientierung gebeten. Kein Schreibversuch mit dev.18 ausgeführt.
Ladezeit, Freigabe und Bedienbarkeit am echten iPhone bleiben offen.

Lokale Prüfungen: alle drei Profile kompilieren als dev.18, beide S3-ELFs
bestehen die neue Speicherprüfung, Versionsprüfung aller drei Images besteht.
51 Frameworktests, LAN-Gate/HTTP-Autorisierung und 25 echte HTTP-Writerfälle
mit 20 exakten gzip-Roundtrips bestehen. Browser-Freigabetest: sechs Kombinationen
aus Chromium/WebKit und drei Bildschirmbreiten bestanden. Simulierte HTTP-/
Browserfälle beweisen keine reale WLAN- oder iPhone-Performance.

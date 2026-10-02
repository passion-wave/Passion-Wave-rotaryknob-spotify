# Native Implementierung und Geräteabnahme

Stand 02.10.2026, Entwicklungsstand `0.1.0-dev.10`. Dieses Dokument ergänzt den Gesamtplan; es erklärt **keine vollständige Feature-, Pilot- oder Produktabnahme**.

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

Im normalen Heimnetz ist dieser Stand **nur lesbar und ohne private Konfigurations-/Standortdaten**. Das erfüllt die geforderte sichere, jederzeit schreibbare Heimnetz-Website (Q02/G2) noch nicht. Ein selbstsigniertes Zertifikat mit Browserwarnung wird nicht als fertiges Kunden-Onboarding ausgegeben. Die vorgeschlagene mobile Spotify-Einrichtungs-App ist eine offene Nutzerentscheidung und löst G2 nicht automatisch.

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
bleiben Antwortfehler. Das konkrete Verhalten am betroffenen Gerät ist noch zu
prüfen; dev.10 ist noch nicht installiert.


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

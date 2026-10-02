# Native Implementierung und Geräteabnahme

Stand 02.10.2026, Entwicklungsstand `0.1.0-dev.7`. Dieses Dokument ergänzt den Gesamtplan; es erklärt **keine vollständige Feature-, Pilot- oder Produktabnahme**.

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
| Spotify | Direkter S3-Web-API-Provider im expliziten Laborprofil; USB-PKCE, Refresh, Ausgabewahl, Play/Pause/Next/Previous/Volume, Playliststart und echte UI-/Websitebindung; eigene Lab-App samt Redirect angelegt; Standardprofil deaktiviert | Echte Konten-/Roam-/Move-Tests, erweiterte Medienfunktionen, kommerzielle Controller-Freigabe und mobiler Kunden-Anmeldeweg |
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

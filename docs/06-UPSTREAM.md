# Übernahme aus dem bisherigen RotaryKnob-Projekt

Stand: 30. September 2026. Die Analyse war ausschließlich lesend. Bestehende Quellverzeichnisse, Kundendateien, Firmware und Geräte wurden nicht verändert. Es wurde noch kein Bestandsquellcode in dieses Framework kopiert.

## 1. Nachvollziehbare Herkunft

| Fundstelle | Exakter Commit | Einordnung |
| --- | --- | --- |
| `/Users/CZ/Documents/GitHub/Passion-Wave-rotaryknob` | `d16e0a944fe3a20091bca7eeb8ffa18abce23c65` | Aktiver lokaler Checkout `codex/release-3.0.1-beta.7`; älter als weitere vorhandene Arbeitskopien |
| `/Users/CZ/Documents/temp/pw-beta14-fw` | `5b81b0e75c0467cecfa6eab013803c903cbff1f5` | Enthält tatsächlich `3.0.1-beta.15`, trotz Verzeichnisname |
| `/Users/CZ/Projects/rotary_JC3636K518C/weather-avatar-test/Passion-Wave-rotaryknob` | `1846b68609084485e3b3efeb17578de614e294b2` | Neuere Feature-Arbeitskopie, `3.0.1-beta.16`, 28. September; lokale Releaseänderungen vorhanden |
| `/Users/CZ/Projects/rotary_JC3636K518C/weather-avatar-test/avatar-docs-followup` | `9cc5576c2fd4a9beb56cad24e211957c43aa2c62` | Sauberer Dokumentationsnachfolger, `3.0.1-beta.16`; festgelegter Referenzstand für spätere gezielte Übernahme |

Der letzte Commit ist die Herkunftsreferenz, keine Empfehlung zur vollständigen Übernahme dieses Featurezweigs. HA-/MA-Anbindung und Kundenprofile werden ausgeschlossen. Auf späteren ausdrücklichen Nutzerwunsch wird Wetter-/Avatarverhalten gezielt portiert; Datenabruf und Radarquelle entstehen auf dem S3 neu, siehe [Wetter](14-WETTER.md). Jede spätere Übernahme benennt Datei, Commit, unveränderten oder angepassten Umfang und zugehörige Tests. Bei Abweichungen von den hier zitierten älteren Zeilen ist der festgelegte Commit maßgeblich.

Die geprüfte Root-Lizenz ist MIT. Bei Übernahme bleiben die ursprünglichen Copyright- und Lizenzhinweise erhalten. Schriftarten, Bilder und eingebundene Bibliotheken behalten ihre eigenen Bedingungen. Ein optionales proprietäres Spotify-SDK unterliegt separat dem Partnervertrag. Keine `secrets.yaml`, realen Gerätekennungen, WLAN-Daten, API-/OTA-Schlüssel oder historischen Kundeneinstellungen importieren.

## 2. Was die bestehende Hardware tatsächlich verwendet

Die folgenden **Softwarekonfigurationen** sind direkt im Bestandsprojekt belegt; eine neue Hardware-Abnahme ersetzen sie nicht. Zeilen beziehen sich auf den älteren Hauptcheckout oben.

| Funktion | Beleg im bisherigen Repository | Für das neue Produkt |
| --- | --- | --- |
| S3, 240 MHz, 16 MB Flash, octal PSRAM | `esphome/rotaryknob-s3-ui-core.yaml:170` | S3 wird Produktcontroller; Hersteller nennt 8 MB PSRAM |
| Klassischer ESP32, 240 MHz, 4 MB Flash | `esphome/dual-mcu-esp32-core.yaml:57` | Kein PSRAM konfiguriert; kleiner Begleitprozessor |
| QSPI-Display 360×360 | `rotaryknob-s3-ui-core.yaml:313` und `:2224` | CLK13, D0–3 GPIO15/16/17/18, CS14, RESET21; ST77916-Sequenz gezielt portieren |
| Touch CST816 | `rotaryknob-s3-ui-core.yaml:319` und `:1623` | SDA11/SCL12, Interrupt9, Reset10; I2C 400 kHz |
| Haptik DRV2605 | `rotaryknob-s3-ui-core.yaml:325` | I2C-Adresse 0x5A, LRA |
| Backlight | `rotaryknob-s3-ui-core.yaml:2043` | GPIO47 PWM |
| EC1-Eingabe | `docs/rotary-recognition.md:3` | GPIO7 links / GPIO8 rechts, getrennte aktive Low-Pulse; **keine Quadratur** |
| EC2 | Dasselbe Dokument `:14` | Classic GPIO22 links / GPIO19 rechts; doppelte mechanische Bewegung, nur Diagnose |
| UART | `dual-mcu-s3-core.yaml:52`, `dual-mcu-esp32-core.yaml:387` | S3 TX38/RX48, Classic TX23/RX18; bisher 2 Mbaud, 8N1, RX-Puffer 4096 |
| Batteriebeobachtung | `rotaryknob-s3-ui-core.yaml:11000` | S3 ADC GPIO1; Spannungsmodell, kein nachgewiesener Fuel Gauge |

Ein Druckschalter im Ring ist nicht belegt. Die normale Benutzeraktion erfolgt durch Drehen und Touch. EC1 und EC2 dürfen nicht addiert werden.

## 3. Neue Zuständigkeiten und Stromversorgung

Bislang rendert der S3 die Oberfläche; der klassische ESP32 übernimmt Netzwerkdownloads und HA-Zustand. Die HA-Integration besitzt den autoritativen Medienzustand, Freigaben und Updatekoordination. Nach Entfernung von HA/MA müssen diese fachlichen Aufgaben ausdrücklich neu implementiert werden.

Im neuen Produkt besitzt der S3 alle Anwendungskomponenten: WLAN, lokale Website, Zugangsdaten, Favoriten, Provider, OTA-Koordination und UI. Der klassische ESP32 bleibt Begleiter für benötigte IO-/Mute-Funktionen, EC2-Diagnose und seinen Updateempfänger. Ein WLAN-Client und eine IP-Adresse vereinfachen Einrichtung und Betrieb.

Der bisherige regelmäßige S3-Tiefschlaf ist dafür ungeeignet: Website und Provider wären dann offline. Im normalen erreichbaren Betrieb darf nur das Display ausgehen; geeignete Modem-Sleep-Stufen müssen gemessen werden. Expliziter Batterieschlaf wird als offline angezeigt. Kein vorhandener Laufzeitwert wird als neue Akku-Zusage übernommen.

Die 8 MB PSRAM machen den S3 zum plausiblen Host, beweisen aber keine stabile parallele UI-/TLS-/Audioausführung. Interne DMA-Puffer, zusammenhängender Heap, Task-Stacks, Flashpausen und Watchdog bleiben harte Budgetfragen. [Firmware-Gates](../firmware/README.md) müssen vor einem Geräte-Build belegt sein.

## 4. Portierungskarte

| Baustein im Referenzprojekt | Wiederverwendung | Erforderliche Anpassung |
| --- | --- | --- |
| `esphome/ec1_pcnt_encoder.h` | Höchste Priorität: hardwaregestützte Richtungspulse, Overflow, Diagnose, Common-Mode-Abweisung | IDF-Komponente, Pinprofil, Lasttests; keine generische Quadratur-Bibliothek einsetzen |
| `esphome/dual_mcu_encoder.h` | EC2-Diagnose bei Bedarf | Keine zweite Bedienautorität |
| `esphome/dual_mcu_link.h` | COBS/CRC, Grenzen, Prioritäten, Sequenzen und fehlerfeste Framing-Ideen | ESPHome-UART-Bindung entfernen, neues Produktprotokoll/Identität; HA-/Wetter-/Lichttypen ausschließen |
| `esphome/dual_mcu_library_proxy.h` | Paginierung, Generationen, begrenzte Payloads | Bibliothek liegt neu auf S3; keine unnötigen Katalogkopien im ESP32, Spotify-ID/Radio-UUID statt MA-URI |
| `esphome/dual_mcu_radar_proxy.h` | Nur relevante generische Transfer-/ACK-Ideen | Radar direkt auf S3 neu anbinden; Hausdaten entfallen; OTA braucht eigene authentifizierte Prüfung |
| `esphome/ui_next_framework.h` | Medienseite, Favoritenauswahl, Stile und Dialogverhalten | ESPHome-Fontwrapper auf LVGL abstrahieren, nur Produktseiten übernehmen |
| `esphome/rotaryknob-s3-ui-core.yaml` | Panelinitialisierung, Gesten, Haptik, optimistische Lautstärke, asynchrone Coveranzeige | Monolith zerlegen; Vollbild-Cover nicht ungeprüft zuschneiden, Spotify-Designregeln beachten |
| `esphome/responsive_power_policy.h` | Reaktionsfähiger Betrieb und modem-sleep als Zustandspolitik | ESPHome-Abhängigkeit entfernen, Dienste nicht durch normalen Tiefschlaf abschalten |
| `custom_components/passion_wave` | Fachliche Erfahrungen zu Zielprüfung, letzter Auswahl, Bestätigung und konsistenten Metadaten | Keine Python-/HA-Laufzeit übernehmen; Logik in `product_core` neu implementieren |
| Bestehende Releasewerkzeuge | Unveränderliche Artefakte, Hashes, gekoppelte Versionen, Fehlerfälle | Neues unabhängiges Produkt, Signaturen, lokaler S3-Koordinator, UART-OTA und per-Chip-Bootprüfung |
| Bestehende Tests und Abnahmekataloge | Schnelles Drehen, verspätete Zustände, Coverwechsel, Wake, Strom-/Netzausfälle | Verhaltensprüfungen portieren; alte YAML-/Entity-Textprüfungen sind kein neuer Funktionsnachweis |

Bestehende Bibliotheks-/Bildgrößen sind historische Grenzen, keine vorgeschriebene Produktkonfiguration. Coverdaten werden im neuen S3-Host nicht unnötig durch den Begleitprozessor geleitet.

## 5. Klinke und I2S: gesicherte Quelle, noch offene Gerätebestätigung

Das [offizielle Guition-Handbuch](https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/FTPData/UEditor/file/2026121/1768961095200/JC3636K518%20Instructions-EN.pdf) bestätigt die Produktfamilie, Displaydaten, Speicher sowie Bluetooth-Audio und lokale Webkonfiguration der Werkssoftware. Es liefert keinen vollständigen Schaltplan.

Der [offizielle Waveshare-Schaltplan](https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-schematic.zip) des vergleichbaren Dual-MCU-Knobs wurde bildlich geprüft. **Alle folgenden Audio-Routen sind bis zur Prüfung der tatsächlichen Guition-Platinenrevision Kandidaten:**

| Schaltblatt | Aussage |
| --- | --- |
| `5_DAC.png` | PCM5100A, CH445P als I2S-Umschalter zwischen beiden MCUs, kein Kopfhörerverstärker auf den fünf gelieferten Blättern |
| `2_ESP32S3-R8.png` | S3 WS40, DATA41, BCLK39; Umschaltung über GPIO0, zugleich Boot-Strapping-Pin |
| `3_ESP32-CHIP.png` | Classic WS27, DATA26, BCLK25; DAC-Mute XSMT an GPIO32 |
| `1_LCD&POWER.png` | Analog OUTR/OUTL gehen auf CN1-Pins9/10; tatsächliche Klinken-Tochterplatine fehlt |

Damit besteht im Referenzschaltplan kein Display-/I2S-Pinkonflikt. Die im Netz ebenfalls zu findenden GPIO16/17/18-Werte anderer JC3636W518-Boards dürfen nicht übernommen werden. Mute und Mux brauchen eine gemeinsame sichere Zustandsmaschine, insbesondere bei Start, Reset, Update und Schlaf.

[TI spezifiziert den PCM5100A](https://www.ti.com/product/PCM5100A) als Line-Ausgang mit 2,1 Vrms und Lasten ab 1 kΩ. Daraus folgt keine Eignung für normale 16–64-Ω-Kopfhörer. Ohne bestätigten Verstärker/Lasttest heißt die Fähigkeit zunächst `local_line_out`; Kopfhörermodus bleibt gesperrt. Lokales Radio ist ein separates Decoder-Projekt, Spotify-Audio zusätzlich ein zugelassenes SDK-/Zertifizierungsprojekt.

PCM wird nicht über den bestehenden UART-Austausch geschickt: Stereo mit 44,1 kHz und 16 Bit benötigt bereits 1,4112 Mbit/s; 2-Mbaud-UART mit 8N1 liefert vor Framing/ACK höchstens 1,6 Mbit/s. Audio wird auf dem zuständigen Prozessor direkt geladen und decodiert; UART überträgt kleine Steuer-/Mute-Nachrichten.

## 6. OTA und physische Wiederherstellung

Die bisherige ESPHome-OTA-Existenz liefert keinen UART-Updateempfänger. Dieser muss auf dem Begleitprozessor neu entstehen: signiertes Release prüfen, nur eigene Rolle/Hardware akzeptieren, Offset/Größen begrenzen, Zielslot beschreiben, vollständigen Hash prüfen und neuen Boot melden. Der S3 koordiniert Begleiter zuerst und danach sich selbst; ein dauerhaftes Journal und N/N+1-Kompatibilität decken die Zwischenstände ab.

Ein S3-gesteuerter EN-/BOOT-Zugang zum klassischen ESP32 ist im geprüften Schaltplan nicht nachgewiesen. Ein Begleitprozessor, dessen Anwendung nicht mehr startet, kann daher physischen USB-Zugriff benötigen. Die bekannte USB-C-Orientierung zur Auswahl der beiden Prozessoren bleibt ein dokumentierter Werkstatt-/Recovery-Pfad. Dieses Framework verspricht kein fernbedientes Entsperren beliebig beschädigter Firmware.

Die spätere Abnahme trennt Quellcodebelege, simulierte Tests und echte Gerätebeobachtung. In dieser Vorarbeit wurden ausschließlich Dokumente und Quellcode gelesen, Referenzschaltbilder angesehen und Schnittstellen formuliert.

## 7. Ergänztes Featureaudit einschließlich Wetter

[Featuretabellen und exakte Quellanker](13-FEATURE-PORTIERUNG.md) sind die vollständige Portierungsliste zum Commit `9cc5576`. Neue Befunde: UI Next ist im Rollenprofil aktiv; Playliststart verwendet den Kontext; Herz/Power sind lokale Flags; nur Einzeltracks werden im alten Broker beobachtet bestätigt. Seek, Episodenbrowser und Geräte-/Gruppenverwaltung sind Erweiterungen.

Wetterseite, Screensaver, Outfitresolver, Haarwahl und Morgenzeitplan werden übernommen. HA-Forecastaggregation, Wettersensoren und Radarrenderer werden durch direkte S3-Adapter ersetzt. Der allgemeine alte Forecastparser wird wegen Offset-/Ersatzwertproblemen nicht blind kopiert. Das mitgelieferte HA-Radarbeispiel ist ein fester Ausschnitt, kein vollständiger universeller Drei-Zoom-Renderer.

Gemessene Quelldateigrößen: 15 Wetter-JPEGs zusammen 361.439 Bytes; deren RGB565-Rohdaten wären 4.062.720 Bytes. Avatar dedupliziert laut Manifest 549.141 Bytes; RGB565-Doppelbuffer 541.696 Bytes. Keine Aussage über freien Heap oder fertige neue Firmwaregröße. [Wetterarchitektur](14-WETTER.md).

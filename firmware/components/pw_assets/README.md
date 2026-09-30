# Lokale Wetter- und Avatarbilder

Alle **52 Avatar-Zuordnungen und 15 Wetterbilder** des freigegebenen Bestands sind
enthalten. Identische JPEG-Dateien werden genau einmal eingebettet: 55 Dateien,
910.580 Byte. Es gibt keinen Download, Platzhalter-Avatar oder Netzwerk-Demozustand.
Die Firmware zeigt Bilder ausschließlich passend zum realen Wetter-Snapshot.

## Herkunft und Lizenz

Quelle ist der feste Commit
[`9cc5576`](https://github.com/passion-wave/Passion-Wave-rotaryknob/tree/9cc5576c2fd4a9beb56cad24e211957c43aa2c62)
des MIT-lizenzierten PassionWave-Projekts. Die unveränderte Repository-Lizenz
steht in [UPSTREAM-LICENSE.txt](UPSTREAM-LICENSE.txt). Der Import erzeugt keine
neuen Bilder, verändert keine Pixel und prüft jede Datei gegen den ursprünglichen
SHA-256. [manifest.json](manifest.json) verbindet sämtliche lokalen Namen mit
Originalpfad, Hash und Größe; die ursprüngliche Avatar-Provenienz bleibt in
[upstream-avatar-manifest.json](upstream-avatar-manifest.json) erhalten.

Die historische [Wetterbild-Provenienz](upstream-weather-provenance.md) nennt
acht Ausschnitte aus der damals freigegebenen Nutzervorlage und sieben generierte
Ergänzungen. Ihr einziger Markdown-Dateilink wurde auf den festen Upstream-Commit
umgestellt. Die dort beschriebene alte Home-Assistant-Laufzeit und der
`partlycloudy`-Fallback gelten **nicht** für diese Firmware. Die Avatarbilder
stammen aus den im Originalmanifest dokumentierten generierten Atlanten.
Die Repository-Lizenz und Herkunft sind nachvollziehbar erhalten; dies ist keine
zusätzliche Rechtezusage für externe Vorlagen über deren dokumentierte Freigabe hinaus.

Der Decoder verwendet die bereits in `lvgl@9.2.2` mitgelieferte TJpgDec-R0.03-
Implementierung über `jd_prepare`/`jd_decomp`, ohne deren Quellcode zu kopieren.
Diese trägt die eigene permissive ChaN-Lizenz, Copyright 2021: Nutzung und
Weitergabe sind erlaubt, bei Quellcodeweitergabe muss der Lizenztext erhalten
bleiben. Den Hinweis deshalb auch bei der Weitergabe des vollständigen
Firmware-Quellpakets erhalten; nicht pauschal alles als MIT umetikettieren.

## Speicher und Nebenläufigkeit

| Bestandteil | Festes Budget |
|---|---:|
| Komprimierte, eingebettete JPEGs | 910.580 Byte Flash |
| Zwei dekodierte Bilder, 368×368 RGB565 | 541.696 Byte PSRAM |
| Decoder-Arbeitsbereich | 16.384 Byte interner RAM |
| Decoder-Taskstack | 8.192 Byte interner RAM |
| Metadaten / Synchronisierung | Kleine feste Tabellen; keine Bild-Heap-Kaskade |

Bild-, UI-Zeilen- und Board-DMA-Puffer sind getrennt. Der tatsächliche freie
Systemheap unter gleichzeitigem WLAN, TLS, Wetter und OTA muss am Gerät gemessen
werden. Zwei 5,5-MiB-Appslots sind im 16-MiB-S3-Layout vorgesehen. Der isolierte
Board-/UI-/Asset-Linkcheck lag bei `0x185380` Byte; er verwendet Backend-Testdoubles
und ist deshalb ausdrücklich **keine** vollständige Produktgrößenmessung.

Eine Task mit Priorität 1 dekodiert nur bei neuem Bild. Sie verwendet den
reentranten JPEG-Kern ohne LVGL-Aufrufe. Anfragen ersetzen ältere Aufträge;
Abbruch wird zwischen JPEG-MCU-Blöcken geprüft. Ein gerade angezeigter Slot
bleibt unverändert. Erst nach Verbergen/Quellwechsel und LVGL-Cachefreigabe darf
die UI ihn zurückgeben. Fertige alte Generationen werden verworfen. Es gibt
weder eine wachsende Queue noch die Dekodierung aller Bilder beim Start.

Jeder Decoderaufruf prüft Eingabegrenze, SOI/EOI, tatsächliche 368×368-Abmessungen,
Ausgabekapazität und Rechteckgrenzen. Nur vollständig dekodierte Bilder werden
sichtbar. Ein Abbruch, Fehler oder fehlender PSRAM ergibt eine Textmeldung.
LVGL zeigt den nativen 368×368-Puffer bei Position −4/−4. Keine Laufzeitskalierung:
das vermeidet den im Bestandsgerät dokumentierten RGB565-Randfehler.

## Zuordnung und Grenzen

Avatar-IDs 0–51 entsprechen unverändert dem portierten Wettermodell. Die UI
fragt `pw_weather_avatar_resolve` mit dem tatsächlichen Vierstundenfenster und
der gewählten Haarfarbe ab. Ungültige/veraltete Daten zeigen kein Avatarbild.

Wetterfotos erkennen die dokumentierten MET-Symbolstämme einschließlich
`_day`, `_night`, `_polartwilight` und der erhaltenen Schreibweisen
`lightssleetshowersandthunder` / `lightssnowshowersandthunder`.
Siehe [MET-API-Dokumentation](https://api.met.no/weatherapi/locationforecast/2.0/documentation).
Unbekannte Codes ergeben kein Bild. Schnee-/Schneeregengewitter nutzen das
Gewittermotiv; Regengewitter das Regen-/Gewittermotiv. Die zusätzlichen
Bestandsmotive `exceptional`, `hail`, `windy`, `windy-variant` sind vollständig
mitportiert, werden vom MET-Symbolmapping jedoch nicht ohne entsprechenden
Datenbeleg ausgewählt. Insbesondere wird aus einer Windgeschwindigkeit keine
unbelegte Unwetterwarnung abgeleitet. Radar ist kein Bestandteil dieses Moduls.

## Reproduzieren und prüfen

`python3 import_assets.py /pfad/zum/bestandscheckout` verlangt den festen Commit
und liest JPEG-Bytes, Dateiliste, Hashlisten, Manifeste, Lizenz und Provenienz ausschließlich aus dessen Git-Objekten. Der Import benötigt
keine Netzwerkverbindung. Er liefert wieder dieselben 67 Zuordnungen und Hashes.

`python3 tests/run_host.py --lvgl /pfad/zu/lvgl__lvgl` prüft mit Clang,
AddressSanitizer und UndefinedBehaviorSanitizer sämtliche 55 JPEGs:
vollständige Dekodierung, vier unabhängige RGB-Farbreferenzen pro Originalbild,
Pufferwächter, zu kleine Puffer/Arbeitsbereiche,
abgeschnittene Dateien mit und ohne EOI sowie frühen und mittleren Abbruch.
Zusätzlich werden Hashes, Flashbudget und MET-Zuordnungen einschließlich
unbekannter Codes geprüft. Der Runner lädt nichts herunter und hinterlässt
keine Testbinaries im Repository.

Die Farbreferenzen in `tests/color_reference.json` stammen aus einer unabhängigen
Dekodierung der unveränderten Original-JPEGs mit macOS ImageIO (`sips-316`).
`tests/generate_color_reference.py` reproduziert diese numerischen Referenzen
auf macOS. Der normale Testlauf benötigt kein macOS. Verglichen werden vier
16×16-Regionen pro Bild; 12 Helligkeitsstufen pro RGB-Kanal tolerieren
RGB565-Quantisierung und Unterschiede der JPEG-Chromainterpolation. Dieser
Test deckt den beim direkten Originalvergleich gefundenen Rot-/Blau-Tausch ab:
Der LVGL-9.2.2-Fork liefert bei `JD_FORMAT=0` tatsächlich **B,G,R**, obwohl der
übernommene Konfigurationskommentar RGB sagt. Die Konvertierung folgt den
tatsächlichen Ausgabebytes. Der allgemeine Speichersicherheitstest allein
konnte diesen Farbfehler nicht entdecken.

Die Prüfungen bestätigen Compiler-, Speichergrenzen- und Zuordnungslogik.
Darstellungsqualität, Decodezeiten, Ringreaktion während des Dekodierens und
Langzeit-/OTA-Mischlast bleiben zusätzlich am physischen S3 zu bestätigen.

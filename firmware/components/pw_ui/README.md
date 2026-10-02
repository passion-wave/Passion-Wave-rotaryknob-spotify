# Geräteoberfläche

LVGL 9.2.2, 360×360, native Board-API. `pw_ui_init()` nach `pw_board_init()` und
`pw_app_init()` starten. Der einmalige Start wartet höchstens fünf Sekunden
auf die LVGL-/Widgetinitialisierung. Die Anzeige tatsächlicher Pixel bleibt
separat über Displaycompletion und physische Beobachtung nachzuweisen.

Eine Task besitzt sämtliche LVGL-Aufrufe. Board-DMA-Completion meldet sich
über eine Queue; die UI gibt ihren Bildpuffer erst nach echter Completion frei.
Der `flush_wait_cb` verhindert den LVGL-9-Deadlock, der entstünde, wenn nur eine
äußere Schleife Completions abholt. Im Betrieb werden keine Netzabfragen,
I2C-Transfers oder NVS-Schreibvorgänge im LVGL-Callback ausgeführt. Setupaktionen
laufen auf einer getrennten Steuerungstask; Helligkeitsänderungen verwenden
die zusammengefasste Anwendungseinstellung.

## Bedienung im aktuellen Laborstand

- Einrichtung: individueller WLAN-QR, danach QR/Adresse zur lokalen Website.
  `Weiter` und `Zurück` wechseln die beiden Schritte; das Kreuz schließt das
  Zeitfenster. QR enthält ausschließlich den individuellen Geräte-AP-Zugang.
  Kein Token, Kennwort oder QR-Inhalt wird geloggt. Beim Schließen verschwindet
  das QR-Muster. Dies ist der Labor-AP-Weg, noch kein nachgewiesener sicherer
  schreibender Heimnetz-Zugriff auf allen Kundenbrowsern.
- Drei klare Ziele: Musik, Wetter, Gerät. Das Standardprofil hält Spotify
  deaktiviert. Im Laborprofil führt `Musik` zur echten Spotify-Steuerung:
  oben `Ausgabe wählen`, Play/Pause, vor/zurück und Lautstärke am Ring. Die
  erste Geräteanmeldung auf dev.8 ist bestätigt; physische Bedienprüfung und
  hörbare Wiedergabe auf den Connect-Lautsprechern bleiben offen.
- Die offene Ausgabeliste folgt ab dev.9 neuen Gerätezuständen auch nach dem
  ersten Zweisekundenfenster. Während einer Berührung und bis 750 ms Eingaberuhe
  bleiben Zeilen und Geräte-IDs gebunden. Ein Kontositzungswechsel schließt die
  Liste; Favoriten werden von dieser automatischen Geräteaktualisierung nicht
  verändert. `python3 firmware/components/pw_ui/tests/run_picker.py` prüft die
  Produktionsfunktionen mit simulierten LVGL-/Providergrenzen und ASan/UBSan;
  es ist kein Render- oder physischer Touchtest.
- Wetter zeigt den realen Snapshot mit Temperatur, Zustand, Wind, Regenmenge
  pro Stunde, zwei Tagesprognosen und Herkunft. Fehlende Werte werden nicht
  durch Null ersetzt. Der Wetterzustand bleibt ausdrücklich eine Prognose.
- `Outfit` öffnet den Avatar für das tatsächliche kommende Vierstundenfenster;
  `Foto` zeigt das passende Wetterbild mit analoger Uhr. Unbekannte/veraltete
  Wetterdaten ergeben Text ohne Bild. Alle originalen Bildzuordnungen sind im
  [lokalen Bildmodul](../pw_assets/README.md) enthalten; die UI benötigt keine
  Bilddownloads. Tippen schließt die Ansicht und verbraucht die Geste vollständig.
- Der einschaltbare Morgenavatar erscheint zwischen 06:00 und 10:00 Ortszeit
  nach 30 Sekunden Ruhe, unter Beachtung der konfigurierten Zeitzone/Sommerzeit.
  Er hat Vorrang vor dem automatischen Wetterfoto, aber nie vor der Einrichtung.
  Um 10:00 verschwindet nur ein automatisch geöffneter Avatar. Der manuelle
  `Outfit`-Aufruf bleibt unabhängig vom Automatikschalter verfügbar. Haarfarbe
  und Morgenautomatik kommen aus den gespeicherten Websiteeinstellungen.
- Der konfigurierbare Modus `weather_photo` öffnet nach 60 Sekunden Ruhe das
  passende Foto; `off` schaltet diese Automatik ab. Der Ring schließt ein
  automatisches Foto und verbraucht die Bewegung. Im Avatar oder manuellen Foto
  weckt er lediglich die Helligkeit, ohne die Geräteeinstellung zu verändern.
- Gerät zeigt WLAN-/IP-Zustand und Helligkeit. Nur hier verstellt der Ring die
  Helligkeit. Einrichtungsbutton oder drei Sekunden ruhiger Touch öffnen das
  geschützte Zeitfenster. Eine bewegte Geste gilt nicht als langer Druck.
- Nach 60 Sekunden ohne Bedienung wird die Anzeige auf höchstens 15% gedimmt;
  nach fünf Minuten geht nur die Anzeige aus. Die erste Wakegeste verändert
  keine Einstellung. Während der Einrichtung bleibt der QR gut beleuchtet.
  Website, Netzwerk und beide Prozessoren werden dafür nicht schlafen gelegt.
  Solange der automatische Morgenavatar sichtbar ist, bleibt die Anzeige
  gedimmt an. Bei bereits dunklem Display weckt die erste Geste nur die Anzeige;
  eine zweite Berührung schließt gegebenenfalls die Bildansicht.
  Die Zeitwerte sind vorläufige Laborwerte; der vollständige konfigurierbare
  Energieport ist damit noch nicht erledigt.
- Haptik höchstens einmal alle 55 ms, nur bei angenommener Bedienung. Elektrisch
  mehrdeutige EC1-Signale wecken das Display nicht.

Die Seiten werden einmal erstellt; normale Aktualisierungen ändern nur Texte
oder Sichtbarkeit. Zwei kleine RGB565-Zeilenpuffer bevorzugen PSRAM. Ein
Wetter-Snapshot liegt statisch, nicht zusätzlich auf dem UI-Taskstack.
JPEGs dekodiert eine getrennte Task in zwei begrenzte PSRAM-Bildpuffer. Erst eine
vollständige passende Generation wird als native RGB565-Quelle angezeigt.
Die UI gibt vorherige Bildpuffer erst nach dem LVGL-Quell-/Cachewechsel frei.

## Schrift und Herkunft

Die Palette orientiert sich an den dokumentierten PassionWave-Farben des
MIT-lizenzierten Bestandsprojekts. Dieser UI-Code ist neu, keine vollständige
Kopie des ESPHome-/LVGL-8-Monolithen.

Die Standard-Montserrat-Schriften von LVGL enthalten keine vollständigen
deutschen Glyphen. `fonts/pw_font_de_*.c` ergänzen daher `ÄÖÜäöüß·–—…` und
verwenden die originalen LVGL-Schriften als Fallback. Linienhöhe/Baseline sind
an die jeweilige Elternschrift angepasst. Der native Rendercheck hat fehlende
Umlaute gefunden; die Ergänzung behebt die sichtbaren Ersatzkästchen.

Quelle: `lvgl@9.2.2/scripts/built_in_font/Montserrat-Medium.ttf`, SHA-256
`421f26b23e2be6b98373d32acd3cb2897b154d4bf0a77d26534ce476e4cbed53`.
Generator: `lv_font_conv@1.5.3`, vier Bit, Größen 14/18/24/32, unkomprimiert.
Der Font bleibt **SIL OFL 1.1**, unabhängig von der MIT-Code-Lizenz; vollständige
Lizenz und ursprüngliche eingebettete Copyrightangabe in `FONT-OFL.txt`.
`fonts/generate_fonts.py` regeneriert die Glyphen aus expliziten lokalen
Werkzeug-/LVGL-Pfaden und lehnt andere Fontdateien/Generatorversionen ab.
Keine Laufzeit-Fontdownloads.

## Geprüft und offen

- Isolierter ESP32-S3-Compiler-/Linkcheck mit IDF 5.4.3 und LVGL 9.2.2, realem
  Boardtreiber und ausdrücklich simulierten Anwendungs-/Wettersnapshots.
- Derselbe UI-Quellcode zusätzlich nativ mit LVGL gerendert: Einrichtungsschritte,
  Geräte-, Wetter-, Musik-, Foto- und Avatarseite auf Kreisgrenze, Umlaute, Umbruch und QR-Ruhezone
  visuell geprüft. Test-WLANname und Testkennwort stammen aus einem lokalen
  Renderer, nicht vom Nutzergerät. Native LVGL-Heapspitze im Setup: 31.408 Byte;
  keine Aussage über den gesamten ESP32-Heap unter Netzlast.
- Beide gerenderten QR-Codes mit `jsqr@1.4.0` dekodiert und gegen die erwarteten
  Testpayloads geprüft. Dies ersetzt keinen Scan des tatsächlichen Displays mit
  iOS/Android und keine beobachtete WLAN-Einrichtung.

Noch offen sind physische Darstellung/Touch/Ring/Haptik inklusive Bildwechseln,
Smartphone-Scan, Mischlast-/Langzeitprüfungen, Cover/Playerbibliothek, Radar,
vollständige Energieparameter und alltagsgeeigneter sicherer Heimnetz-Zugriff.
OTA-Dialogprioritäten müssen mit dem Update-Workflow verbunden werden; bisher
ist die lokale Einrichtungsansicht der blockierende UI-Dialog. Compiler und
native Renderings ersetzen die physische Geräteabnahme nicht.

# Direkter Geräte-Wetteradapter

Implementierungsstand 30. September 2026. Native ESP-IDF-Komponente für den S3,
kein HA-/MA-Zugriff und kein eigener Server. Das Modul stellt echte Forecastdaten
und die portierte deterministische Avatarentscheidung bereit. Die Integration in
Website, Anzeige, persistente Einstellungen und Assetdecoder liegt bei den
aufrufenden Komponenten. Ein erfolgreicher Hosttest ist kein Gerätetest.

## Datenquelle und tatsächlicher Spike

Der erste Transportadapter verwendet **MET Norway Locationforecast 2.0 /complete**.
Es ist eine begrenzte Umsetzung für den vorgesehenen Pilot mit zwei Geräten,
keine uneingeschränkte Flottenqualifikation. Die API liefert frei lizenzierte
Wetterdaten; Attribution lautet „Wetterdaten: MET Norway (CC BY 4.0); lokal
zusammengefasst“. In Website/Quellenansicht müssen zusätzlich Links zu
[MET Norway](https://api.met.no/) und
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) erscheinen. Forecastwerte
sind Modellprognosen, keine Messung am Knob. Standort und IP werden bei aktivierter
Wetterversorgung direkt an den Anbieter übertragen.

Die öffentliche Berlin-Abfrage `lat=52.5200&lon=13.4050` lieferte beim Spike am
30. September 2026 HTTP 200, 4.529 Byte gzip / 60.971 Byte JSON, `Last-Modified`
18:32:47 UTC und `Expires` 19:03:21 UTC. Die Daten selbst nannten `updated_at`
17:18:53 UTC. Diese Zeitpunkte werden ausdrücklich getrennt. Die Antwort enthielt
Temperatur, gefühlte Temperatur, Feuchte, Wind, Wolken, UV und Niederschlagsmenge.
Böen und Regenwahrscheinlichkeit fehlten für diese Abfrage und bleiben ungültig.
Der unveränderte JSON-Inhalt liegt mit Quellenvermerk bei den Hosttests.

**DWD MOSMIX_L wurde zuerst tatsächlich geprüft:** Einzelstation 10382, Datei
`MOSMIX_L_2026093015_10382.kml`, 17.276 Byte im KMZ, aber 344.439 Byte entpacktes
ISO-8859-1-XML. Das beweist weder Untauglichkeit noch zu wenig PSRAM. Es erfordert
einen eigenen begrenzten XML-/ZIP-Pfad oberhalb des 128-KiB-JSON-Limits und die
Stationsauswahl samt Parameternormalisierung. Dieser Adapter ist noch nicht
implementiert; MET ermöglicht inzwischen den direkten Datenpfad ohne Kostenkey.
DWD bleibt der bevorzugte weitere Deutschlandadapter aus dem Gesamtplan.

Quellen: [DWD-Einzelstation](https://opendata.dwd.de/weather/local_forecasts/mos/MOSMIX_L/single_stations/10382/kml/),
[MET-Lizenz](https://api.met.no/doc/License),
[MET-Nutzungsregeln](https://api.met.no/doc/TermsOfService),
[MET-Intervallformat](https://api.met.no/doc/ForecastJSON).

## Einbindung

`pw_weather_init()` einmal nach Netif/Eventloop-Initialisierung starten.
`pw_weather_configure(enabled, latitude, longitude, generation)` nach Laden oder
Ändern der Wettereinstellungen aufrufen. Generation betrifft Standort/Zeitzone und
Wetterkonfiguration, nicht jeden Medien-/Lautstärkesave. Koordinaten werden auf
vier Dezimalstellen abgeschnitten. Persistenz und POSIX-TZ setzt der Aufrufer;
Tagesgrenzen werden mit `localtime_r` / `mktime(tm_isdst=-1)` berechnet.

`pw_weather_get_snapshot()` kopiert atomar 6.216 Byte; Snapshot auf Heap/statischem
Speicher halten, nicht auf einem kleinen UI-Taskstack. Gültigkeitsbits immer
prüfen, auch bei numerischem Nullwert. `pw_weather_avatar_resolve()` erzeugt
Assetindex 0–51 sowie deutsche Szene-/Outfitlabels. Braunes/blondes Haar wirkt
nur auf das Bild. Ohne qualifizierten Vierstundenkontext erscheint der neutrale
Avatar. Vor OTA `pw_weather_set_suspended(true)`, danach `false` setzen.

## Umgesetzte Grenzen

- Ein Worker mit niedriger Priorität, eigener 8-KiB-Stack; keine UI-/LVGL-Aufrufe.
- HTTPS mit ESP-Zertifikatsbundle und identifizierendem Projekt-User-Agent;
  kein privater Schlüssel oder Konto erforderlich.
- 12 Sekunden pro Netzwerkoperation, 35 Sekunden Gesamtziel; Abbruch wird vor
  jedem weiteren Read geprüft. Eine bereits blockierende Operation kann bis zu
  ihrem Timeout benötigen. Keine harte Echtzeitzusage für Abbruch während TLS.
- Höchstens drei Folge-Redirects; nur HTTPS auf `api.met.no`, kein Downgrade oder
  beliebiger fremder Host. Ein offizieller Hostwechsel benötigt Neuqualifikation.
- gzip und zlib-deflate; gzip Header-/Daten-CRC sowie ISIZE geprüft. Keine
  Multi-Member-Archive, keine still akzeptierten Restdaten.
- Maximal 128 KiB empfangener und 128 KiB entpackter Inhalt. Die beiden temporären
  Puffer und der Ergebnissnapshot liegen ausdrücklich in PSRAM. Empfang in
  2-KiB-Blöcken, kein unbeschränktes Realloc. Kleine cJSON-Objekte nacheinander
  statt eines kompletten Forecast-DOM; max. 384 Einträge / 8 KiB pro Eintrag,
  Verschachtelungstiefe 24. Speicher-/Formatfehler erhalten den alten Snapshot.
- `Expires`, identisches `If-Modified-Since`, zeitlich verteilte Starts/Abfragen,
  Mindestabstand und Fehler-Backoff. 429/403 mindestens eine Stunde Pause;
  `Retry-After` und Fehler-Expires werden berücksichtigt. „Aktualisieren“ umgeht
  keine Sperrfrist. Eine passende 304 bestätigt die Quellenprüfung und verändert
  weder `updated_at` noch vorhandene Prognosezeitpunkte.
- Standort-/Konfigurationswechsel oder OTA-Sperre entwerten laufende Jobs;
  veraltete Jobs dürfen weder Snapshot noch Cacheheader übernehmen.
- Initialzustand `unconfigured`; WLAN-/Uhr-/Fetch-/Stale-/Fehlerstatus getrennt.
  Modellversionen aus der Zukunft (>5 Minuten) oder älter als 48 Stunden werden
  abgelehnt. Sichtbare Veraltetgrenze: Quellenprüfung >2 Stunden, Providerupdate
  >24 Stunden oder kein passender aktueller Prognosepunkt. Die Grenzen sind
  konservative lokale Produktregeln, keine MET-Verfügbarkeitsgarantie.

## Zeit und Darstellung

UTC intern. `source_updated_utc` ist METs Aktualisierungsmetadatum, nicht ein
behaupteter numerischer Modelllauf. Stundenfelder beziehen Niederschlag und Symbol
nur aus `next_1_hours` auf `[time, time+3600)`. Ein fehlendes Ein-Stundenintervall
bleibt ohne Endzeit und ohne erfundenen Regenwert. 6-Stundenblöcke dürfen in die
Tagesaggregation eingehen, aber niemals als sechs gleiche Stunden erscheinen.

Tages-Min/Max sind Extrema vorhandener Prognosepunkte/gelieferter Intervalle,
keine garantierten gemessenen Tagesextrema. Niederschlag summiert nur
nicht überlappende, vollständig im lokalen Tag liegende Intervalle. Eine fehlende
Nacht, ein Intervall über Mitternacht oder eine Lücke macht die Tagesmenge
unvollständig; `precipitation_complete` muss die Anzeige berücksichtigen.
Sommerzeit-Tage haben tatsächlich 23 oder 25 Stunden.

Avatarfenster sind genau vier lückenlose Stunden ab der ersten vollen Stunde
nach erfolgreicher Quellenprüfung. Das Fenster bleibt an dieser Prüfung
verankert, statt sich beim Rendern zu verschieben. 20 Minuten TTL; fehlende
Temperatur/Condition oder Intervalllücken ergeben neutral. Wind und
Regenwahrscheinlichkeit dürfen teilweise fehlen; das Ergebnis bleibt als
`partial` markiert. Keine CURRENT-Sensorfallbackwerte aus Modellprognosen.

Die ursprünglichen reinen Outfitregeln einschließlich sechs Temperaturbändern,
Regen/Schnee/Wind, Sonnenhut und Haarwahl wurden mit MIT-Lizenz erhalten.
Ihr Windmaß ist km/h; der Adapter rechnet MET-m/s vor den Schwellwerten um.
Gewitter-/Hagelhinweise sind keine amtlichen Warnmeldungen. Keine Radar-ETA,
Zugrichtung, Animation oder behauptete Warnquelle wird erzeugt.

## Prüfung und verbleibende Qualifikation

`tests/weather_native/run.sh` kompiliert den tatsächlichen Parser, die
Dekompressionshülle und die tatsächlichen Avatarregeln mit AddressSanitizer und
UndefinedBehaviorSanitizer. 91 Assertions bestanden: echte Berliner Antwort,
Null/Missing/Units, Zeitstempel, doppelte/rückläufige Reihen, Grenzen,
Sommerzeit, partieller Regen, Outfitwechsel, TTL, gzip/deflate/CRC/Abbruch.
Host-Deflate verwendet zlib, Firmware den S3-ROM-tinfl. Damit ist der ROM-Pfad
noch nicht auf Hardware getestet; vollständiger HTTP-Cache-/Reconnect-/TLS- und
Mischlasttest auf dem Knob bleibt erforderlich.

METs Regeln verlangen geringe und gleichmäßig verteilte Last. Die Grenze von
20 Requests/s gilt für die gesamte Anwendung, nicht pro Knob. Direkte größere
Clientflotten und mögliche Proxyanforderungen bleiben vor Erweiterung gesondert
zu klären. Ein eigener Proxy wird durch diese Implementierung weder benötigt
noch als spätere stillschweigende Produktabhängigkeit eingeführt.

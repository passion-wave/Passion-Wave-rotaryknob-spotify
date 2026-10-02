# Wetter-Nativtests

`./tests/weather_native/run.sh` führt die tatsächlichen C-/C++-Module unter
ASan/UBSan aus. Erfordert C/C++17-Compiler, zlib und ESP-IDFs cJSON-Quellen;
`IDF_PATH` kann gesetzt werden. Aktuell 91 Assertions, kein Geräte-/Livekonto-Test.

`met_berlin_20260930.json` wurde am 30. September 2026 aus
[MET Locationforecast complete, Berlin](https://api.met.no/weatherapi/locationforecast/2.0/complete?lat=52.5200&lon=13.4050)
unverändert heruntergeladen. Daten: MET Norway,
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/),
[Quellenlizenz](https://api.met.no/doc/License). Öffentliche Referenzkoordinaten,
keine Kundendaten. Testzeit ist fixiert; die Fixture ist keine aktuelle Anzeige.
Synthetische Fehler-/Sommerzeit-/Outfitfälle entstehen im Testprogramm.

Hostseitig übernimmt zlib nur den Deflate-Kern, während gzip-Grenzen/Header/CRC
und alle Normalisierungs-/Avatarfunktionen dem Firmwarecode entsprechen.
S3-ROM-Dekompression und HTTPS-Transport benötigen einen separaten Gerätetest.

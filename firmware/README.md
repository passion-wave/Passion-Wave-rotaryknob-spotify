# Firmware-Verträge

Dieser Ordner enthält ausschließlich den C++17-Schnittstellenvertrag [pw_framework.hpp](include/pw_framework.hpp). Es gibt noch keine implementierten Treiber, Spotify-Anbindung, verschlüsselte Speicherung, OTA-Übertragung, Buildkonfiguration oder flashbare Firmware. Der Header ist neu erstellt; bestehender RotaryKnob-Code wurde noch nicht kopiert. Die [Übernahmekarte](../docs/06-UPSTREAM.md) benennt Herkunft und Portierungsaufwand.

## Zielaufteilung

Der ESP32-S3 besitzt WLAN, Website, Produktzustand, Zugangsdaten, Provider, Display, Touch und EC1. Der klassische ESP32 übernimmt ausschließlich die benötigten Begleitfunktionen, beispielsweise DAC-Mute, EC2-Diagnose und seinen lokalen Updateempfänger. Im Normalbetrieb gibt es ein WLAN-Gerät und eine IP-Adresse. Die Schnittstellen sagen keine bestimmte Spotify-SDK-Funktion und keine Produktfreigabe zu.

Der S3 bleibt für Website und Mediensteuerung erreichbar. Display-Abschaltung und geeignetes WLAN-Energiesparen ersetzen den bisherigen normalen Tiefschlaf; expliziter Tiefschlaf bedeutet, dass diese Dienste offline sind. Eingabe und Rendering dürfen nicht auf TLS, JSON, Audio oder Flash-Schreibvorgänge warten.

## Geplante Struktur nach den Machbarkeitsprüfungen

| Späterer Pfad | Verantwortung |
| --- | --- |
| `firmware/s3/` | Echte ESP-IDF-Anwendung für den Produktcontroller |
| `firmware/companion/` | Echte ESP-IDF-Anwendung für den klassischen ESP32 |
| `firmware/components/board/` | Revisionsabhängiges Display, Touch, PCNT, Haptik und Audio-Routing |
| `firmware/components/product/` | Serialisierte Befehle, Favoritenfreigabe, Generationen, konsistente Zustandsansicht |
| `firmware/components/provider/` | Tatsächlich freigegebener Spotify-Adapter; optional getrenntes Radio-Audio |
| `firmware/components/web/` | Lokaler Assetserver, geschützte API und Sitzungen |
| `firmware/components/security/` | Zugangsdaten, Schlüssel, Vertrauenskette und sichere Löschung |
| `firmware/components/peer/` | Neues Produktprotokoll, UART-Framing und begrenzte Updateübertragung |
| `firmware/components/ota/` | Signaturprüfung, A/B-Slots, Journal, Healthchecks und Wiederherstellung |

Diese Verzeichnisse und eine scheinbar baubare ESP-IDF-Anwendung werden erst angelegt, wenn passende Abhängigkeiten und Partitionen erprobt sind.

## Harte Gates vor einem Firmware-Build

1. Tatsächliche Guition-Platinenrevision, Chip-/Flash-/PSRAM-Ausstattung und Pinbelegung bestätigen. Die Audio-Pins aus dem vergleichbaren Waveshare-Schaltplan sind bis dahin Kandidaten.
2. ESP-IDF-Version, LVGL-Major, Display-/Touch-Treiber und gegebenenfalls zugelassenen Spotify-Adapter gemeinsam auswählen und fixieren. Ein ESP32-Port eines proprietären SDK ist nicht vorausgesetzt.
3. Reale Partitionstabellen für beide Chips festlegen: Bootloader, Partitionstabelle, OTA-Daten, verschlüsselte NVS/Schlüssel, zwei genügend große App-Slots und Web-/Konfigurationsspeicher. Staging und Webassets dürfen Rollback-Slots nicht verdrängen. Reserven anhand realer Linkerberichte nachweisen.
4. Internen Heap, größten freien Block, DMA-Puffer, PSRAM, Task-Stacks, TLS-Spitzen und Flash-Schreibpausen messen; Worst Case mit Websitzung, Medienstatus, Cover und Eingabe prüfen. PSRAM ersetzt nicht sämtliche internen RAM-Anforderungen.
5. Neuer UART-OTA-Empfänger auf dem Begleitprozessor, signierte Rollenprüfung, Stromausfall-Wiederaufnahme, Bootbestätigung und N/N+1-Kompatibilität nachweisen. Ein getrenntes A/B je Chip ist keine sofortige gemeinsame Rückrollfunktion.
6. USB-C-Recovery für beide Prozessoren dokumentieren und testen. Ein S3-gesteuerter Hardware-Reset/Bootloaderzugriff auf den Begleitprozessor ist im geprüften Schaltplan nicht nachgewiesen; ein nicht startender Begleitprozessor kann physischen USB-Zugriff brauchen.

Es werden in diesem Framework keine eFuses gesetzt, Geräte geflasht oder realen Zugangsdaten benötigt.

## Schnittstellen prüfen

Vom Repository-Wurzelverzeichnis mit einem vorhandenen C++-Compiler über eine minimale Übersetzungseinheit prüfen:

```sh
printf '#include "pw_framework.hpp"\n' | c++ -std=c++17 -Wall -Wextra -Werror -pedantic -Ifirmware/include -x c++ -fsyntax-only -
```

Dieser Check beweist nur die C++17-Syntax und die Abhängigkeit von Standardheadern. Er beweist weder einen Geräte-Build noch Funktionsfähigkeit, Speichersicherheit der späteren Implementierung, Verschlüsselung oder Spotify-Zulassung. C++-Strukturen dürfen nicht direkt als Binärdaten gespeichert oder über UART kopiert werden; der spätere Codec serialisiert Felder ausdrücklich.

Die Provider-Schnittstelle meldet Annahme und Bestätigung getrennt. Jeder Adapter muss unbekannte/unerlaubte Fähigkeiten ablehnen, alte Generationen verwerfen und den zugelassenen Kontoumfang einhalten. Die abstrakten Speicher- und OTA-Klassen sind Verpflichtungen für die Implementierung, keine vorhandenen Sicherheitsfunktionen.

Die Rollen werden ausdrücklich übersetzt: Wire-/Manifestwert `controller_s3` entspricht C++ `s3_controller` und Python `s3`; `companion_esp32` entspricht C++ `esp32_companion` und Python `companion`. Enum-Zahlen werden nicht direkt auf den Draht kopiert. Inhalts- und Ausgangstypen folgen dem gemeinsamen Vertrag; `partner_local_spotify` ist ausschließlich reserviert. Kopfhörertauglichkeit bleibt eine separat nachzuweisende Hardwarefähigkeit.

Protokoll- und Konfigurationskompatibilität verwenden wie das Manifest `emitted_protocol` sowie geschlossene Min-/Max-Bereiche. Adapter dürfen nur geordnete, positive, kleine Bereiche innerhalb ihres unterstützten Versionsuniversums ins Python-Referenzmodell übertragen; keine ungeprüfte Expansion beliebiger Werte. Leere IDs, Nullprotokolle, unbekannte Rollen und nicht belegte Fähigkeiten sind ungültig. Release-ID, Imagehash und frische Bootidentität binden Update- und Healthnachweise an die tatsächlich erwarteten Artefakte.

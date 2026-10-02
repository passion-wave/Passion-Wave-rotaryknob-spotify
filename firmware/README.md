# Native Firmware

Dieser Ordner enthält zwei baubare ESP-IDF-Anwendungen und ihre Implementierungen
für Board, Bedienung, Gerätewebsite, Speicherung, Wetter und Updateprüfung.
Der direkte Spotify-Provider ist ausschließlich im ausdrücklich gewählten
S3-Laborprofil aktiviert; das Standardprofil hält Spotify deaktiviert.
Buildbefehle, Toolchain, tatsächlich ausgeführte Prüfungen und Gerätebefunde stehen
in [Native Implementierung und Geräteabnahme](../docs/16-NATIVE-IMPLEMENTIERUNG.md).
Beide Images beziehen ihre Produktversion aus der gemeinsamen Datei `VERSION`.

## Struktur und Zuständigkeiten

| Pfad | Verantwortung |
| --- | --- |
| `s3/` | ESP-IDF-Anwendung für den S3: WLAN, Produktzustand, Website, Provider und Bedienung |
| `companion/` | ESP-IDF-Anwendung für den klassischen ESP32 mit UART-Protokoll und Updateempfänger |
| `components/pw_board/`, `components/pw_ui/` | Boardtreiber und LVGL-Oberfläche |
| `components/pw_app/`, `components/pw_storage/` | Gerätezustand, geschützte HTTP-API und persistente Einstellungen |
| `components/pw_spotify/`, `components/pw_setup_usb/` | Spotify-Laborprovider und lokales USB-PKCE-Setup |
| `components/pw_weather/`, `components/pw_assets/` | Wettermodell/-abruf, Bilder und Avatar |
| `components/pw_protocol/` | Begrenztes UART-Framing und Nachrichtenprüfung |
| `components/pw_update_verify/`, `components/pw_update_service/`, `components/pw_companion_update/` | Signatur-/Kompatibilitätsprüfung, Staging, Journal und Companion-Empfang |
| `web/` | In das S3-Image eingebettete Gerätewebsite |
| `include/` | Plattformunabhängiger C++17-Schnittstellenvertrag |

Der S3 ist der einzige WLAN-Teilnehmer des Produkts. Displayruhe lässt Website
und Steuerung weiterlaufen; Tiefschlaf würde diese Dienste unterbrechen.
Die Companion-Anwendung ist gebaut, aber auf dem angeschlossenen Begleitprozessor
läuft weiterhin die Originalfirmware. Gemeinsamer Gerätebetrieb ist noch zu prüfen.

## Implementierung, Verträge und Portquellen

[pw_framework.hpp](include/pw_framework.hpp) beschreibt abstrakte Schnittstellen.
Seine Syntaxprüfung belegt keine Gerätefunktion. Ausführbarer Firmwarecode liegt
in den ESP-IDF-Projekten und `components/`; die aktuellen Laborendpunkte erfüllen
noch nicht den gesamten geplanten Vertrag aus [`contracts/`](../contracts/).
Die Entwurfsvorschau unter [`../web/`](../web/) verwendet weiterhin Beispieldaten
und ist von der echten Gerätewebsite in `firmware/web/` getrennt.

Übernommene Quellen, Herkunft und Portumfang dokumentieren die
[Übernahmekarte](../docs/06-UPSTREAM.md) und
[Featuretabellen](../docs/13-FEATURE-PORTIERUNG.md). Lizenzhinweise bleiben erhalten;
die HA-/ESPHome-Laufzeit wird nicht als Produktabhängigkeit übernommen.

## Offene Geräte- und Produktabnahmen

Erfolgreiche Builds und Hosttests ersetzen keine physische Abnahme. Schneller
Seitenaufbau und Heim-WLAN-Einrichtung wurden am iPhone bestätigt. Die erste echte
Spotify-Anmeldung auf dev.8 wurde vom S3 für den konkreten Anmeldeversuch und vom
Nutzer bestätigt; TLS und Tokenabschluss sind damit belegt. Daraus folgt keine
vollständige Feature- oder Produktfreigabe. Offen bleiben insbesondere:

- Tokenpersistenz nach Neustart, Kontowechsel, hörbare Connect-Ausgabe auf
  Roam/Move, mobiles Kunden-Onboarding und kommerzielle Controller-Freigabe.
- Sicherer, jederzeit schreibender Heimnetz-Zugriff (G2); die aktuelle
  Heimnetz-Website bleibt lesend. Einrichtung erfolgt im geschützten Geräte-AP.
- Reale Bedien-, Wetter-, Speicher-, TLS- und Mischlastmessungen sowie Dauerlauf.
- UART-Betrieb beider Chips, provisioniertes Updatevertrauen, vollständige
  Pair-OTA-Aktivierung und Stromausfall-/Rollback-/Recoverytests. Staging allein
  ist kein ausgeführtes Update; die Aktivierung bleibt gesperrt.
- Fertigungsschutz: NVS ist verschlüsselt, der Laborschlüssel aber aus dem Flash
  auslesbar. Secure Boot und Flashverschlüsselung sind nicht aktiviert.
- Radar gemäß späterer Lieferstufe sowie optionale Sonos-LAN-, Radioausgabe-
  und Klinkenfunktionen; Kopfhörertauglichkeit ist nicht nachgewiesen.

Vor jedem erstmaligen Flash eines Chips sind Identifikation und verifiziertes
Originalbackup erforderlich. Recovery-Daten bleiben privat; keine eFuses auf dem
Recovery-Gerät ändern. Verbindliche Abnahmen und Lieferstufen stehen in
[Geräteabnahme](../docs/16-NATIVE-IMPLEMENTIERUNG.md),
[Produktentscheidungen](../docs/15-OFFENE-PRODUKTENTSCHEIDUNGEN.md) und
[Implementierungsplan](../docs/08-IMPLEMENTIERUNGSPLAN.md).

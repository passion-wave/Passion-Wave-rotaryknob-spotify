# Implementierungsverträge

Die Dateien unter `contracts/` sind ein Entwurf für die spätere Firmware. Sie erzeugen noch keinen Server. C++-Interfaces und Python-Referenzmodelle dienen unterschiedlichen Ebenen; keine rohe C++-Struktur darf als UART-Paket oder NVS-Format geschrieben werden.

## Datenverträge

- `config.schema.json`: ausschließlich exportierbare Einstellungen und freigegebener Katalog. `revision` erhöht sich atomar, maximale 64 Spotify-Favoriten und 64 Radiosender. IDs innerhalb jeder Liste eindeutig; `spotify_uri` muss zum angegebenen Typ passen. Verknüpfte Konten, WLAN-Kennwörter, Provider-Tokens, Trustanker und Produktgenehmigungen gehören nicht hinein.
- `ota-manifest.schema.json`: signierter Releaseinhalt für genau zwei Rollen. JSON-Schema prüft Struktur; kryptografische und semantische Prüfungen sind zusätzlich verpflichtend. Beide Images müssen dieselbe Version und Release-ID wie das Manifest tragen, genau eine Rolle je Typ, bekannte echte Hardware. `example_only=true` ist immer nicht installierbar.
- `openapi.json`: geplanter HTTP-Vertrag für lokale Konfiguration, Radio-Suche/-Probe, Jobs und OTA. Authentifizierter geschützter Transport, Origin-/CSRF-Prüfungen gelten zusätzlich zum angegebenen Session-Cookie. Der lokale Vorschau-Server implementiert diese API nicht.

GET gibt eine Konfigurationsrevision als ETag zurück; PUT trägt `If-Match`, bei Konflikt 409. Gleichzeitige Browser überschreiben sich nicht. Konfiguration atomar validieren/speichern/aktivieren, keine halben Katalogänderungen. HTTP 202 bedeutet Job angenommen, nicht „Wiedergabe läuft“ oder „Update erfolgreich“.

`/playback/commands` wird diskriminiert nach `action` validiert: `play_favorite` verlangt freigegebene `favorite_id`; `set_volume` einen begrenzten Prozentwert; `seek` eine zulässige Position; Shuffle einen 0/1-Wert; Repeat 0=aus/1=Kontext/2=ein Titel. Unpassende Felder ablehnen. Die generische API-Objektstruktur ersetzt diese domänenspezifische Validierung nicht. `command_id` dedupliziert Wiederholungen, neuere Generationswünsche können ältere überholen. Produktprovider und Ausgabe prüfen Fähigkeiten erneut.

Einträge sind Referenzen, keine Spotify-Audiodownloads. IDs/Spotify-URLs aus Browsern normalisieren, erlaubte Hosts und Typen prüfen; nur erlaubte URIs an Provider weitergeben. Im Gerät Playlists, Shows und Episoden getrennt behandeln. Radioquelle nie in Spotify-Kontext umdeuten. Sender-Metadaten/URIs sind untrusted input.

## Gemeinsame Begriffe

| Bedeutung | JSON / HTTP | C++ | Python-Referenz |
| --- | --- | --- | --- |
| Hauptprozessor | `controller_s3` | `s3_controller` | `Role.S3` / `s3` |
| Begleitprozessor | `companion_esp32` | `esp32_companion` | `Role.COMPANION` / `companion` |
| Zielversion | `version`, `release_id` | Descriptor/Releasezustand | `product_version`, `release_id` |
| Signierte Manifestidentität | aus exakt verifizierten Bytes | `manifest_digest` | `signed_manifest_sha256` |
| Aktive Protokollversion | `emitted_protocol` | `emitted_protocol` | `protocol` |
| Akzeptierte Peer-Versionen | inklusive min/max | inklusive min/max | explizites Tuple |
| Lesbares Settingsschema | inklusive min/max | inklusive min/max | explizites Tuple |

Adapter expandieren nur kleine validierte Intervalle in unterstützte Versionen; kein unbeschränktes Array aus untrusted Zahlen erzeugen. Im ersten Produktprofil maximal acht unterschiedliche akzeptierte Versionen/Schemata. Eine Rollenkennung wird explizit konvertiert und nie allein anhand der Reihenfolge im Manifest gewählt.

`partner_local_spotify` ist eine reservierte Ausgabekategorie für eine spätere genehmigte Empfängerimplementierung. Das derzeitige Python-Modell unterstützt sie absichtlich nicht und lehnt unbekannte Ziele ab. Es bescheinigt dadurch keine lokale Spotify-Funktion. Kopfhörereignung ist eine eigene Hardwarefähigkeit, kein automatisch freigegebener Ausgang allein wegen des Steckers.

## C++-Grenzen

`firmware/include/pw_framework.hpp`:

- `MediaProvider` führt keine synchronen Internetaufrufe im UI aus; Events enthalten IDs/Generationen.
- `CredentialStore` liefert Secrets kurzzeitig nur an berechtigte Adapter, niemals an öffentlichen Konfigurationsexport.
- `OtaBackend` verifiziert Artefakte, kontrolliert Reihenfolge und persistiert Journal vor Seiteneffekten.
- `UartPeer` kapselt Prozessorzustand und Transport; ein ACK ist keine Image-Signatur oder Bootgesundheit.
- `PeerHealth` bindet Transaktion, Rolle, laufenden Imagehash und frische Bootsitzung; alte Meldungen sind unzulässig.

Alle Implementierungen fehlen noch. Ein bestandener Header-Syntaxcheck beweist keine Hardware-/SDKkompatibilität. Strings/Puffer sind begrenzt und vor Nutzung validiert; kein NUL-Abschluss vorausgesetzt.

## UART-Protokoll PW-S, erste Version

Bisherige COBS/CRC16- und Prioritätskonzepte wiederverwenden, aber neue Produktkennung und Versionsbereich. Frame: Produktmagic, Wire-Version, Senderrolle, Typ, Session, Sequence, Payload-Länge, Payload, CRC16; kleines festes Bytebudget (Startwert 192 Byte Payload). Exakte Feldbreiten/Endianness und Typnummern werden in P3 einmal festgeschrieben und mit Golden-Vektoren getestet.

Mindestens `HELLO`, `PEER_HEALTH`, `CONFIG_CAPABILITIES`, `MUTE_SET`, `INPUT_DIAGNOSTICS`, `OTA_BEGIN`, `OTA_CHUNK`, `OTA_ACK`, `OTA_VERIFY`, `OTA_ACTIVATE`, `OTA_BOOT_REPORT`, `OTA_ABORT`. Keine HAAction-/Entity-IDs und kein allgemeiner Remote-Code-Befehl. Audio-PCM läuft nicht über diesen Kanal.

OTA-Chunk enthält Offset/Transfer-ID/Länge; Begleitprozessor setzt eigene Bounds und akzeptiert nur laufende Transaktion, nicht beliebige Schreibadressen. Duplikate müssen idempotent sein, Out-of-order entweder begrenzt zurückweisen oder definiert behandeln. Signaturverifikation erfolgt vor Slotaktivierung auf jedem MCU. Handshake weist falsches Produkt/Role früh ab. Physische Recovery bleibt separat.

## Referenzmodell und Prüfwerkzeug

`tools/reference_model.py` enthält abstrahierte Capability-Entscheidung und OTA-Automat. Es erhält **bereits verifizierte** Evidenz aus Testfixtures, kann aber diese Evidenz nicht herstellen. Produktzulassung/Signatur/Bootgesundheit dürfen niemals aus Benutzer-JSON befüllt werden. `snapshot()` modelliert einen Checkpoint im Speicher; der persistente sichere Journaladapter fehlt.

`tools/check.py` nutzt einen bewusst begrenzten Strukturprüfer für die im Repo verwendeten JSON-Schema-Schlüsselwörter und zusätzliche semantische Ablehnungsfälle. Es ist kein allgemein vollständiger JSON-Schema-/OpenAPI-Validator. Vor Codegenerierung/Produktimplementierung einen standardkonformen Validator und HTTP-Vertragstests mit echten Antworten ergänzen; dies ist P3/P6, kein heute erfülltes Gate.

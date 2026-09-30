# Echtes OTA-Signatur- und Imagegate

`pw_update_verify.h` implementiert ECDSA-P256/SHA256 mit ESP-IDF 5.4.3s mbedTLS,
keine simulierte `signature_valid`-Eingabe. Trusted Public Key, Schlüssel-ID und
beide Hardware-/Bootloader-/Protokoll-/Security-/Slotnachweise kommen aus
vertrauenswürdiger Firmware/Provisionierung. Der Upload darf sie nicht ersetzen.
Ohne solche Nachweise darf der Aufrufer keine OTA-Aktivierung freigeben.

## Manifestbytes und Signatur

Bestehendes Schema `contracts/ota-manifest.schema.json`, Encoding
`detached-over-canonical-manifest-v1`: ASCII-JSON, alle Objektkeys rekursiv streng
lexikographisch sortiert, keine Whitespace außerhalb Strings, nur die im Schema
vorgesehenen ganzzahligen Zahlen. Strings verwenden die minimalen JSON-Escapes;
für dieses eingeschränkte ASCII-Profil entspricht der Producer
`json.dumps(sort_keys=True, separators=(",", ":"), ensure_ascii=True)`.
Dies ist **kein allgemeiner RFC-8785-Canonicalizer**. Unicode-/Float-/Metadaten-
Erweiterungen benötigen einen neuen Vertrag. Manifest maximal 16 KiB, Tiefe 8.

Detached-Signatur: ASN.1-DER ECDSA P-256 über SHA256 der **exakt gelieferten**
Manifestbytes, keine nachträgliche Normalisierung vor der Prüfung. Erst nach
Kryptoprüfung wird geparst; anschließend muss Re-Serialisierung exakt dieselben
Bytes liefern. Doppelte/unerwartete Keys, nichtkanonische Zahlen, unbekannte
Rollen, Examplemanifest und Rollen-/Versions-/Release-Mix werden abgelehnt.

Beide Images müssen zur tatsächlichen Board-ID und Slotgröße passen. Verifiziert
werden Sicherheitsuntergrenze, Mindestbootloader, optionales Beta, SemVer-
Rückstufung, aktuelles und Zielschema sowie die neue Paarung und die notwendige
Zwischenpaarungen alter S3 + neuer Companion und neuer S3 + alter Companion
(für einen automatischen unbestätigten Companion-Rückfall). Intervallbreite maximal acht Werte.
OTA-URLs sind ASCII-HTTPS-URLs ohne Credentials/Fragment; DNS-Hostnamen und Ports
werden begrenzt geprüft, IPv6-Literale sind in diesem ersten Profil nicht erlaubt.

## Bindung der tatsächlichen Images

Nur ein erfolgreich verifiziertes opakes Manifest kann einen Imageprüfer öffnen.
`image_feed` nimmt geordnete Bytes, begrenzt Größe und hasht den tatsächlichen
Inhalt. `image_finish` prüft Länge, Manifesthashbindung und den ESP-Appheader:
Chip 0/9, Descriptor-Magic, eingebettete Version, rollenspezifischer Projektname
und `secure_version`. Release-ID wird durch signiertes Manifest und beide Hashes
gebunden; es wird kein nicht existentes eingebettetes Release-ID-Feld behauptet.

Die Hashbindung an das signierte Manifest authentifiziert Images auch ohne
Secure-Boot-eFuses. Sie ersetzt **nicht** IDFs vollständige Imageprüfung,
Bootloader-Signaturprüfung, lokalen Peer-Verifier, Journal, A/B-Transaktion,
Powercut-Abnahme oder vertrauenswürdige Erstprovisionierung. Das Modul schreibt
weder Flash noch Bootslot oder eFuse und macht keinen Netzwerkaufruf.

## Uploadbundle und Producer

`tools/build_update_bundle.py` erzeugt ein unveränderliches Uploadpaket:

| Offset | Inhalt |
| --- | --- |
| 0 | 8 Byte ASCII `PWOTA1\r\n` |
| 8 | Manifestlänge, uint32 little-endian |
| 12 | DER-Signaturlänge, uint16 little-endian |
| 14 | exakt Manifestbytes, danach DER-Signatur |
| anschließend | Companion-Image, dann S3-Image, jeweils signierte Manifestlänge |

Parser müssen alle Längen vor Allokation prüfen und zusätzliche Bytes ablehnen.
Erst Manifest+Signatur+Policy prüfen, dann Images begrenzt in inaktive Staging-
Slots nehmen und ihre tatsächlichen Bytes separat auf beiden MCUs prüfen.
Eine Aktivierung vor erfolgreicher vollständiger Vorprüfung ist unzulässig.

Producer benötigt Python `cryptography`, existierende private P-256-PEM-Datei,
Manifest und beide Images. Er prüft Repo-Schema/Identitäten/Appdescriptor und
berechnet echte Hashes. Er veröffentlicht nichts, überschreibt keine bestehenden
Pakete und enthält/erzeugt keine Produktionsschlüssel. Eine Signatur ist kein
Gerätefreigabenachweis; Policyprüfung bleibt auf jedem MCU erforderlich.

## Nachweise

`python3 tests/update_native/run.py`: **51** signierte Native-Fälle plus Bundle-
Framing/Unveränderlichkeit bestanden. Tatsächlicher C-Verifier und IDF-mbedTLS,
ASan/UBSan für Prüfer/cJSON/Testtreiber. mbedTLS-Hostbibliothek selbst ist normal
gebaut. Schlüssel werden frisch erzeugt und danach mit dem temporären Verzeichnis
entfernt. Tests decken Signatur-/Key-/Algorithmusmanipulation, kanonische Bytes,
rollen-/release-/board-/protokoll-/schemafremde signierte Inhalte, Rollback,
Bootloader und geänderte/gekürzte/vertauschte Binärbytes ab. Testimages sind
synthetische Descriptorfixtures, ausdrücklich nicht bootfähig.

Zusätzlich wurde der Verifier mit dem tatsächlichen S3-ESP-IDF-5.4.3-Compiler und
Projektkonfiguration fehlerfrei kompiliert. Live-OTA/Powercut/Hardwarekryptografie
sind damit noch nicht bestätigt. Physische Prüfung und Provisionierung bleiben
Aufgaben des Koordinators.

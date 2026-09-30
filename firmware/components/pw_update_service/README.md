# Signiertes Pair-Staging auf dem S3

Dieser Stand implementiert das echte Einlesen eines `.pwota`-Bundles, die
ECDSA-P256-/Policyprüfung, begrenztes Flash-Staging beider Images und den
UART-Transfer zum unabhängig prüfenden ESP32. Er installiert **noch kein
Produktupdate**: Es gibt keinen S3-Aktivierungs-, Commit- oder Rollbackpfad.
`activation_enabled` und `capabilities.pair_ota` bleiben immer `false`.

Der schwache Provisionierungshook gibt standardmäßig `NULL` zurück. Dann sind
auch Upload und Flashschreiben gesperrt. Ein späterer, lokal qualifizierter
Provisionierungsstand muss Public Key, Schlüssel-ID und beide Gerätepolicies
bereitstellen; HTTP-Body, Manifest und UART-Bootreport dürfen diese Daten nicht
ersetzen. Es werden keine eFuses, Schlüssel oder Geräteeinstellungen provisioniert.

## Tatsächlicher Ablauf

1. Geschützte AP-Sitzung, Host/Origin, CSRF, Konfigurationsrevision, Dateityp,
   begrenzte Content-Length und bestätigter USB-Netzbetrieb prüfen. Die Bestätigung
   ist eine Laborbedingung, kein erfundener Akku- oder Stromsensor.
2. Ein eigener Worker übernimmt den asynchronen HTTP-Request. Der normale
   HTTP-Server und LVGL warten nicht auf Upload/UART. Wetterabrufe pausieren.
   RAM: höchstens 16 KiB Manifest plus 72 Byte DER, begrenzte 4-KiB-Puffer und
   Verifierzustand; keine vollständigen Images im RAM.
3. Framing, Signatur, Rollen, Versionen, Produkt-/Hardware-/Bootloader-/Schema-/
   Protokoll-/Securitypolicy und exakte Gesamtlänge prüfen, bevor ein Slot
   gelöscht wird. Das Companion-Image landet in `pair_stage`; das S3-Image im
   inaktiven OTA-Slot. Beide werden gestreamt gehasht und anschließend unabhängig
   aus dem geschriebenen Flash erneut gehasht/gegen den ESP-Appdescriptor geprüft.
4. `esp_ota_end` prüft das vollständige S3-ESP-Image. Ein S3 kann den fremden
   ESP32-Chipheader nicht mit seinem lokalen IDF-Imageverifier qualifizieren;
   die vollständige ESP32-Prüfung erfolgt deshalb auf dem Companion nach Transfer.
5. HTTP antwortet nach lokaler Prüfung mit `202`. Der Gerätejob läuft unabhängig
   weiter. Der alleinige existierende UART-Task übergibt Frames an den Sender;
   kein zweiter Task liest UART. Stop/wait prüft Transaktion, Anfrageart,
   Sequenz, Bootnonce, Offset, Phase, Manifestdigest und Imagegröße. Wiederholung
   verwendet identische Sequenz und Bytes. Ein Fehler versucht ausschließlich
   einen vor Aktivierung zulässigen ABORT, keinen Bootslotwechsel.
6. Erst der passende `IMAGE_VERIFY`-ACK führt zu `prepared`. Die Website sagt
   ausdrücklich „noch nicht installiert“. Beide bisher aktiven Images bleiben
   ausgewählt. UART-Transfer ist auf 15 Minuten begrenzt.

## Persistenz und Neustart

Das Journal liegt als einzelner Blob in der separat verschlüsselt initialisierten
NVS-Partition `journal`, Namespace `pw_ota`. `pw_storage_init()` muss vorher
erfolgreich sein. Das Modul ruft kein `nvs_flash_init()` auf. Fester Codec,
Schema-Magic, Generation und CRC erkennen inkonsistente Daten; CRC ist kein
Authentizitätsnachweis. Typ/Existenz werden vor dem Blob-Lesen geprüft, Fehler
werden nicht als fehlendes Journal interpretiert.

Jeder Flashübergang folgt einem erfolgreichen Journalwrite. Nach Neustart gilt
ein vorhandener Stagingvorgang als `recovery`; Flashbytes erhalten keine neue
Prüfgültigkeit aus alten Prozentwerten. Ein neuer autorisierter Upload kann erst
mit bereitem Companion von vorne beginnen. Ein bereits vollständig vorbereitetes
Companion-Image hat derzeit keinen Benutzerpfad zum Verwerfen/Ersetzen; dieser
gehört zur noch fehlenden Produktkoordination.

Der Boot-Finalisierer muss `pw_update_service_may_finalize_boot()` beachten.
Ein aktiver, vorhandener, beschädigter oder unlesbarer Journalzustand sowie fehlendes Storage
darf nicht pauschal durch `esp_ota_mark_app_valid_cancel_rollback()` übergangen
werden. Ein aufgezeichneter Healthwert aktiviert in diesem Stand nichts.

## API und Nachweise

- `GET /api/v1/updates`: tatsächlicher Zustand, Bytes, Zielversion, Sperrgrund.
- `POST /api/v1/updates/upload`: rohes `application/octet-stream`, kein Multipart;
  Header `X-CSRF-Token`, `If-Match`, `X-PW-USB-Power: confirmed`.
- Es gibt keinen Endpunkt zum Hochladen von Trustkeys oder Aktivierungsfreigaben.

`python3 tests/update_service_native/run.py` prüft den realen C-Stagingkern mit
temporären Signaturschlüsseln und ASan/UBSan. Die Flash-/NVS-Callbacks sowie
ESP-Endprüfung sind dort simuliert, die Appimages reine Descriptorfixtures.
Abgedeckt sind Fragmentierung, Signatur/Framing vor Erase, Readback-Manipulation,
NVS-/Write-/ESP-Fehler, falsche/abgeschnittene/zusätzliche Bytes und Neustartjournal.
Der ESP-IDF-Wrapper, HTTP/UART unter Last, reale USB-Stromausfälle, Peer-Recovery,
vollständiger Pair-Commit/Rollback und Produktprovisionierung brauchen eigene
physische Nachweise. Native Tests beweisen diese Punkte nicht.

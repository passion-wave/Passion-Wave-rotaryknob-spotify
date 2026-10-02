# Lokale Geräte-API und Konfigurationspersistenz

Schreibzugriff benötigt die zeitlich begrenzte, geschützte Geräte-AP-Sitzung,
korrekte lokale Host-/Originadresse, CSRF und die aktuelle Revision. Der offene
Vertrauensnachweis für dauerhaft schreibbares Heim-LAN wird dadurch nicht ersetzt.

Die vollständige Konfiguration ist ein einzelner NVS-Blob `config_blob` im
verschlüsselten Namespace `settings/pw`, inklusive Schema, Revision und
abschließendem NUL. Grenze: 49.152 Bytes. `nvs_set_blob`/`nvs_get_blob` verwenden
explizite Längen. Der vorherige `config`-String wird nur gelesen, wenn der Blobkey
tatsächlich fehlt. Ein erfolgreicher späterer Save schreibt den neuen Blobkey;
der alte String wird nicht vorsorglich gelöscht. Ein vorhandener beschädigter
Blob fällt niemals auf diesen alten Stand zurück.

Nur wenn beide Keys nach `nvs_find_key` fehlen, entstehen Erstdefaults. Typfehler,
Lesefehler, ungültige Länge, JSON-/Schemafehler und Parse-OOM führen zum Initfehler,
ohne Daten zu überschreiben. WLAN verwendet weiterhin einen kleinen String;
auch dort ist nur tatsächliches Fehlen ein Erstsetup, kein Read-/Parsefehler.

Der API-Wechsel ist durch die tatsächlichen ESP-IDF-5.4.3-Quellen begründet:
`components/nvs_flash/include/nvs.h` begrenzt `nvs_set_str` auf 4.000 Bytes samt
NUL; `nvs_set_blob` erlaubt mehrseitige Werte bis zur dokumentierten
Partitionsgrenze. `src/nvs_api.cpp` reicht die explizite Bloblänge an `set_blob`
weiter; `src/nvs_storage.cpp` schreibt über `writeMultiPageBlob`. Die Typabfrage
ist bewusst separat: `getItemDataSize`/`findItem` können einen typfremden Wert bei
der Suche als nicht gefunden behandeln.

`tests/app_native/run.sh` serialisiert unter anderem tatsächlich 64 Favoriten
zu mehr als 4.000 Bytes, prüft den vollständigen längengebundenen JSON-Roundtrip
und verwirft den fehlenden Terminator. Dies ist ein Parser-/Budgetnachweis,
kein physischer NVS-Powercut-Test. Der Journal-/Pair-Updatepfad ist separat unter
`../pw_update_service/README.md` beschrieben.

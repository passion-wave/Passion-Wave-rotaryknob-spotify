# PWSET1 · USB-Einrichtungsvertrag 1.0.0

Dieser Ordner beschreibt den **vorhandenen Laborvertrag** zwischen lokalem
Desktophelfer und Display-S3. Er ist die kanonische Ausgangsbasis für eine spätere
Auslagerung der Einrichtungs-App. Es entsteht dadurch keine neue Firmwarefunktion,
Produktfreigabe oder verschlüsselte Gerätepaarung.

Die aktuelle Implementierung liegt in
[pw_setup_protocol.h](../../../firmware/components/pw_setup_usb/include/pw_setup_protocol.h),
[pw_setup_protocol.c](../../../firmware/components/pw_setup_usb/pw_setup_protocol.c),
[pw_setup_usb.c](../../../firmware/components/pw_setup_usb/pw_setup_usb.c) und
[helper.py](../../../tools/spotify_setup/helper.py). Die
[Repositoryvorbereitung](../../../docs/17-SETUP-REPOSITORY.md) legt Zuständigkeiten
und die spätere Übernahme fest.

## Dateien und Prüfgrenzen

| Datei | Inhalt |
| --- | --- |
| [request.schema.json](request.schema.json) | JSON Schema 2020-12 für die fünf Requestmethoden; geschlossene Feldmengen und Wertebereiche |
| [response.schema.json](response.schema.json) | Geschlossener Vertrag für vom S3 erzeugte Antwortobjekte, inklusive Spotify-Status und stabiler Fehler |
| [golden.json](golden.json) | Gute/schlechte vollständige Wirezeilen, Antwortbeispiele, Ablauf-/Korrelationsfälle und OAuth-URL-Prüffälle |

Die Schemata validieren den JSON-Inhalt **nach** Prüfung der Zeilenrahmung und
eindeutiger Schlüssel. Ein Schema kann weder doppelte JSON-Schlüssel nach einem
verlustbehafteten Parse erkennen noch Request-ID-Korrelation, Zeitablauf,
Tokenpersistenz oder Gerätebesitz nachweisen. Die Antwortform wird zusätzlich
gegen die Methode des ausstehenden Requests geprüft: ein syntaktisch gültiges
Cancel-ACK ersetzt beispielsweise keine Statusantwort.

`golden.json` bleibt gültiges JSON. Fehlerhafte JSON-Nachrichten stehen als
escapte Strings in `wire_utf8`; niemals als eigenständige kaputte `.json`-Dateien.
Nach JSON-Unescaping enthält ein solcher String die tatsächlich vorgesehenen
Wirezeichen, einschließlich Abschluss-LF, gegebenenfalls CR, NUL oder absichtlich
fehlendem Abschluss. Jeder Code, State, Accountmarker und jede ID ist synthetisch.
Die Null-Client-ID im Fixtureprofil ist kein existierender freigeschalteter
Spotify-Zugang und darf keine Laufzeitprofilprüfung erfüllen.

`tools/check.py` lädt über die normale Testdiscovery nun
[tests/contracts/test_usb_setup.py](../../../tests/contracts/test_usb_setup.py).
Diese Tests prüfen alle 62 guten/schlechten Frame-/Strukturbeispiele mit dem
bestehenden begrenzten Schema-Validator; die positiven und negativen Antworten,
URL-Prüfung, Request-ID-/Methodenkorrelation und Anmeldeabläufe laufen gegen den
tatsächlichen Python-Helfer mit einem reinen Byte-Fixtureport. Dabei wird kein
USB-Gerät geöffnet und keine Spotify-Anfrage gesendet. Die neue positive
Antwortformprüfung im Helfer verhindert insbesondere, dass ein HELLO-Objekt oder
`accepted: false` als Callback-ACK gilt.

Bei vorhandenem Compiler und `IDF_PATH` beziehungsweise `CJSON_DIR` werden außerdem
37 gemeinsame Requestvektoren gegen den echten C-Decoder mit ASan/UBSan ausgeführt.
Ohne diese vorhandene Firmwaretoolchain wird ausschließlich dieser Test sichtbar
übersprungen. Eine explizit gesetzte, aber unbrauchbare `IDF_PATH`-/`CJSON_DIR`-
Umgebung schlägt fehl statt den CI-Nativtest zu überspringen. Der Fall „LF fehlt“ betrifft den Transportbesitzer und wird nicht
als C-Decoderaufruf ausgegeben, weil dieser bereits abgetrennte Zeilen erwartet.
Für eine verpflichtende native CI-Ausführung dient:
`python3 -m unittest discover -s tests/contracts -p 'test_*.py' -v` mit bereitgestelltem
IDF-Pfad. Die Workflow-Anbindung liegt beim Firmware-Releaseprozess.

**Noch offen:** ein vollständiger JSON-Schema-2020-12-Validator, echte USB-
Framing-/Timeouttests sowie Firmware-Antworterzeugung und Spotify-Netzwerkabläufe
gegen diese Goldens. Der vorhandene Schema-Validator unterstützt ausdrücklich nur
das verwendete begrenzte Vokabular samt `oneOf`. Die Tests des Desktop-Ablaufs
simulieren die S3-Antwortbytes und beweisen weder die Tokenablage auf echter
Hardware noch eine Spotify-Freigabe. Die separate Browserprüfung nutzt weiterhin
ihre eigenen Browserfixtures. Das spätere Setup-Repo muss die gemeinsame Suite
ebenfalls verbindlich übernehmen.

## Transport und Framing

- **Native USB Serial/JTAG des S3**, USB-VID/PID **303a:1001**, als serieller
  USB-Port auf dem Computer. Der Wert **115200/8N1** im Desktophelfer dient der
  CDC-/Serial-API-Kompatibilität und ist keine UART0-Baudratenanforderung für diesen
  Transport. Der Helfer setzt DTR und RTS vor Öffnen auf inaktiv und sendet keinen
  Reset-/Flashbefehl. Verhalten des USB-Treibers muss am Gerät geprüft werden.
  UART1 zwischen S3 und Companion bleibt unverändert ein anderer Vertrag.
- Eine Nachricht: `PWSET1 ` + genau ein JSON-Objekt + LF. Prefix ist sieben
  ASCII-Bytes. Der Sender verwendet LF; der Empfänger toleriert ein einzelnes CR
  unmittelbar davor und entfernt es vor der Payloadprüfung.
- Höchstens **8191 Bytes vor LF**, einschließlich Prefix und gegebenenfalls CR.
  Der Puffer hat 8192 Bytes einschließlich abschließendem NUL. Bei CRLF verbleiben
  daher höchstens 8190 Bytes nach Entfernung des CR. Kein eingebettetes NUL, CR
  oder LF im an den Decoder übergebenen Inhalt. JSON-Escapes werden anschließend
  feldbezogen geprüft; ein escaptes NUL ist unzulässig.
- Überlange Zeilen werden bis LF verworfen. Eine unvollständige Zeile wird nach
  fünf Sekunden ohne neue Bytes zurückgesetzt. Fremde Firmwarelogzeilen sind
  keine Protokollantworten und dürfen nicht als Authentisierungsdaten geloggt werden.
- Keine automatische Neuübertragung von `authorize` oder `callback`. IDs dienen
  der Antwortkorrelation; es gibt im USB-Vertrag keinen allgemeinen ACK-Replaycache.
  Ein verbrauchter Callback-State kann nicht ein zweites Mal autorisieren.

Der Requestparser verwirft unbekannte oder doppelte Schlüssel, falsche Typen,
Objektverschachtelung anstelle skalarer Felder und zusätzliche Daten nach dem
JSON-Objekt. Ein nicht dekodierbarer Request erhält **keine** Antwort, die eine
unzuverlässig gelesene ID oder empfangene Daten widerspiegelt. Ein gültiger Request
mit einer nicht erfüllten Laufzeitbedingung erhält hingegen ein korreliertes
Fehlerobjekt. UUIDs, MAC-Adressen oder neue Pairingfelder werden nicht still in
Version 1 aufgenommen.

## Requests und Antworten

Jeder Request hat `id` als positive Ganzzahl **1…2147483647** und `method` als
exakten kleingeschriebenen Methodennamen. ID und Methode allein genügen für alle
Methoden außer `callback`. Sender serialisieren IDs als dezimale Ganzzahlen.
Jede Antwort enthält dieselbe `id` und boolesches `ok`. Die Antwort enthält kein
eigenes `method`; der Helfer ordnet sie seinem ausstehenden Request zu.

| Methode | Zusätzliche Requestfelder | Erfolgsantwort zusätzlich zu `id`, `ok: true` | Physisches Setupfenster |
| --- | --- | --- | --- |
| `hello` | keine | `role`, `hardware`, `version`, `lab_enabled`, `setup_open` | nicht erforderlich |
| `status` | keine | HELLO-Felder plus `spotify` | erforderlich |
| `authorize` | keine | `authorization_url`, `expires_in_seconds: 600` | erforderlich |
| `callback` | `code`, `state` | `accepted: true` | erforderlich bei Annahme |
| `cancel` | keine | keine | erforderlich |

`code` enthält 1…1024 druckbare ASCII-Zeichen ohne Leerzeichen (`0x21…0x7e`).
`state` besteht exakt aus 48 kleingeschriebenen Hexzeichen. Weder eine andere
Redirectadresse noch ein Client-Secret, Access-/Refresh-Token oder PKCE-Verifier
ist ein erlaubtes Requestfeld. Die Desktoptimeouts betragen fünf Sekunden für
HELLO/Status und zehn für die anderen lokalen Vorgänge; Spotify-TLS läuft
asynchron auf dem S3.

Identität: `role` ist `controller_s3`, `hardware` ist
`JC3636K518C_I_YR1`. `version` ist die tatsächliche Appversion aus dem ESP-IDF-
Descriptor, höchstens 31 ASCII-Zeichen. Die beiden Flags sind boolesch. Der
vorhandene Standardbuild ohne `CONFIG_PW_SPOTIFY_LAB` startet keinen USB-Setup-
Task; ein `lab_enabled: false`-HELLO darf daher nicht als heute garantierte Antwort
dieses Builds erwartet werden. Rolle/Modell sind Erkennungshilfen, kein
kryptografischer Identitätsnachweis eines individuellen Geräts.

## Spotify-Status und Abschlussnachweis

| Feld | Bedeutung und Bereich |
| --- | --- |
| `linked` | Tatsächlich vorhandene Kontoverknüpfung auf dem S3; boolesch |
| `state` | `disabled`, `unlinked`, `waiting_network`, `waiting_clock`, `authorizing`, `ready`, `reauth_required`, `rate_limited`, `error`, `suspended`, `disconnecting` |
| `error` | `none`, `storage`, `network`, `auth`, `forbidden`, `no_device`, `rate_limit`, `response`, `memory`, `stale`, `unsupported` |
| `connected` | Netzwerkzustand des S3; **keine** Bestätigung der Spotify-Anmeldung |
| `session` | uint32, 0…4294967295; 0 steht für nicht initialisierten/deaktivierten Zustand. Laufende Provider erzeugen eine nichtnull Sitzung. |
| `http_status` | Letzter übernommener HTTP-Status, 0…599; 0 bedeutet kein Status. Kein alleiniger Loginbeweis. |
| `authorization_id` | Leer oder 48 kleine Hexzeichen: verbrauchter State des zuletzt erfolgreich gespeicherten neuen Anmeldeversuchs; ausschließlich RAM |

Der S3 erzeugt State und Verifier, nimmt den Callback höchstens einmal innerhalb
von 600 Sekunden an und tauscht den Code selbst gegen Tokens. `accepted: true`
besagt nur, dass der asynchrone Austausch angenommen wurde. Erst nach erfolgreicher
Tokenablage setzt der S3 `authorization_id` auf den verbrauchten State. Der Helfer
muss `linked: true` **und** exakte Übereinstimmung mit seinem begonnenen Versuch
beobachten, bevor er diesen Versuch als verknüpft bestätigt. `state: ready` ist
zusätzlich für die Meldung „bereit/verbunden“ nötig. Bei `waiting_network` kann
die Verknüpfung bestätigt, das Gerät aber noch nicht bereit sein.

Ein Sitzungswechsel allein genügt nicht: Nach Neustart kann die alte Verknüpfung
wieder geladen werden und `session` wechseln, während `authorization_id` leer
bleibt. Ein Marker eines früheren oder anderen Versuchs bestätigt den neuen
Versuch ebenfalls nicht. `authorization_id` verlässt den geschützten USB-Status
nicht in Richtung Browserstatus, Diagnoseexport oder Log. Es ist keine neue
Autorisierung und kein wiederverwendbarer Zugangstoken.

Schließen des physischen Fensters verwirft einen noch nicht übergebenen Versuch.
Ein bereits im offenen Fenster angenommener Austausch darf fertiglaufen. Für
die anschließende USB-Statusabfrage ist gegebenenfalls erneut ein langer Druck
nötig. Eine laufende oder noch gültige alte Verknüpfung darf nicht aufgrund eines
neuen fehlgeschlagenen Versuchs als dessen Erfolg ausgegeben werden.

## OAuth-Profilbindung und Fehler

Für URL-Prüfung gelten das öffentliche qualifizierte Releaseprofil und dessen
fixe Client-ID, `https://accounts.spotify.com/authorize`, Code-Flow, PKCE S256,
43-Zeichen-Base64url-Challenge, feste Rückleitung
`http://127.0.0.1:8766/callback` und genau die beiden Scopes
`user-read-playback-state user-modify-playback-state`. Doppelte/zusätzliche
Queryparameter, Credentials, Fragmente und abweichende Ziele werden abgelehnt.
Das Antwortschema prüft nur URL-Form und Länge, nicht die vollständige Profilbindung.
Die reale App-ID wird hier absichtlich nicht ein zweites Mal festgeschrieben.

Fehlerantwort: `{"id":17,"ok":false,"error":"setup_closed"}`.

| Fehlercode | Bedeutung |
| --- | --- |
| `setup_closed` | Physisches Einrichtungsfenster ist geschlossen |
| `lab_disabled` | Angefragte Funktion in diesem Profil nicht unterstützt |
| `not_ready` | Provider-/Anmeldezustand lässt den Vorgang nicht zu; umfasst falschen, abgelaufenen oder bereits verbrauchten Callback-State |
| `invalid_request` | Dekodierter Request wurde von der Provider-API als ungültig abgelehnt |
| `busy` | Ressourcen/Queue aktuell nicht verfügbar; keine Loginbestätigung |
| `device_error` | Sonstiger Gerätefehler, ohne rohe Providerantworten |

Lokale Helferfehler wie `usb_timeout`, `wrong_chip`, `auth_expired` und
`auth_failed` sind **keine** zusätzlichen S3-Wirefehler. Sie werden vom Helfer
gebildet und übersetzt. Es gibt keinen Trust-/Secretimport oder OTA-Befehl in
diesem Einrichtungsvertrag.

## Versionierung

Vertragsrelease **1.0.0**, Wire-Major **1** (`PWSET1`). Die Versionsnummer dieser
Beschreibung wird nicht als neues Pflichtfeld gesendet. Korrekturen ohne
Verhaltensänderung erhöhen PATCH. Rückwärtskompatible Ergänzungen erhöhen MINOR
nur mit dokumentierter Kompatibilität und Testmatrix. Strikt abgewiesene neue
Requestfelder sind nicht automatisch kompatibel. Eine Capability-Aushandlung
wäre erst gemeinsam zu implementieren; heute existiert kein solches Feld.
Inkompatible Feld-, Sicherheits- oder Ablaufänderungen benötigen neuen Wire-Major
und bewusst abgestimmte Firmware-/Helferreleases. Alte veröffentlichte
Vertragsartefakte bleiben per Commit und SHA-256 unverändert nachvollziehbar.

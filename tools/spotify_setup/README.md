# Spotify per USB einrichten

Dieser kurzzeitig laufende Desktophelfer reicht eine Spotify-Anmeldung über USB an
den RotaryKnob weiter. Er steuert keine Lautsprecher. PKCE-Verifier, Tokenaustausch,
Tokenablage, Erneuerung und Spotify-API liegen ausschließlich auf dem S3. Ein
PassionWave-Cloudservice wird nicht verwendet.

**Entwicklungsstand:** echter lokaler HTTP-/USB-Helfer mit isolierten Tests und
erster realer Geräteanmeldung auf dev.8 am 02.10.2026. Der S3 bestätigte den
konkreten Anmeldeversuch mit `linked: true`, `state: ready` und `error: none`;
der Nutzer bestätigte den Erfolg ebenfalls. Nach verifiziertem Appupdate auf
dev.9 und Neustart gelangen ohne erneute Anmeldung zwei Geräteabrufe mit jeweils
zwei erfolgreich geparsten Geräten. Die gespeicherte Verknüpfung blieb in diesem
Ablauf nutzbar. Weitere Refresh-/Kontowechsel- und Stromausfalltests, hörbare
Knob-Steuerung und Produktfreigabe bleiben offen.
[Gerätenachweis](../../docs/16-NATIVE-IMPLEMENTIERUNG.md#erste-erfolgreiche-ger%C3%A4teanmeldung-auf-dev8).

Die öffentliche App-ID verweist auf die nach ausdrücklicher Nutzerfreigabe am
01.10.2026 angelegte „PassionWave RotaryKnob Lab“-App. Development Mode und
`http://127.0.0.1:8766/callback` wurden im Dashboard bestätigt. Die öffentlichen
Profildaten stehen in [spotify-lab.json](../../profiles/spotify-lab.json);
`tools/check.py` prüft ihre Übereinstimmung mit Firmware und Helfer. Dieses
Werkzeug legt selbst keine Spotify-App an und akzeptiert keine Bedingungen.

## Start

Python 3.10 oder neuer und ein Computer mit USB sind erforderlich. Aus dem
Repositoryverzeichnis, beispielsweise unter macOS/Linux:

```sh
python3 -m venv /tmp/pw-spotify-setup-env
/tmp/pw-spotify-setup-env/bin/python -m pip install -r tools/spotify_setup/requirements.txt
/tmp/pw-spotify-setup-env/bin/python tools/spotify_setup/helper.py
```

Der identifizierte S3 erscheint unter macOS beispielsweise als `/dev/cu.usbmodem1101` mit VID/PID `303a:1001`; Portnummern können sich ändern. Der CH340-`usbserial`-Anschluss gehört bei diesem Gerät zum anderen Chip.

Unter Windows kann das virtuelle Environment an einem lokalen Ort angelegt und
dessen `Scripts\python.exe` verwendet werden. Windows-USB-/Treiberverhalten ist
noch nicht physisch qualifiziert. Die Laufzeitabhängigkeit ist `pyserial==3.5`;
HTTP-Server und Sicherheit verwenden die Python-Standardbibliothek.

1. RotaryKnob über USB mit dem Computer verbinden. Der **Display-Chip (S3)** muss
   über den USB-Stecker erreichbar sein und die passende Laborfirmware ausführen.
2. Der Helfer öffnet `http://127.0.0.1:8766/` im normalen Systembrowser. Bei mehreren
   USB-Kandidaten den Anschluss ausdrücklich auswählen. Es wird kein Chip geflasht.
3. Das Display drei Sekunden berühren, um die physische Einrichtung zu öffnen.
   Beim ersten Start über den QR-Code am Display das Heim-WLAN einrichten.
   Der Knob benötigt sein eingerichtetes WLAN und eine gültige Uhrzeit für TLS.
4. **Mit Spotify verbinden** wählen und direkt bei Spotify anmelden. Der Helfer
   fragt weder Kennwort noch API-Schlüssel ab. Spotify Premium und passende
   Testkontofreigabe sind für den vorgesehenen Laborweg erforderlich.
5. Nach Rückkehr zeigt die Seite zunächst, dass das Gerät die Anmeldung prüft.
   Erst die Bestätigung **dieses** Anmeldeversuchs vom S3 plus `linked: true` und Zustand `ready` ergibt die Meldung „verbunden“. Eine alte Verknüpfung nach fehlgeschlagener Neuanmeldung oder Neustart genügt nicht.
   Danach den Helfer mit Strg+C schließen. Die Steuerung läuft auf dem Knob weiter.

Optional `--port /dev/cu.usb…` bzw. `--port COM3` und `--no-browser` verwenden.
Der Port muss unter den aktuell aufgelisteten USB-Kandidaten vorkommen. Ein
belegter HTTP-Port beendet den Start; der Helfer weicht nicht auf eine andere
Adresse oder einen anderen OAuth-Redirect aus.

Für die gezielte Fehlersuche kann `--diagnostics` ergänzt werden. Der Helfer
meldet dann ausschließlich feste Rückgabegründe und begrenzte Formdaten wie
Parameteranzahl, Codelänge und Zeichenprüfung. Anmeldecodes, States, URLs,
Cookies, Tokens sowie frei angelieferte Parameternamen oder Werte werden nicht
ausgegeben. Formatfehler, zu lange Codes und ein gewechselter USB-Anschluss
erhalten unterscheidbare Hinweise. Diese Diagnose verändert weder die
Stateprüfung noch PKCE, Einmaligkeit oder die Firmwaregrenzen; sie bestätigt
keine erfolgreiche Anmeldung.

Die Option erfasst außerdem Änderungen des tokenfreien Gerätestatus einschließlich
HTTP-Status sowie ausgewählte vorhandene ESP-IDF-Transportfehler. Aus diesen
USB-Logzeilen bleiben ausschließlich feste Ereignisnamen und begrenzte numerische
Fehlercodes erhalten; Originalzeilen, Hosts und unbekannte Meldungen werden
verworfen. Diese Ereignisse identifizieren nicht den aufrufenden Gerätedienst:
Auch andere HTTPS-Funktionen können sie auslösen. Ein Netzwerkfehler beim
Anmeldeabschluss erhält einen eigenen Hinweis statt der pauschalen Aufforderung,
sich erneut anzumelden. Die Diagnose behebt selbst keinen Verbindungsfehler.

Ab dev.9 meldet `spotify_devices` zusätzlich das Ergebnis des Geräteabrufs:
HTTP-Status, numerischer Transportfehler, erfolgreiche Formatprüfung sowie
Anzahl gelieferter und erfolgreich geparster Geräte. Namen, IDs und Antwortinhalte werden
nicht ausgegeben. Dadurch bleibt ein Geräteabruf-Fehler erkennbar, auch wenn
eine spätere Wiedergabeabfrage den allgemeinen letzten HTTP-Status ersetzt.
Eine erfolgreich leere Liste und eine verworfene Antwort sind unterscheidbar;
daraus folgt noch keine Bedienbarkeit oder hörbare Wiedergabe.

## Verbindung und Fehler

- Ohne angeschlossenes Gerät oder mit mehreren Kandidaten werden keine Ports
  durchprobiert. Genau ein USB-Kandidat wird für HELLO automatisch ausgewählt.
- Falsche Rolle/Hardware wird deutlich angezeigt. Ein ESP32-Begleitprozessor ist
  kein S3. Bei keiner Antwort kann auch die alte Firmware laufen; eine Zeitüberschreitung
  wird deshalb nicht fälschlich als erkannter zweiter Chip ausgegeben.
- pySerial öffnet mit 115200 Baud, DTR/RTS bereits vor `open()` deaktiviert, ohne
  Resetimpuls und ohne esptool. Unter POSIX wird exklusiver Zugriff angefordert.
  Ein USB-/OS-Treiber kann beim Öffnen trotzdem kurz Leitungen verändern; das muss
  am konkreten Adapter gemessen werden, kein Softwaretest beweist Resetfreiheit.
- Ein geschlossenes Einrichtungsfenster blockiert Status, Anmeldung und Callback
  auf dem Gerät. Der Helfer zeigt über HELLO weiter den Hinweis zum langen Druck.
- Nach unterbrochenem USB, abgelaufener Anmeldung oder geschlossenem Fenster
  bewusst erneut verbinden. Ein übergebener Callback wird nicht automatisch
  wiederholt und gilt nicht als erfolgreicher Tokenaustausch.

## Fester USB-Vertrag

Nativer USB-Serial/JTAG-Anschluss des S3: eine UTF-8-JSON-Zeile, Prefix `PWSET1 `, abschließendes Newline; höchstens
8 KiB pro empfangener Zeile. Nicht passende Firmwarelogs werden verworfen und
weder angezeigt noch gespeichert. Anfragen haben eine positive ganzzahlige `id`
und eine der Methoden `hello`, `status`, `authorize`, `callback`, `cancel`.
Nur `callback` enthält zusätzlich `code` und `state`. Eine Antwort muss dieselbe
ID und ein boolesches `ok` besitzen. Negative Antworten haben einen stabilen
`error`-Code, niemals Inhalte, die direkt als Fehlermeldung reflektiert werden.
Timeouts: fünf Sekunden für HELLO/Status, zehn für die übrigen lokalen Vorgänge.

HELLO/Status enthalten `role: "controller_s3"`,
`hardware: "JC3636K518C_I_YR1"`, `version`, `lab_enabled`, `setup_open`.
Status enthält `spotify: {linked, state, error, connected, session, http_status, authorization_id}`. `authorization_id` ist der bereits verbrauchte OAuth-State und wird erst nach erfolgreicher Tokenablage gesetzt; er bleibt nur im RAM. Der Helfer vergleicht ihn mit seinem begonnenen Versuch und gibt ihn nicht an die Website weiter.
Der Helfer gibt nur ausdrücklich freigegebene Anzeigeattribute an den Browser;
unbekannte USB-Antwortfelder werden nicht durchgereicht. `authorize` liefert
`authorization_url`. Ein Callback-ACK bedeutet ausschließlich „angenommen“.

## OAuth und lokale Sicherheit

Der einzige Callback lautet `http://127.0.0.1:8766/callback`. Die Anwendung bindet
ausschließlich `127.0.0.1:8766`, nie `0.0.0.0`, IPv6 oder eine LAN-Adresse. Strikter
Host, Origin bei POST, SameSite=Strict/HttpOnly-Sitzungscookie, ein an diese Sitzung
gebundenes CSRF-Token, JSON-only-POST, kein CORS, CSP und No-Store gelten für die
lokale Website. Maximal acht Clients/Sitzungen; Anfragen und Bodygrößen sind begrenzt.

Anmeldung startet ausschließlich nach POST mit CSRF. Der Helfer validiert die vom
S3 erhaltene URL auf **genau** `https://accounts.spotify.com/authorize`, feste
Client-ID, festen Redirect, Code-Flow, S256-Challenge und genau die beiden Scopes
`user-read-playback-state user-modify-playback-state`. Zusätzliche Rechte,
Redirects, URL-Fragmente, Credentials und doppelte Queryfelder werden abgelehnt.
Die App-ID steht einmal in `helper.py`; Tests verwenden diesen Wert. Bei einer
freigegebenen App-Umstellung muss sie mit `pw_spotify.h` übereinstimmen. Kunden
geben keine eigene App-ID ein; es gibt keinen frei überschreibbaren OAuth-Endpunkt.

Die von Spotify zurückkommende Antwort wird gesondert geprüft: Unbekannte
Zusatzparameter werden entsprechend [RFC 6749 §4.1.2](https://www.rfc-editor.org/rfc/rfc6749.html#section-4.1.2)
ignoriert, nicht an das Gerät weitergereicht oder als Einstellungen übernommen.
Das gilt nicht für die oben beschriebene Autorisierungs-URL vom Gerät, deren
Profilprüfung unverändert strikt bleibt. Doppelte Parameter (auch nach
URL-Dekodierung), gleichzeitig `code` und `error`, falscher/fehlender State,
Überlänge und Wiederholung bleiben abgelehnt. Maximal vier Rückgabeparameter
und 4096 Zeichen Anfragepfad; zum Gerät gelangen ausschließlich Code und State.

Der 48-stellige Hex-State bleibt maximal zehn Minuten und nur für die beginnende
Browsersitzung/den ausgewählten Port gültig. Eine parallele Sitzung darf ihn nicht
ersetzen. Er wird vor USB-Übergabe einmalig verbraucht, auch bei Fehlern. Weil der
Strict-Cookie bei einer Rücknavigation von Spotify fehlt, identifiziert allein
der vorher autorisierte unvorhersagbare State den lokalen Vorgang. Es entsteht
keine neue Sitzung durch den Callback. Die Antwort ist stets `303 /`, ohne Code
oder State in Zieladresse, HTML oder Logs. Ein anschließender gleichseitiger Fetch
erkennt wieder den ursprünglichen Strict-Cookie; dieser Ablauf wird in Chromium
und WebKit getestet. Status bestätigt den tatsächlichen Gerätezustand, nicht den Redirect.

Autorisierungscode und State sind kurzzeitig in RAM. Python bietet keine
garantierte Nullung unveränderlicher Strings. Der Helfer schreibt keine Codes,
Tokens, PKCE-Verifier, Sitzungen oder Browserdaten auf Disk. Er deaktiviert auch
HTTP-Zugriffs-/Fehlerlogs und Tracebacks aus Handlern. Browserhistorie und lokale
Schadsoftware liegen außerhalb dieses Schutzes; der Callback wird sofort auf die
saubere Startadresse umgeleitet. Dies ist ein Desktop-Laborweg und noch kein
qualifizierter mobiler Produkteinrichtungsweg.

## Prüfungen

```sh
python3 -m unittest discover -s tools/spotify_setup/tests -p 'test_*.py' -v
# Separates Testenvironment: Playwright 1.58.0 samt Chromium/WebKit installieren.
python3 tools/spotify_setup/tests/browser_check.py --browser chromium
python3 tools/spotify_setup/tests/browser_check.py --browser webkit
```

Die Python-Tests prüfen das echte HTTP-Verhalten auf Loopback und reale
Serial-Framinglogik gegen einen Fakeport: korrelierte IDs, verworfene Logs,
Überlänge, Timeout, falscher Chip, physisches Fenster, Session/CSRF/Origin/Host,
Scope-/Redirectprüfung, Einmaligkeit/TTL/Abbruch und keine Secret-Reflexion.
Der Browsertest verwendet den echten HTTP-Handler in isolierten Chromium-/WebKit-
Profilen. Der Testserver erhält einen eigenen kurzlebigen Loopbackport; sämtliche
Browseranfragen werden abgefangen und nur an ihn beziehungsweise die synthetische
Spotify-Seite weitergereicht. Der reale Helfer auf Port 8766 und echtes USB werden
nicht angesprochen. Nur im isolierten Testprozess verwenden Origin und Redirect
den Testport; Browserheader und Cookies erreichen den HTTP-Handler unverändert.
Die feste Produktbindung wird separat geprüft. Er prüft speziell zusätzliche Callbackparameter,
Strict-Cookie/Rückleitung, ausstehenden
Callback gegenüber bestätigtem Status, Bedienbarkeit bei 390 px und leere
Webstorage-APIs. Screenshots werden ausschließlich in ein temporäres Verzeichnis
geschrieben. Die erste reale USB-/OAuth-Anmeldung auf dev.8 sowie die ohne neue
Anmeldung erfolgreiche Geräteabfrage nach Appupdate/Neustart auf dev.9 sind
separat dokumentiert. Weitere Refresh-/Kontowechsel-, Stromausfall- und
Treiberprüfungen bleiben offen.

Primärquellen: [Spotify PKCE](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow),
[Spotify Redirectregeln](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri),
[pySerial 3.5 API und DTR/RTS-Hinweise](https://pyserial.readthedocs.io/en/latest/pyserial_api.html).

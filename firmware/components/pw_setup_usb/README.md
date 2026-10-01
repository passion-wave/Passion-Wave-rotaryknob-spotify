# Lokale Spotify-Verknüpfung über USB

Dieser Component ist ausschließlich im expliziten `CONFIG_PW_SPOTIFY_LAB=y`-Profil
aktiv. Der normale Produktbuild installiert keinen USB-Setup-Task. Der
Produktfreigabestatus bleibt auch im Laborprofil falsch.

Der S3 erzeugt PKCE-Verifier und OAuth-State selbst. `tools/spotify_setup` öffnet
am angeschlossenen Computer die von ihm gelieferte Spotify-URL. Nach der normalen
Spotify-Anmeldung empfängt der lokale Helfer den einmaligen Code über
`http://127.0.0.1:8766/callback` und gibt Code plus State über USB zurück. Erst der
S3 tauscht den Code per HTTPS ein, speichert Tokens und steuert Spotify Connect.
Es gibt keinen externen PassionWave-Callbackdienst und keinen laufenden
Desktop-Steuerungsserver als Betriebsabhängigkeit. Der Helfer ist ein Laborweg;
er entscheidet noch nicht über die vorgeschlagene mobile Kundeneinrichtung.

## Schnittstelle

UART0, 115200 Baud, 8N1, Zeilen mit Präfix `PWSET1 ` und einem einzelnen JSON-Objekt.
Der USB-Stecker muss den **Display-S3** mit dem Computer verbinden. Der vorhandene
Companiontransport auf UART1 bleibt unberührt. Der Helfer darf beim Öffnen weder
DTR noch RTS aktivieren; es wird kein Reset/Flash ausgelöst.

| Methode | Zusätzliche Requestfelder | Ergebnis |
| --- | --- | --- |
| `hello` | keine | Rolle, Hardware, Version, Laborprofil, physisches Setupfenster |
| `status` | keine | dieselbe Identität, tokenfreier Spotify-Verknüpfungsstatus |
| `authorize` | keine | öffentliche Autorisierungs-URL und Ablaufdauer |
| `callback` | `code`, `state` | nur Annahme des asynchronen Codeaustauschs |
| `cancel` | keine | laufenden Anmeldeversuch verwerfen |

Jeder Request enthält `id` als positive Ganzzahl bis 2³¹−1 und `method`.
Antworten enthalten dieselbe ID, `ok` und bei Fehler einen festen Fehlercode.
Unbekannte/duplizierte Felder, zusätzliche Daten nach JSON, Steuerzeichen,
NUL-Zeichen und zu lange Eingaben werden verworfen. Frames sind auf 8191 Byte,
Codes auf 1024 ASCII-Zeichen und State auf 48 kleine Hexzeichen begrenzt.
Fehlertexte enthalten keine übertragenen Codes oder Zugangstoken.

Nur `hello` ist ohne physischen Setupmodus verfügbar. Alle anderen Befehle
benötigen das per langem Drücken geöffnete Gerätefenster. Ein Anmeldeversuch ohne übergebenen Callback wird beim Schließen des Fensters verworfen. Ein im geöffneten Fenster bereits angenommener Codeaustausch darf auf dem S3 fertiglaufen; sein Ergebnis ist erst nach erneut geöffnetem Fenster über USB abfragbar.
Ein angenommener Callback bestätigt noch keine Verknüpfung: Der Helfer muss den
tatsächlichen Providerstatus und die zur begonnenen Anmeldung passende `authorization_id` prüfen. Diese RAM-Kennung wird erst nach erfolgreicher Tokenablage gesetzt und ist der bereits verbrauchte OAuth-State. Tokens und PKCE-Verifier verlassen den S3
niemals über diese Schnittstelle. Die UART-Zeilen sind kein verschlüsselter
Transport; dieser Laborpfad setzt den physischen USB-Anschluss voraus.

## Build und Nachweise

Zum vorhandenen `sdkconfig.defaults` kommt ausdrücklich `sdkconfig.spotify-lab`
hinzu. Ein separates Build-/SDKCONFIG-Verzeichnis verhindert, dass ein Laborflag
unbemerkt den normalen Build verändert. Die Client-ID stammt aus der vom Nutzer
benannten Entwickler-App; der Loopback-Redirect muss dort passend registriert
sein. Ein vorhandener Dashboardeintrag bedeutet keine kommerzielle Freigabe.

`tests/setup_native/run.sh` prüft die echte begrenzte C-Requestvalidierung mit
ASan/UBSan. Das ist kein USB-Geräte-, OAuth- oder Wiedergabenachweis. Vor dem ersten
Flash gelten weiterhin Chipidentifikation, verifiziertes Originalbackup und die
explizite Vorbereitung der neuen NVS-Partitionierung aus
[Geräteabnahme](../../../docs/16-NATIVE-IMPLEMENTIERUNG.md).

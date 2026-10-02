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

Nativer **USB-Serial/JTAG-Controller des ESP32-S3**, Zeilen mit Präfix `PWSET1 `
und einem einzelnen JSON-Objekt. Am identifizierten Display-S3 ist das USB-Gerät
`303a:1001`; macOS stellt es als `/dev/cu.usbmodem…` bereit. Das ist weder UART0
hinter einem USB-UART-Adapter noch USB-OTG/TinyUSB CDC. Die im seriellen Helfer
eingestellten 115200 Baud legen hier keine physische UART-Geschwindigkeit fest.
Der USB-Stecker muss den **Display-S3** mit dem Computer verbinden. Der vorhandene
Companiontransport auf UART1 bleibt unberührt; dieser Component konfiguriert
keine UART-Pins. Der Helfer aktiviert DTR/RTS nicht und führt weder Reset- noch
Flashbefehle aus. Betriebssystem-/USB-Treibereffekte beim Öffnen sowie Abziehen,
Reset und erneutes Enumerieren müssen trotzdem am Gerät geprüft werden.

Das Laborprofil wählt USB Serial/JTAG als primäre Console, keine zweite Console
und Info-Loglevel. Nach der Treiberinstallation stellt
`usb_serial_jtag_vfs_use_driver()` die schon registrierte Console auf denselben
Interrupttreiber um. Ein zweiter USB-/stdin-/REPL-Leser ist nicht zulässig. Ein
bereits anderweitig installierter USB-Treiber wird deshalb abgelehnt. Der
Produktbuild installiert weiterhin keinen Setup-Task und keinen Setup-Treiber.

RX- und TX-Treiberring sind jeweils auf 8192 Byte begrenzt. Eine Antwort wird als
ganzer Frame eingereiht, mit einem zusätzlichen führenden Zeilenumbruch: Normale
Console-Ausgaben können so nicht in die JSON-Zeile gelangen; ein vorher nur
teilweise geschriebenes Log wird abgetrennt. Der Helfer ignoriert solche Log- und
Leerzeilen. Diese Eigenschaft stützt sich auf die vollständige Ringkopie in
ESP-IDF 5.4.3 `usb_serial_jtag_write_bytes()` und den gemeinsam verwendeten
Treiber; direkte ROM-/Panic-Ausgaben sind davon nicht umfasst. Der Frame wird
nach Annahme durch den Treiber nicht erneut gesendet. Einreihen und Flush haben
zusammen ein Zeitbudget von zwei Sekunden, zuzüglich Task-Scheduling. Ein
USB-Flush ist keine Bestätigung des Helfers oder einer Spotify-Anmeldung; bei
Verbindungsabbruch kann eine Antwort ausbleiben oder verspätet ankommen.

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
Ein USB-Lesevorgang wartet höchstens 100 ms. Unvollständige Eingaben werden nach
fünf Sekunden Gesamtzeit verworfen und bis zum nächsten Zeilenumbruch ignoriert;
fünf Sekunden ohne weitere Bytes setzen auch diesen Verwerfungszustand zurück.
Die Frist wird zwischen Lesevorgängen geprüft, nicht während der synchronen
Antwortübertragung. Parserregeln und zulässige Requestfelder bleiben unverändert.

Nur `hello` ist ohne physischen Setupmodus verfügbar. Alle anderen Befehle
benötigen das per langem Drücken geöffnete Gerätefenster. Ein Anmeldeversuch ohne übergebenen Callback wird beim Schließen des Fensters verworfen. Ein im geöffneten Fenster bereits angenommener Codeaustausch darf auf dem S3 fertiglaufen; sein Ergebnis ist erst nach erneut geöffnetem Fenster über USB abfragbar.
Ein angenommener Callback bestätigt noch keine Verknüpfung: Der Helfer muss den
tatsächlichen Providerstatus und die zur begonnenen Anmeldung passende `authorization_id` prüfen. Diese RAM-Kennung wird erst nach erfolgreicher Tokenablage gesetzt und ist der bereits verbrauchte OAuth-State. Tokens und PKCE-Verifier verlassen den S3
niemals über diese Schnittstelle. Die USB-Zeilen sind kein verschlüsselter
Transport; dieser Laborpfad setzt den physischen USB-Anschluss voraus.

## Build und Nachweise

Zum vorhandenen `sdkconfig.defaults` kommt ausdrücklich `sdkconfig.spotify-lab`
hinzu. Ein separates Build-/SDKCONFIG-Verzeichnis verhindert, dass ein Laborflag
unbemerkt den normalen Build verändert. Die Client-ID stammt aus der vom Nutzer
benannten Entwickler-App; der Loopback-Redirect muss dort passend registriert
sein. Ein vorhandener Dashboardeintrag bedeutet keine kommerzielle Freigabe.

Die Transportanbindung verwendet die lokalen offiziellen Header und Quellen aus
ESP-IDF 5.4.3, `components/esp_driver_usb_serial_jtag` und
`components/esp_system/Kconfig`. Ein Build bestätigt die API-/Kconfig-Anbindung,
noch nicht Empfang, gleichzeitige Info-Logs oder Verhalten beim Abziehen. Diese
Hardwaretests sind für den nativen USB-Pfad weiterhin erforderlich. Das Gerät
muss für die Einrichtung wach bleiben; Reset, Tiefschlaf und Belegung des
gemeinsamen USB-PHY durch eine andere USB-Funktion können die Verbindung trennen.

`tests/setup_native/run.sh` prüft die echte begrenzte C-Requestvalidierung mit
ASan/UBSan. Das ist kein USB-Geräte-, OAuth- oder Wiedergabenachweis. Vor dem ersten
Flash gelten weiterhin Chipidentifikation, verifiziertes Originalbackup und die
explizite Vorbereitung der neuen NVS-Partitionierung aus
[Geräteabnahme](../../../docs/16-NATIVE-IMPLEMENTIERUNG.md).

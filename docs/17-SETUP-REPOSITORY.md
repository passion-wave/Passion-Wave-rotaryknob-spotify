# Separates Repository für PassionWave Setup vorbereiten

Stand: 1. Oktober 2026. Diese Vorbereitung legt **kein Repository** an und
verschiebt noch keinen Code. Ausgangspunkt sind die native
[Spotify-Edition](../README.md), der
[USB-Einrichtungshelfer](../tools/spotify_setup/README.md) und der
[USB-Laborvertrag](../contracts/usb-setup/v1/README.md).

## Name und Zuständigkeiten

Empfohlener Repositoryname: `passion-wave/Passion-Wave-rotaryknob-setup`.
Anzeigename der Einrichtungsanwendung: **PassionWave Setup**. Die Spotify-
Dashboard-App **PassionWave RotaryKnob Lab** ist eine gesonderte OAuth-
Registrierung und nicht dieses Quellcoderepository.

Der Nutzer hat das Anlegen dieser Spotify-Labor-App genehmigt. Sie wurde im
Development Mode mit Web API und der festen Loopback-Rückleitung angelegt.
Die öffentlichen Appdaten werden zentral als `profiles/spotify-lab.json` im
Firmware-Repo geführt. Dieses Profil wird später genauso wie der USB-Vertrag
gepinnt übernommen. Client-ID und Redirect sind öffentliche Metadaten, keine
Vertriebsfreigabe. Ein Client-Secret wird für diesen PKCE-Weg nicht benötigt.

| Bestehendes Firmware-Repository | Späteres Setup-Repository |
| --- | --- |
| Beide MCU-Firmwares und Boardtreiber | Vorübergehend laufender Desktop-Einrichtungshelfer |
| Spotify-Provider, PKCE-Verifier, Codeaustausch, Tokenablage und Erneuerung auf S3 | Systembrowser öffnen, Loopback empfangen, Code/State über USB weiterreichen |
| Displaybedienung, Spotify-Connect-Steuerung, Wetter, Avatar, Katalog | Einfache deutsche Einrichtungsoberfläche und verständliche USB-/Anmeldefehler |
| Geräteinterne Website und persistente Einstellungen | Desktoppaketierung, Appsignierung und Installation des Helfers |
| Signierte Firmwareupdates, Trustprüfung, Pair-OTA und Recovery | Updates der Desktopanwendung als eigener Releaseprozess |
| Kanonische USB-Verträge und öffentliche Profile | Gepinnte Vertrags-/Profilkopien und Gegenstellen-Conformance |

Der Helfer wird weder ein dauerhafter Desktopcontroller noch ein eigener
externer Anmeldedienst. Nach Einrichtung steuert der Knob selbst. Der bestehende
Helfer erhält durch den Repo-Split keine USB-Flashfunktion und keinen Zugriff auf
Gerätebackups. Firmware-OTA und Desktop-Appupdates bleiben getrennte Prozesse.

## Vorgesehene Struktur

```text
Passion-Wave-rotaryknob-setup/
  apps/desktop/                 Python-Einstieg und lokaler HTTP-/USB-Helfer
  ui/                           Lokale Einrichtungswebsite
  protocol/usb-setup/v1/         Unveränderte gepinnte Vertragskopie
  profiles/spotify-lab.json      Gepinntes öffentliches OAuth-Laborprofil
  upstream.lock.json             Quellrepo, Commit, Version und SHA-256 je Übernahme
  tests/unit/                    Zustände, URL-/Profilprüfung, Serial-Framing
  tests/http/                    Host, Origin, CSRF, Cookie, Callback
  tests/browser/                 Isolierte echte Browserabläufe
  tests/conformance/             Gemeinsame Good-/Bad-/Ablauf-Vektoren
  packaging/macos/               Spätere signierte Desktoppakete
  packaging/windows/
  packaging/linux/
  docs/                          Bedienung, Sicherheitsmodell, Kompatibilität
  LICENSE
  README.md
```

Dies ist eine Zielstruktur, kein heute erzeugtes Verzeichnisgerüst. Eine mobile
App wird erst nach Auswahl und Qualifikation dieses Einrichtungswegs ergänzt;
sie ist nicht automatisch eine Kopie des Python-Loopbackhelfers. Keine leeren
Plattformpakete werden als vorhandene Installer ausgegeben.

## Gemeinsame Verträge und Profile

Kanonisch bleibt zunächst
[contracts/usb-setup/v1](../contracts/usb-setup/v1/README.md) im Firmware-Repo.
Die bestehende USB-Zeilenrahmung `PWSET1`, fünf Methoden, Bounds, ID-Korrelation,
Setupfenster, OAuth-Abschlussmarker, Fehler und Ablaufregeln sind dort gemeinsam
mit JSON-Schemata und synthetischen Golden-Vektoren beschrieben. Ein drittes
Vertragsrepository ist für zwei Pilotgeräte nicht nötig.

Im Setup-Repo werden diese Dateien unverändert übernommen. `upstream.lock.json`
nennt den unveränderlichen Quellcommit, Vertragsrelease, Wire-Major und SHA-256
aller übernommenen Dateien einschließlich des öffentlichen Profils. Ein
beweglicher Branch wie `main` allein ist keine Versionsbindung. Vertragskopien
werden über einen eigenen Review aktualisiert, nicht beim Benutzerstart aus
beliebigen Netzwerkquellen geladen oder frei konfigurierbar ersetzt.

Beide CI müssen dieselben Vektoren gegen ihre **echten** Parser und Abläufe
ausführen. C-seitig betrifft dies den USB-Decoder sowie Status-/Callbacklogik;
appseitig Serial-Framing, Antwortkorrelation, Profilprüfung und einmalige
Callbackzustände. Zusätzlich braucht es eine dokumentierte Matrix aus
Firmwareversion, Appversion, Vertragsversion und Profilcommit. Tests dürfen
keine Spotify-Anmeldung, Freigabe oder physische Gerätepaarung aus Fixtures
ableiten.

Eine neue Client-ID ist eine Änderung öffentlicher Profilmetadaten, nicht
automatisch ein neuer Wire-Major. S3 und Helfer müssen trotzdem dasselbe bewusst
qualifizierte Profil verwenden; das Release muss auch den Übergang bereits
gespeicherter Verknüpfungen bewerten. Keine Vermischung von Tokens verschiedener
OAuth-Apps und kein automatischer Import fremder Kontodaten.

Versionierung: Die erste Vertragssammlung ist **1.0.0** zu `PWSET1`. PATCH für
rein präzisierende Korrekturen; MINOR nur für tatsächlich kompatible Erweiterungen.
Der aktuelle Decoder lehnt unbekannte Requestfelder ab. Eine geplante Capability-
Aushandlung muss deshalb erst implementiert und geprüft werden. Inkompatible
Änderungen bekommen beispielsweise `PWSET2`; ein Repo-Split ändert das
Wireformat nicht.

## Gerätekopplung und Geheimnisse

Heute gilt der Laborweg **physisches USB plus am Knob geöffnetes Setupfenster**.
Der S3 nutzt seine native **USB Serial/JTAG-Schnittstelle (303a:1001)**; der
Desktopbaudwert 115200 ist CDC-/Serial-API-Kompatibilität, kein UART0-Transport.
UART1 zwischen den beiden Chips bleibt unverändert.
Rolle und Boardmodell verhindern einfache Verwechslung, authentisieren aber
kein individuelles Gerät. Die USB-Protokollnachrichten sind unverschlüsselt. Ein künftiger sicherer
LAN-/Mobile-Pairingvertrag wird gesondert spezifiziert und nachgewiesen, statt
ihn aus der USB-Verbindung oder dem OAuth-State abzuleiten.

`session` ist kein Anmeldenachweis. Ein Callback-ACK bestätigt nur die Annahme.
`authorization_id` bestätigt genau den begonnenen Anmeldeversuch erst nach
erfolgreicher Tokenpersistenz auf dem S3; nach Neustart fehlt dieser RAM-Marker.
Damit darf eine weiter bestehende alte Verknüpfung nicht als Erfolg eines
fehlgeschlagenen neuen Versuchs erscheinen. Die verbindlichen Regeln stehen im
USB-Vertrag und werden als gemeinsame Ablauf-Vektoren mitgeführt.

Client-ID, Redirect, Scopes, Protokollversion und öffentliche Prüfschlüssel dürfen
als qualifizierte öffentliche Metadaten versioniert werden. **Nicht** übernommen
oder eingecheckt werden Client-Secrets, Zugriffstokens, Refresh-Tokens, PKCE-
Verifier, echte Callbackcodes, Geräteschlüssel, OTA-Privatschlüssel, Benutzer-
Profile oder vollständige Flashbackups. Keine gemeinsamen Defaultpasswörter.
Signierschlüssel für künftige Desktoppakete gehören in getrennte geschützte
Releaseumgebungen; ihre Existenz ist nicht Bestandteil dieser Vorbereitung.

## Labor und Produkt

Laborartefakte heißen erkennbar „Lab“, verwenden das qualifizierte Laborprofil
und nur freigeschaltete Testkonten. Die aktuelle Firmware aktiviert den Adapter
über ein eigenes `CONFIG_PW_SPOTIFY_LAB`-Profil; der Standardbuild bleibt davon
getrennt. Apppakete und CI müssen diese Trennung ebenfalls erhalten. Ein
Produktprofil wird erst nach passender Freigabe, Onboarding- und Geräteabnahme
eingeführt und nie durch einen Schalter im Kunden-JSON freigegeben.

Die getrennte App löst weder die kommerzielle Spotify-Controller-Freigabe G0
noch den sicheren jederzeit schreibenden Heimnetz-Webzugriff G2. Diese Grenzen
bleiben aus [Spotify](02-SPOTIFY.md), [Onboarding](03-ONBOARDING-WEB.md) und
[Geräteabnahme](16-NATIVE-IMPLEMENTIERUNG.md) bestehen. Die Dashboardanlage ist
eine Laborvoraussetzung, kein Abschluss dieser Gates.

## Migration, sobald das neue Repository bereitsteht

1. Quellcommit, Lizenz und vorhandene Testnachweise festhalten. Profil und
   Vertragsrelease inklusive Checksummen als Übergabepunkt markieren.
2. Die inzwischen vorhandene gemeinsame C-/Python-Golden-Suite in beiden CI
   verbindlich ausführen und eine vollständige JSON-Schema-2020-12-Prüfung ergänzen;
   bestehende Sicherheitsfälle behalten. C-Prüfungen ohne vorhandene Toolchain
   dürfen nicht still als bestanden gelten.
3. `tools/spotify_setup` samt Tests und Lizenzhistorie in das neue Repo überführen;
   Einstieg, UI-Pfade und Testimporte auf die Zielstruktur anpassen.
4. Profil-/Vertragskopien samt `upstream.lock.json` übernehmen; keine Tokens oder
   lokalen Browser-/USB-/Backupdaten kopieren. Bei Abweichungen vom Lock prüfen
   CI und Packaging nicht erfolgreich weiter.
5. App-CI für Python, echte Loopback-HTTP-Tests, isolierte Browsertests und
   Conformance aufbauen. Firmware-CI prüft weiterhin die Gegenstelle und die
   veröffentlichte Kompatibilitätsmatrix. Desktopinstallation separat je OS testen.
6. Erst nach bestandenen Tests den Firmware-Repo-Helfer durch einen eindeutigen
   Verweis auf das gepinnte Setup-Release ablösen; keine dauerhaft auseinander
   laufenden Kopien pflegen. Bis dahin bleibt der vorhandene Pfad funktionsfähig.
7. Laborpaket gezielt an den beiden Pilotgeräten prüfen: falscher Chip, Fenster-
   ablauf, USB-Abbruch, erneute Anmeldung, alte Verknüpfung, Neustart und Beenden
   des Helfers. Produktpakete erst nach den gesonderten Freigaben anbieten.

## Nachweis dieser Vorbereitung

Die ursprüngliche Strukturvorbereitung umfasste dieses Dokument und
`contracts/usb-setup/v1/`; sie hat kein Repository angelegt oder Code ausgelagert.
Die anschließende Conformance-Ergänzung liegt in
[tests/contracts](../tests/contracts/test_usb_setup.py) und wird von
`python3 tools/check.py` entdeckt. Sie führt gemeinsame Goldens gegen den echten
Python-Helfer sowie bei bereitgestelltem IDF/cJSON gegen den C-Requestparser aus.
Dabei wurde die positive Methodenformprüfung im Helfer ergänzt; die öffentliche
Client-ID bleibt an das zentrale Profil gebunden. Die Rootfunktion
`check_spotify_profile` bleibt erhalten.

Die Schema-Anwendung ist weiterhin ein begrenzter eigener Validator, kein
vollständiger JSON-Schema-2020-12-Nachweis. S3-Antworten und OAuth-Gegenstelle
werden in den Helfertests simuliert. C-Goldens prüfen den Requestdecoder, keinen
echten USB-Treiber und keinen Tokenaustausch. Workflow-/Paketierung im späteren
Setup-Repo, Browser-Golden-Verknüpfung und physische Abnahme bleiben offen.

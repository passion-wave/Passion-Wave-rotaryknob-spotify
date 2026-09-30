# Radioverzeichnis, Ausgabefähigkeiten und Klinke

**Festlegung vom 30. September 2026:** Die erste Version liefert Senderverwaltung; hörbare Radioausgabe folgt als spätere Erweiterung ([Q03](15-OFFENE-PRODUKTENTSCHEIDUNGEN.md)). Klinke/Kopfhörer und ein nativer Sonos-Radiopfad bleiben eigene Prüfaufträge, keine Voraussetzung für den ersten Zweigerätepilot oder die erste Produktversion.

## 1. Radio Browser als Datenquelle

Empfehlung: [Radio Browser API](https://docs.radio-browser.info/). Öffentliches Senderverzeichnis, keine verpflichtende API-Key-Eingabe für Kunden. Es liefert Sendernamen und Streamadressen; Erreichbarkeit, Codec, Region und Rechte werden dadurch nicht garantiert. Kein eigener vollständiger Datenbankspiegel auf dem Gerät.

Ablauf: Server entdecken (`all.api.radio-browser.info` per DNS/Reverse-DNS oder `/json/servers`), konkreten HTTPS-Spiegel auswählen, Suchanfrage mit Zeitlimit/Backoff senden. Beispiel, Host nicht als einzige dauerhaft garantierte Instanz einbauen:

```text
https://de1.api.radio-browser.info/json/stations/search?name=NDR&countrycode=DE&hidebroken=true&is_https=true&limit=20
```

Browser spricht nur mit der Geräte-API; S3 fragt das Verzeichnis ab. Aussagekräftiger `User-Agent: PassionWaveRotarySpotify/<version>`, begrenzte Ergebniszahl und begrenzter Cache. Fällt ein Spiegel aus, nächsten nutzen; gespeicherte Liste bleibt offline editierbar. Netzwerkadapter lässt sich gegen Testdaten austauschen. [API](https://docs.radio-browser.info/), [Radio Browser FAQ](https://www.radio-browser.info/faq)

Pro Eintrag: lokale ID, `stationuuid`, Name/Anzeigename, ursprüngliche `url`, `url_resolved`, Codec, Bitrate, `hls`, `lastcheckok`, Prüfdatum, aktiv, Reihenfolge und gewünschter geeigneter Ausgang. Eigene URLs bleiben ohne Datenbank-UUID möglich. `url_resolved` kann trotzdem eine HLS-Playlist sein. Ein Datenbank-Haken ersetzt keinen Wiedergabetest. Klickmeldung `/json/url/{stationuuid}` erst bei tatsächlichem Start, nicht beim Speichern oder Suchergebnisanzeigen.

Im Webentwurf sind Suchergebnisse Beispiele. Live-API und Streamprobe sind noch nicht implementiert.

## 2. Ausgabematrix

| Inhalt | Spotify-Connect-Lautsprecher | Lokaler DAC | Hersteller-Radioadapter |
| --- | --- | --- | --- |
| Spotify-Playlist | nach G0 und Fähigkeit | nur separater lizenzierter Spotify-Receiver | keine generische Zusage |
| Spotify-Episode | nach G0 und nachgewiesenem Episodenstart | nur separater lizenzierter Spotify-Receiver | keine generische Zusage |
| Podcast-Show | Sammlung zum Öffnen, kein blindes Play | Sammlung | keine generische Zusage |
| MP3/AAC-Radiostream | **nicht über Connect** | nach Decoder-/Hardwareprüfung | nur für konkret unterstützte Geräte |
| HLS/andere Codecs | **nicht über Connect** | zunächst deaktiviert, eigener Decoder-/Speichertest | capabilityabhängig |

Sonos ist jetzt der konkrete Prüfkandidat für einen solchen zusätzlichen Renderer: zunächst vorhandene Sonos-Radiofavoriten, danach eigene Radio-Browser-URLs unter gesondertem API-/Sessionnachweis. Siehe [Sonos-Prüfung](12-SONOS-PRUEFUNG.md). Ein erfolgreicher Spotify-Start auf Sonos belegt keinen Radio-URL-Pfad.

Ein Connect-Lautsprecher kann zusätzlich etwa einen herstellerspezifischen Radioplayer anbieten. Das macht diesen Radioweg nicht zu Spotify Connect. Eine solche Erweiterung wäre bewusst außerhalb des Spotify-Providers, direkt auf dem Gerät und optional; keine HA-/MA-Brücke.

Die Website kann Radioeinträge immer verwalten, zeigt ohne geeigneten Ausgang „Gespeichert – Radioausgang noch nicht verfügbar“. Nicht schweigend Spotify starten oder eine Radio-URL in ein Spotify-URI-Feld einsetzen. Die Senderverwaltung erfüllt Q03 für die erste Version; eine spätere Radiowiedergabe braucht einen zusätzlich qualifizierten Audioweg.

Spotify- und Radioangebot gemeinsam vor Vertrieb mit dem Produktpartner klären: Öffentliche API-Policy und Hardware-Verträge haben unterschiedliche Kontexte. Keine generelle Freigabe aus technischen APIs ableiten. [Developer Policy](https://developer.spotify.com/policy), [Hardware-Anforderungen](https://developer.spotify.com/documentation/commercial-hardware/implementation/requirements/technical)

## 3. URL-Sicherheit und Probe

Nur `http`/`https`; keine Zugangsdaten im URL, keine lokalen Dateipfade. Standardmäßig keine Loopback-/Link-local-/privaten Netzadressen und keine Hostnamen, die dorthin auflösen. Bei jedem Redirect neu prüfen und die geprüfte Adresse beim Verbindungsaufbau verwenden; keine erneute ungeprüfte DNS-Auflösung (DNS-Rebinding). Eigene interne Streams benötigen später eine bewusst gesonderte Administratorfreigabe, die nie als allgemeiner Proxy arbeitet.

Begrenzter GET (z.B. 64 KiB, fünf Sekunden, höchstens drei Redirects) statt blindem Vertrauen auf HEAD. Zertifikate prüfen; TLS-Fehler sichtbar, keine globale Deaktivierung. HTTP-Streams als unverschlüsselt kennzeichnen und im Produktprofil nur bewusst erlauben. Playlisten und Metadatenlängen begrenzen; HLS/PLS/M3U Parser getrennt. Kein automatischer Download von Logos/URLs aus ungeprüften Metadaten. Keine Inhalte in Hostname-/HTML-/Shellkontext interpolieren.

Verzeichnis-Softwarelizenz, Datenbanknutzung und Rechte an Streams/Logos sind getrennte Fragen. Eigene Clientnutzung benötigt keinen eingebetteten Radio-Browser-Server. [Offizielles Projekt](https://github.com/segler-alex/radiobrowser-api-rust)

## 4. Was am Klinkenanschluss belegt ist

Bestandsfirmware decodiert bislang kein Audio. Das offizielle Schaltbild des vergleichbaren Waveshare-Knobs zeigt PCM5100A, einen I2S-Umschalter und Audioausgänge zur Tochterplatine. Guition- und Waveshare-Revision des tatsächlich vorhandenen Geräts müssen vor Ansteuerung abgeglichen werden. Kandidatenbelegung und Audit stehen in [Upstream](06-UPSTREAM.md).

TI spezifiziert den PCM5100A als Line-Ausgang mit 2,1 Vrms und Last ab 1 kΩ. Daraus folgt keine Eignung für typische 16–64-Ω-Kopfhörer. Im verfügbaren Schaltbild ist kein Kopfhörerverstärker belegt; die Tochterplatine zum Anschluss ist nicht vollständig dokumentiert. [TI PCM5100A](https://www.ti.com/product/PCM5100A), [Waveshare](https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8)

**Entscheidung:** Fähigkeit zunächst `local_line_out`, Auslieferung deaktiviert. Kopfhörerfähigkeit erst nach Schaltungs-/Messnachweis; andernfalls zusätzlicher Kopfhörerverstärker. Es ist kein reines Softwarehäkchen.

### Audio-Probe vor Integration

1. PCB-Revision/Fotos/Bauteile prüfen, Originalschaltung identifizieren; I2S-Mux und Mute festlegen. GPIO0 ist Boot-Strap, nicht beliebig beim Start treiben.
2. S3 I2S → DAC zunächst mit Testlast/Oszilloskop, sehr kleinem Pegel und Rampen testen. Keine Testtöne mit aufgesetzten Kopfhörern.
3. Mute bei Reset, Update, fehlendem Peer und Unterlauf; analoges Einschaltknacken/Offset prüfen. Classic kontrolliert laut Referenzschaltung Mute, deshalb Peer-Handshake.
4. Line-Ausgang über geeigneten Verstärker, anschließend nur nach Lastnachweis Kopfhörer prüfen.
5. MP3-Stream, dann AAC als getrennte Codec-Gates; TLS/Ringpuffer/Jitter/Netzausfall unter gleichzeitiger Bedienung messen.
6. Spotify-Audio bleibt unabhängig blockiert, bis ein offizieller Empfängerport und dessen Zertifizierungsweg vorliegen.

Audio nicht über den bisherigen 2-Mbaud-UART streamen: Stereo-PCM bei 44,1 kHz/16 Bit würde dessen Nutzkapazität fast ausfüllen. S3 decodiert direkt und treibt I2S; UART führt nur Mute-/Steuerzustand. Bei fehlenden Ressourcen darf der Radioausgang fehlen, ohne Spotify-Connect-Steuerung oder Website zu beschädigen.

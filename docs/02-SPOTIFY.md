# Spotify: Machbarkeit, Produktfreigabe und Adapter

Prüfdatum 2026-09-30. Kein authentifizierter API-Test oder Lautsprechertest durchgeführt. Die folgenden Aussagen trennen dokumentierte Möglichkeiten, offene Freigaben und geplante Implementierung.

## Gate G0: gewünschtes Produkt ausdrücklich klären

Das Produkt ist eine **Hardware-Fernbedienung für bereits vorhandene Connect-Lautsprecher**. Ein Connect-Empfänger spielt selbst Audio. Diese Rollen sind nicht austauschbar. Die öffentlichen Hardware-Regeln verweisen auf das Embedded SDK; eine Ausnahme für diesen Controller wurde nicht gefunden. Das ist keine Ablehnung unseres Projekts, aber auch keine Nutzungsfreigabe. [Hardware-FAQ](https://developer.spotify.com/documentation/commercial-hardware/implementation/faqs)

Auch ein eSDK-Port ist kein belegter Fernsteuerungsweg: Die dokumentierte Play-Funktion holt Wiedergabe zum eigenen Gerät. Für G0 sind Controller-Profil, andere Lautsprecher und erlaubte Eingaben schriftlich zu bestätigen. [Hardware-Buttons](https://developer.spotify.com/documentation/commercial-hardware/implementation/guides/hardware-buttons)

Die Partnerroute setzt eine Organisation, Verträge, Plattformprüfung und Zertifizierung voraus. Keine SDK-Verfügbarkeit, Gebühr, Freigabedauer oder Zulassung für den ESP32-S3 wird zugesagt. Aktuelle Espressif-Unterlagen verweisen für Spotify Connect an den Vertrieb. [Spotify Hardware](https://developer.spotify.com/documentation/commercial-hardware), [Espressif FAQ](https://docs.espressif.com/projects/esp-faq/en/latest/application-solution/audio-development-framework.html#do-espressif-modules-support-spotify-connect)

### Konkretes Klärungspaket, noch nicht versendet

1. Erlaubter Controller ohne eigenen Audioplayer, direkte Steuerung anderer Connect-Geräte und freigegebener API-/SDK-Weg.
2. Kunden-Onboarding ohne eigenes Entwicklerkonto und nach Q01 ausdrücklich ohne eigenen externen Anmelde-/Callbackdienst; zulässigen Rückweg und Refresh klären. Produkt für weitere Nutzer, zunächst zwei Pilotknobs; Mehrnutzer-/Haushaltsmodell und Abonnements.
3. Lokale Konfigurationswebsite, manuelle Spotify-Links, Playlists/Podcast-Presets, Metadaten und Cover; eigene Suche nicht voraussetzen.
4. Zulässige Kombination mit Radioverzeichnis und separatem Internetradioausgang; Hardware-/API-Verträge können unterschiedliche Regeln enthalten.
5. Konkretes S3/ESP-IDF-Toolchainpaket, Speicherbedarf, Eventloop, Netzwerkverhalten und Update-/Zertifizierungspflichten.
6. Optionaler zertifizierter Audioempfänger über Klinke sowie Anforderungen an Lautstärke, Kontowechsel und Disconnect.

**G0 bestanden:** schriftlich bestätigtes Produktprofil mit tatsächlich nutzbarer Integration, SDK/API-Zugang und Testplan. **G0 offen/negativ:** lokale Plattform weiterentwickeln; Spotify-Produktadapter bleibt deaktiviert. Kein Wechsel zu Reverse Engineering, eigener Client-ID pro Kunde oder permanentem Cloudproxy als versteckte Ersatzlösung. Eine andere Hardware oder ein anderes Produktziel braucht einen expliziten neuen Entscheid.

## Adaptergrenze

| Adapter | Status | Zweck |
| --- | --- | --- |
| `DisabledProvider` | Vorgabe des Frameworks | ehrlicher Einrichtungs-/Fehlerzustand |
| `MockProvider` | Entwurfs-/Testkonzept | UI/Fehlerfälle ohne Konto oder Lautsprecher |
| `WebApiEvaluationProvider` | geplanter, begrenzter Labormodus | technische Lautsprecher-/Contentprüfung mit berechtigtem Testkonto |
| `ApprovedControllerProvider` | Vertrag, Implementierung offen | Produktpfad nach G0; Schnittstelle noch nicht durch SDK belegt |
| `PartnerReceiverProvider` | optional, separates Gate | lokale Spotify-Audiowiedergabe; ersetzt den Ferncontroller nicht automatisch |

Zulassungen werden aus geprüftem Build-/Releaseprofil und verifizierten Fähigkeiten abgeleitet, niemals aus einer Benutzeroption `approved=true`. Eine Produktfirmware kann nicht durch Eingabe einer Client-ID in einen Umgehungsmodus wechseln.

## Bedingter Web-API-Evaluierungsvertrag

Für berechtigte Tests: Konto mit Premium für Playback-Steuerung und passend freigeschaltete Testnutzer. Development Mode ist keine tragfähige allgemeine Kundenverteilung. Der Juli-2026-Änderungsstand nennt 25 Client-IDs mit geteilten Entwicklerkontingenten, während ältere Anleitungen noch eine nennen. Die Freigabegrenze bleibt nach aktueller Quotenübersicht bei fünf Nutzern je Entwicklungs-App. Erweiterte Web-API-Quoten sind eine separate Zulassungsroute; sie lösen Hardwaregenehmigungen nicht. [Quota modes](https://developer.spotify.com/documentation/web-api/concepts/quota-modes), [Juli 2026](https://developer.spotify.com/documentation/web-api/references/changes/july-2026)

Basis `https://api.spotify.com/v1`:

| Aufgabe | API | Verhalten |
| --- | --- | --- |
| Geräte | `GET /me/player/devices` | fehlendes/restringiertes Gerät erklären; IDs neu auflösen |
| Zustand | `GET /me/player?additional_types=track,episode` | 204 und unbekannte Typen verarbeiten |
| Ausgabegerät | `PUT /me/player` | exakt ein Ziel, danach Zustand prüfen |
| Play/Pause | `PUT /me/player/play`, `/pause` | explizites Ziel, keine bestätigte Wiedergabe aus 204 ableiten |
| Lautstärke | `PUT /me/player/volume` | nur bei `supports_volume`, absoluter Wert, zusammenfassen |
| Titel/Position | `POST /me/player/next`, `/previous`; `PUT /me/player/seek` | nicht-idempotente Aktionen nach Timeout nicht blind duplizieren |
| Wiedergabeoptionen | `PUT /me/player/shuffle`, `/repeat` | `actions`/Kontextrestriktionen beachten |
| Freigegebene Playlists | `GET /me/playlists`, `/playlists/{id}` | nur vom Benutzer ausgewählte Referenzen speichern |
| Playlist-Inhalt | `GET /playlists/{id}/items` | keine alten `/tracks`-Annahmen |
| Podcasts | `GET /shows/{id}`, `/shows/{id}/episodes`, `/episodes/{id}` | Show = Sammlung, Episode = möglicher Spielinhalt |
| Episodenqueue | `POST /me/player/queue?uri=spotify:episode:…` | belegt nur Queue, nicht sicheren Sofortstart aus Ruhe |

Quellen: [Geräte](https://developer.spotify.com/documentation/web-api/reference/get-a-users-available-devices), [Playback](https://developer.spotify.com/documentation/web-api/reference/get-information-about-the-users-current-playback), [Transfer](https://developer.spotify.com/documentation/web-api/reference/transfer-a-users-playback), [Play](https://developer.spotify.com/documentation/web-api/reference/start-a-users-playback), [Volume](https://developer.spotify.com/documentation/web-api/reference/set-volume-for-users-playback), [Queue](https://developer.spotify.com/documentation/web-api/reference/add-to-queue), [Show-Episoden](https://developer.spotify.com/documentation/web-api/reference/get-a-shows-episodes).

Basis-Scopes: `user-read-playback-state`, `user-modify-playback-state`. Für aktivierte Zusatzfunktionen `playlist-read-private`, gegebenenfalls `playlist-read-collaborative`, `user-read-playback-position` ergänzen. Keine unnötigen E-Mail-/Schreib-/Streaming-Rechte. [Scopes](https://developer.spotify.com/documentation/web-api/concepts/scopes)

Development-Profile liefern Playlistinhalte nur bei Eigentum/Mitarbeit. Die erfolgreiche Metadatenabfrage anderer Playlists belegt keinen verfügbaren Trackbrowser; Playliststart und Einzeltrackbrowser sind getrennte Fähigkeiten. Die neue Antwortstruktur und fehlende `/me`-Felder müssen Parser berücksichtigen. Stable Account-ID nach dem aktuellen Profilvertrag verwenden, keine Anzeigenamen. [Februar-Migration](https://developer.spotify.com/documentation/web-api/tutorials/february-2026-migration-guide), [Mai 2026](https://developer.spotify.com/documentation/web-api/references/changes/may-2026)

### Fehler- und Grenzfälle

- 401: genau eine koordinierte Token-Erneuerung, dann erneute Anmeldung; kein paralleler Refresh-Sturm.
- 403: fehlende Berechtigung, Development-Freischaltung, Kontovoraussetzungen oder eingeschränktes Gerät unterscheiden, soweit Antwort erlaubt; nicht pauschal „Passwort falsch“.
- 404/204: Gerät/Wiedergabe nicht verfügbar, Auswahl nicht automatisch umleiten.
- 429: `Retry-After`, Jitter und globale Warteschlange; `QUOTA_EXCEEDED` kann ein anderes Erholungsverhalten brauchen als kurze Ratelimits. Keine verdeckten festen Quotenwerte. [Rate limits](https://developer.spotify.com/documentation/web-api/concepts/rate-limits)
- Geräteliste ist kein vollständiger LAN-Scan. Manche Connect-Geräte fehlen; Namen sind nicht eindeutig, IDs nicht dauerhaft garantiert. Keine eigene Multiroom-Gruppierung zusagen.
- Podcasts: `spotify:show` nicht blind als `context_uri` senden. Direkter Episodenstart/Resume aus Ruhe ist eine frühe Pflichtprobe. Reines Queue+Next ist kein gleichwertiger Ersatz. Bei fehlender Fähigkeit bleibt eine Show durchsuchbar, die Aktion jedoch erklärt deaktiviert.
- Verfügbarkeit, explizite Inhalte, Region und Kontoabhängigkeiten werden pro Inhalt behandelt. Kein automatisches Abspielen nach Reboot oder Wechsel des Haushalts.

## Anmeldung und lokales Audio

### Käufer melden sich mit ihrem normalen Konto an

Verbindliches Bedienziel: Käufer benötigen **kein Spotify-Entwicklerkonto, keine eigene App-Registrierung und keine Eingabe einer Client-ID, eines Client Secrets oder eines Tokens**. Passion Wave stellt eine für dieses Produkt freigegebene Integration bereit. Der Kunde wählt „Mit Spotify verbinden“, meldet sich auf der echten Spotify-Seite an und erlaubt den Zugriff. Passwörter werden niemals auf der Gerätewebsite abgefragt oder auf dem Knob gespeichert.

Der vorgesehene Web-API-Weg zur Wiedergabesteuerung setzt **Spotify Premium** voraus. Diese Kontovoraussetzung muss schon vor dem Kauf verständlich genannt werden. Normale Spotify-Zugangsdaten genügen für die Anmeldung; ein kostenloses Konto erfüllt damit noch nicht die Voraussetzungen für die Playback-API. Abweichende Partnerbedingungen dürfen erst nach einer konkreten Vereinbarung zugesagt werden. [Start/Resume Playback](https://developer.spotify.com/documentation/web-api/reference/start-a-users-playback)

Der Produkteigentümer hat am 30.09.2026 einen vorhandenen privaten Spotify-Dashboardzugang mitgeteilt. Dessen Inhalt, Quotenmodus und kommerzielle Controller-Freigabe konnten nicht eingesehen werden. Das ist ein möglicher Zugang für die Laborevaluierung; **G0 ist dadurch nicht bestanden**. Auch die Fernsteuerung von Play/Next/Previous zählt laut Spotify zu Streaming-Anwendungen. Die schriftlich bestätigte Nutzung beim Verkauf bleibt erforderlich. [Compliance Tips](https://developer.spotify.com/compliance-tips)

Unter Q01 ist eine **mobile Einrichtungs-App mit PKCE und lokal gesicherter Kopplung** der derzeit aussichtsreichste technische Vorschlag. Spotify unterstützt Custom-Scheme-Rückleitungen weiterhin; damit wäre kein eigener externer Callbackdienst nötig. Zustimmung zu einer verpflichtenden zusätzlichen App wurde angefragt und steht aus. Der verteilte Codeaustausch, sichere Gerätebindung, Plattformunterstützung und Spotify-Freigabe sind noch zu qualifizieren. Die Website bleibt für Einstellungen vorgesehen; eine App löst den sicheren schreibenden Browserzugriff im Heimnetz (G2) nicht automatisch. [PKCE](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow), [Custom Schemes](https://developer.spotify.com/blog/2025-02-12-increasing-the-security-requirements-for-integrating-with-spotify)

Eine normale lokale HTTP-Website auf `192.168…` oder `.local` ist kein zulässiger Spotify-Callback. Loopback `127.0.0.1` meint das Browsergerät, nicht den Knob. Ein Desktopprogramm mit lokalem Loopback ist ein möglicher Laborweg, keine fertige Handy-Einrichtung. HTTPS-App-Links würden zusätzliche öffentlich erreichbare Domain-Zuordnungsdateien benötigen und werden unter Q01 nicht stillschweigend eingeführt. [Redirectregeln](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri)

Seit Juni/Juli 2026 laufen Refresh-Tokens der Developer-Apps sechs Monate nach ursprünglicher Autorisierung ab; Refresh verlängert die Frist nicht. Die erneute Anmeldung gehört deshalb zum regulären Kundenerlebnis: verständlicher Hinweis, gleiche Kontoverknüpfung, vorhandene Einstellungen erhalten. `invalid_grant` beendet Refresh-Versuche und wechselt in „Spotify erneut verbinden“. Die Abnahme muss Ablauf, Widerruf, Kontowechsel, Abbruch und Geräteverkauf/Werksreset abdecken. [Spotify-Änderung](https://developer.spotify.com/blog/2026-06-18-refresh-token-expiration)

PKCE und eSDK-ZeroConf sind verschiedene Pfade; siehe [Onboarding](03-ONBOARDING-WEB.md). Browser-Web-Playback-SDK würde im Telefonbrowser spielen, nicht im ESP32 und nicht an dessen Klinke. Lokale Spotify-Audiowiedergabe benötigt einen separat freigegebenen Empfänger und dessen Audio-/Codec-Ressourcen. [Web Playback SDK](https://developer.spotify.com/documentation/web-playback-sdk)

Die öffentliche eSDK-Speicherempfehlung liegt oberhalb des internen Speichers des classic ESP32; S3-PSRAM hilft nur, wenn das tatsächliche SDK/Port es unterstützt. Erste Integration misst Speicher, Timing und Decoderlast; SDK-Verfügbarkeit bleibt offen. [Technische Anforderungen](https://developer.spotify.com/documentation/commercial-hardware/implementation/requirements/technical)

## Sonos als zusätzliche Ausgabe

Sonos wird zuerst als `spotify_connect`-Ziel getestet. Für diesen Weg bleiben Spotify-Produktgate und Laufzeitrestriktionen bestehen. Ein separater Sonos-Favoritenpfad verwendet künftig eigene Konto-/Inhaltsreferenzen und muss unter seinen tatsächlichen Plattformbedingungen geprüft werden; er ist keine automatische Freigabe der direkten Spotify-API. Architektur und Prüfungen: [Sonos](12-SONOS-PRUEFUNG.md).

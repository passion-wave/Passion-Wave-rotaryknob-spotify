# Onboarding und Konfigurationswebsite

Ziel: möglichst wenige Schritte für Kunden, keinerlei Entwicklerkonto, keine manuelle Tokenverwaltung. Die Oberfläche liegt auf dem S3 und funktioniert für Verwaltung auch ohne Spotify. Der Webentwurf in `web/` demonstriert die Informationsarchitektur; alle Interaktionen sind simuliert.

## 1. Kundenweg

1. Strom anschließen. Display zeigt „Einrichten“ und einen QR-Code für das individuelle Geräte-WLAN. Hersteller hat beide Chips bereits geflasht und geprüft.
2. QR scannen, dem geschützten Setup-WLAN beitreten. Captive-Portal-Assistent oder ausgeschriebene lokale IP öffnet die Website; QR allein öffnet nicht auf jedem Betriebssystem automatisch beides.
3. Heim-WLAN auswählen, Kennwort eingeben. 2,4 GHz erforderlich, versteckte SSID als Zusatzoption. Kennwort darf angezeigt werden, wird aber nie geloggt/exportiert. AP+STA-Verhalten bei Kanalwechsel testen. Verbindungsfehler lassen Setup bestehen und ermöglichen Korrektur.
4. Gerät bestätigt Verbindung und zeigt Namen/IP; Telefon wieder ins Heimnetz wechseln. Verlauf/Onboarding-Status bleibt auf dem Gerät erhalten, nicht nur in einem verlorenen Mini-Browser.
5. „Mit Spotify verbinden“ über **freigegebenen** Anmeldeadapter. Vollständiger Systembrowser oder offizielle Spotify-App, nicht Spotify-Passwort in einem Geräteformular.
6. Verfügbaren Lautsprecher bewusst wählen, Lautstärkefähigkeit anzeigen und kurze Testwiedergabe nach Nutzeraktion. Nichts automatisch in einem anderen Raum starten.
7. Favoriten freigeben: Spotify-Link übernehmen oder freigegebener Preset-Weg. Reihenfolge und Anzeigenamen festlegen.
8. Wetter optional im Wizard aktivieren: Ort oder Koordinaten und Zeitzone bestätigen, Anbieterhinweis und Avatar-Vorschau anzeigen. Wetter kann später eingerichtet werden; ohne Standort bleibt die Seite erklärt unkonfiguriert.
9. „Fertig“ zeigt Bedienhilfe am Knob. Keine neue PassionWave-Wetterkonto-Pflicht; mögliche Providerkeys/Verträge werden vor Produktauslieferung geklärt.

Der erste Alltagsschritt ist Drehen/Touch. Hilfe erscheint an Fehlerstellen, nicht als technisches Handbuch. Buttons heißen „Erneut verbinden“, „Anderen Lautsprecher wählen“, „Update wiederholen“.

## 2. WLAN- und Verwaltungssicherheit

Kein geteiltes Factory-Passwort. Individuelles langes AP-Passwort/QR, maximal ein Verwaltungsclient, begrenztes Fenster (Startwert 10 Minuten), physische Bestätigung. AP nur bei Erstkonfiguration oder expliziter Geste öffnen. Nach Erfolg schließen. Reines WiFi-Provisioning lässt sich mit Espressif SoftAP oder BLE umsetzen; eine eigene Browseroberfläche braucht den passenden gesicherten Protokollclient. Security 2/SRP ist keine automatische Eigenschaft einer HTML-Seite. [Espressif Provisioning](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/provisioning/provisioning.html)

**Ohne zusätzliche Zertifikatsinfrastruktur ist ein abgesicherter, physisch aktivierter Verwaltungs-AP die konservative Basis für schreibende Einstellungen.** Der lokale Server bleibt auf dem Gerät; im Heimnetz kann eine sparsame Statusseite angeboten werden. Ein permanent komfortabler schreibender Browserzugriff im Heimnetz verlangt vertrauenswürdiges HTTPS oder einen gesondert geprüften sicheren Transport. Ein selbstsigniertes Zertifikat mit Warnung und „trotzdem fortfahren“ ist kein freigegebener Kundenweg. Diese Komfortlücke ist Gate G2, nicht bereits gelöst.

Ziel für die Produktphase: Entscheidung zwischen individueller HTTPS-Geräteidentität mit Provisionierung/Erneuerung und einem von Spotify/Plattformen unterstützten Companion-/Pairingweg. Native Helfer bleiben optionaler zu bewertender Zusatz, nicht stillschweigende Voraussetzung. Bei strikter Vorgabe „nur Browser, keinerlei zusätzliche Infrastruktur“ bleibt das geschützte Verwaltungsnetz mit bewusstem WLAN-Wechsel der dokumentierte Kompromiss.

Im AP: Origin-/Host-Prüfung, CSRF-Schutz, zufällige kurze Sitzung, keine offenen CORS-Regeln, keine sensiblen GETs, physische Wiederbestätigung für Reset/Accountwechsel/Update. AP-Passwort und Sitzung geheim halten, nach Besitzwechsel neu erzeugen. Auf gemeinsamem LAN ersetzt ein Besitzcode niemals Transportverschlüsselung.

BLE/Web Bluetooth kann auf unterstützten Browsern Komfort bieten, ist kein iPhone/Safari-Grundweg. Chrome verlangt sicheren Kontext und Nutzeraktion; WebKit implementiert Web Bluetooth nicht allgemein. Öffentliche HTTPS-Seiten mit LAN-Zugriff unterliegen zusätzlichen Berechtigungen; Browser verhalten sich unterschiedlich. [Chrome Bluetooth](https://developer.chrome.com/docs/capabilities/bluetooth), [WebKit](https://webkit.org/tracking-prevention/), [Chrome LAN-Zugriff](https://developer.chrome.com/blog/local-network-access)

## 3. Spotify-Linking: bedingte Varianten

### A. Zugelassener Partneradapter

Partnerregeln haben Vorrang. Bei einem genehmigten eSDK-Empfänger wird die offizielle Spotify-App/ZeroConf-Einrichtung geprüft. Daraus folgt noch kein zulässiges Anmelden eines reinen Fremdlautsprecher-Controllers. Erst Gate G0 legt den tatsächlichen Flow fest. [ZeroConf](https://developer.spotify.com/documentation/commercial-hardware/implementation/guides/zeroconf)

### B. Genehmigter Web-API-Controller / Labortest

Authorization Code mit PKCE S256: Gerät erzeugt zufälliges einmaliges `state`, Verifier und Challenge. Nur das Gerät hält den Verifier. Spotify fordert exakt registrierte HTTPS-Redirects; `.local` oder LAN-IP mit HTTP reichen nicht. Loopback zeigt auf den Browserrechner, nicht auf den Knob. [Redirects](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri), [PKCE](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow)

Für einfache Kundenführung ist als **bewusste Option** ein kleiner HTTPS-Linkingdienst sinnvoll: Gerät legt eine kurzlebige Pairingsitzung an; QR öffnet Spotify; Callback nimmt nur Code und State auf; Gerät holt beides über ausgehendes TLS mit eigenem hochentropischem Abrufgeheimnis und tauscht den Code selbst ein. Dienst hat keinen Verifier und keine Access-/Refresh-Tokens. Einmalverbrauch, kurze TTL, Zustandsbindung, Ratebegrenzung und ohne Code-/Querylogs. Callback setzt CSP, `no-store`, `no-referrer`, keine Analytik und bereinigt die URL. Refresh und Regelbetrieb erfolgen direkt zwischen Gerät und Spotify.

Das wäre ein **externer Dienst nur zur Anmeldung** und deshalb eine explizite Erweiterung der strikten Alles-auf-dem-Gerät-Vorgabe. Er ist weder implementiert noch stillschweigend genehmigt. Ohne ihn: registrierte statische HTTPS-Callbackseite liefert ein einmaliges Code/State-Paket, das in den Geräteeinrichtungsdialog übernommen wird. Diese manuelle Variante ist robust für Entwicklung, aber nicht das beworbene einfache Produkt-Onboarding. Ein nativer temporärer Loopbackhelfer ist ebenfalls nur eine gesondert bewertete Alternative.

Kein erfundener Spotify-Device-Code-Flow, kein Client-Secret in Firmware, keine Refresh-Tokens in Browserstorage. Neue Refresh-Tokens atomar speichern; fehlt ein neuer, vorhandenen behalten. Trennen löscht lokale Credentials und stoppt deren Verwendung; zusätzliche Spotify-App-Berechtigungswiderrufe nur über tatsächlich vorhandenen Providerweg bzw. klare Benutzeranleitung.

## 4. Website-Seiten und vollständige Einstellungen

| Seite | Inhalte |
| --- | --- |
| Einrichtung | Fortschritt, WLAN-Verbindung/Wechsel, Spotify verbinden/trennen, Lautsprecherwahl, Test |
| Inhalte | Playlist-/Podcast-/Episodenlinks, Freigabe, Reihenfolge, Namen, entfernen, Metadatenstatus; Radio-Suche und eigene URLs |
| Wiedergabe | bevorzugtes Gerät, manuelle Gerätewahl, Lautstärkeobergrenze, Schrittweite, Shuffle/Repeat soweit unterstützt, Startverhalten ohne Autoplay |
| Gerät | Name, Sprache, Zeitzone/Zeitstatus, Helligkeit, Nachtgrenze, Dim-/Fadezeiten, Display-Aus getrennt nach Playback, Haptik/Effekt, Drehrichtung/Schrittweite, Auto-Cover, Netzstatus |
| Wetter und Avatar | Aktiv, Standort/Koordinaten, Zeitzone, Anbieter/Quellenzeit, Einheiten, Radarstatus, Attribution, Haarwahl und Morgenautomatik; keine vorgetäuschte Radar-ETA |
| Updates | installierte/angebotene Produktversion, beide Chips, Kanal stabil/beta, Notizen, signiertes Paket, Prüfung, Start, Fortschritt, Recovery |
| Erweitert / Hilfe | Konfigurationsexport ohne Geheimnisse, geprüfter Import, Diagnose ohne Tokens, Besitzerwechsel/Factory Reset, Browser-/WLAN-Hilfe |

Grundlegende Werte zuerst, seltene Parameter unter „Erweitert“. Keine editierbaren Pinbelegungen, Zertifikatssignierschlüssel oder Sicherheitsfreigaben für Endnutzer. Technische Diagnose nur bei Bedarf. Schreibzugriffe vergleichen Konfigurationsrevision und melden Konflikte statt fremde Änderungen zu überschreiben. Änderungen in einer Sitzung sammeln und atomar speichern; Flash nicht bei jedem Drehereignis schreiben.

Responsive 360-px-Handy bis Desktop, Tastaturbedienung, sichtbarer Fokus, Beschriftungen und verständliche Fehler, Status nicht nur durch Farbe, reduzierte Bewegung berücksichtigen. Auf dem runden Display bleiben häufige Ziele im sicher erreichbaren Innenbereich. Keine externen Fonts/CDNs nötig.

## 5. Verbindungs- und Wiederaufnahmefälle

- Falsches WLAN: Formular bleibt zugänglich; kein Factory Reset nötig.
- Kein Internet/Zeit/DNS: lokale Einrichtung bleibt erhalten; genaue nächste Aktion.
- Gastnetz mit Client-Isolation: erklären, dass Lautsprecher oder Browserkommunikation eingeschränkt sein können.
- OAuth abgebrochen/abgelaufen: Vorgang neu starten, bisheriges gültiges Konto erst nach neuer erfolgreicher Verknüpfung ersetzen.
- Mehrere Knobs/Browser: Produkt-ID am Display vergleichen; Sitzung an dieses Gerät binden.
- Smartphone schläft oder schließt Tab: Update und Verbindungsprüfung laufen auf dem Gerät; Browser liest später Status neu.
- Eigentümerwechsel: komplette lokale Kontotrennung und neue Verwaltungsschlüssel.

## 6. Sonos-Onboarding als geplante Erweiterung

Die [Sonos-Prüfung](12-SONOS-PRUEFUNG.md) ergänzt eine explizite Ausgaberoute. Spotify Connect nutzt den vorhandenen Verknüpfungsweg; ein freigegebener Sonos-Adapter erhält eigene Haushalts-/Raumwahl und erlaubte Favoriten. Aktuelle Gruppenmitglieder sind sichtbar, bevor die Wiedergabe auf zusätzliche Räume ausgeweitet wird. Solange lokale Authentifizierung/Lizenz ungeklärt sind, zeigt die Website keine funktionierende Sonos-Verknüpfung an. Die Sonos-Cloud-Variante braucht einen eigenen sicheren Authentifizierungs-/Refresh-Entscheid; der einmalige Spotify-PKCE-Callbackentwurf ist hierfür nicht übertragbar.

## 7. Wetterkonfiguration und vollständiger Parameterport

Die Tabellen in [Featureportierung](13-FEATURE-PORTIERUNG.md) sind die Prüfliste für alle übernommenen Parameter. Die heutigen JSON-Verträge enthalten noch nicht sämtliche Display-/Wetteroptionen; P3/P6/P10 erweitern Schema, Defaults, Migration, HTTP-API und UI gemeinsam. Der Morgenavatar übernimmt zunächst Ein/Aus und 06–10 Uhr; frei editierbare Fenster sind eine gesonderte Erweiterung.

Direkt angefragte Wetteranbieter erhalten IP und Standortparameter; Ortssuche ist ein eigener Dienst. Stadt-/Rasterpräzision genügt soweit der Provider unterstützt, keine laufende GPS-Ortung. Providerzugänge liegen getrennt von exportierbaren Einstellungen; Standort im Supportexport optional auslassen. Ein Standortwechsel verwirft alte Forecast-, Radar- und Avatarjobs. Siehe [Wetter](14-WETTER.md).

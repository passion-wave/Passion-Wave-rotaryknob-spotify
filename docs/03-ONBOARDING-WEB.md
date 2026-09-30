# Onboarding und Konfigurationswebsite

Ziel: möglichst wenige Schritte für Kunden, keinerlei Entwicklerkonto, keine manuelle Tokenverwaltung. Die Oberfläche liegt auf dem S3 und funktioniert für Verwaltung auch ohne Spotify. Der Webentwurf in `web/` demonstriert die Informationsarchitektur; alle Interaktionen sind simuliert.

**Bestätigte Produktentscheidungen:** Kein eigener externer Anmeldedienst (Q01); Konfiguration jederzeit im Heim-WLAN ohne WLAN-Wechsel (Q02). Die [Antworten](15-OFFENE-PRODUKTENTSCHEIDUNGEN.md) legen das Ziel fest, noch keinen funktionierenden Anmelde-/HTTPS-Weg. Der Geräte-AP bleibt für Erstsetup und Recovery, nicht als alleiniger Alltagszugriff.

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

**Sicherer schreibender Browserzugriff im Heim-WLAN ist nach Q02 Pflicht.** Eine bloße Statusseite im Heimnetz und ein WLAN-Wechsel für Einstellungen erfüllen die Vorgabe nicht. Der Server bleibt auf dem Gerät; vertrauenswürdiges HTTPS oder ein gesondert geprüfter sicherer Transport müssen nachgewiesen werden. Ein selbstsigniertes Zertifikat mit Warnung und „trotzdem fortfahren“ ist kein freigegebener Kundenweg. Diese technische Lücke bleibt Gate G2.

P4.3 prüft einen konkreten Browserweg einschließlich Geräteidentität, lokaler Namensauflösung, vertrauenswürdigem Transport, Erstpaarung, Erneuerung, Routerwechsel und Besitzwechsel auf iOS/Android/Desktop. Individuelle Zertifikate sind erst mit belegtem Bereitstellungs-/Erneuerungsweg eine Lösung. Ein eigener Anmelde-/Callbackdienst ist ausgeschlossen; zusätzliche Zertifikatsinfrastruktur oder eine verpflichtende native Helfer-App sind nicht automatisch genehmigt. Falls der gewünschte Browserweg so nicht gelingt, den konkreten Befund und nötigen Produktkompromiss vorlegen. Kein stiller Rückfall auf Geräte-WLAN im Alltag. Der abgesicherte AP bleibt ein eigener Setup-/Recoveryweg.

Im AP: Origin-/Host-Prüfung, CSRF-Schutz, zufällige kurze Sitzung, keine offenen CORS-Regeln, keine sensiblen GETs, physische Wiederbestätigung für Reset/Accountwechsel/Update. AP-Passwort und Sitzung geheim halten, nach Besitzwechsel neu erzeugen. Auf gemeinsamem LAN ersetzt ein Besitzcode niemals Transportverschlüsselung.

BLE/Web Bluetooth kann auf unterstützten Browsern Komfort bieten, ist kein iPhone/Safari-Grundweg. Chrome verlangt sicheren Kontext und Nutzeraktion; WebKit implementiert Web Bluetooth nicht allgemein. Öffentliche HTTPS-Seiten mit LAN-Zugriff unterliegen zusätzlichen Berechtigungen; Browser verhalten sich unterschiedlich. [Chrome Bluetooth](https://developer.chrome.com/docs/capabilities/bluetooth), [WebKit](https://webkit.org/tracking-prevention/), [Chrome LAN-Zugriff](https://developer.chrome.com/blog/local-network-access)

## 3. Spotify-Verknüpfung ohne eigenen externen Dienst

Q01 schließt einen eigenen PassionWave-Linkingdienst ausdrücklich aus. Der ausgewählte Weg muss Einrichtung, erneute Anmeldung, Widerruf und Refresh ohne diesen Dienst beherrschen. Direkte Kommunikation mit Spotify und dessen freigegebene Anmeldedienste bleibt Bestandteil des gewünschten Produkts.

### A. Zugelassener Partneradapter

Partnerregeln haben Vorrang. Bei einem genehmigten eSDK-Empfänger wird die offizielle Spotify-App/ZeroConf-Einrichtung geprüft. Daraus folgt noch kein zulässiges Anmelden eines reinen Fremdlautsprecher-Controllers. Erst Gate G0 legt den tatsächlichen Flow fest. [ZeroConf](https://developer.spotify.com/documentation/commercial-hardware/implementation/guides/zeroconf)

### B. Genehmigter Web-API-Controller / Labortest

Authorization Code mit PKCE S256: Gerät erzeugt zufälliges einmaliges `state`, Verifier und Challenge. Nur das Gerät hält den Verifier. Für Web-Rückleitungen verlangt Spotify HTTPS; `.local` oder LAN-IP mit HTTP reichen nicht. Loopback zeigt auf den Browserrechner, nicht auf den Knob. Mobile Custom Schemes werden weiterhin unterstützt; eine zusätzliche Einrichtungs-App ist als Variante angefragt, aber noch nicht ausgewählt. Sichere lokale Gerätebindung und Produktfreigabe müssen auch dann nachgewiesen werden. [Redirects](https://developer.spotify.com/documentation/web-api/concepts/redirect_uri), [PKCE](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow), [App-Rückleitungen](https://developer.spotify.com/blog/2025-02-12-increasing-the-security-requirements-for-integrating-with-spotify)

Die HTTPS-Redirectanforderung ist unter Q01 eine offene technische Hürde, keine bereits gelöste automatische LAN-Rückgabe. P4.4 muss nach G0 einen tatsächlich zugelassenen Partner-/Geräteflow oder einen passenden direkten Rückweg unter den bestätigten Produktvorgaben nachweisen. Eine erreichbare lokale Konfigurationsseite allein löst den Spotify-Callback nicht.

**Verworfene bisherige Variante:** der kleine externe HTTPS-Linkingdienst mit kurzlebiger Code-/State-Übergabe. Auch eine eigene extern gehostete statische Callbackseite wird nicht als Ausweichlösung eingeplant. Manuelles Code-/Tokenkopieren ist kein fertiges Kunden-Onboarding; ein nativer Loopbackhelfer wäre eine zusätzlich zu entscheidende Produktabhängigkeit. Ohne passenden Nachweis bleibt der Live-Anmeldepfad offen, statt einen Dienst oder eine App still einzuführen.

Kein erfundener Spotify-Device-Code-Flow, kein Client-Secret in Firmware, keine Refresh-Tokens in Browserstorage. Neue Refresh-Tokens atomar speichern; fehlt ein neuer, vorhandenen behalten. Trennen löscht lokale Credentials und stoppt deren Verwendung; zusätzliche Spotify-App-Berechtigungswiderrufe nur über tatsächlich vorhandenen Providerweg bzw. klare Benutzeranleitung.

## 4. Website-Seiten und vollständige Einstellungen

| Seite | Inhalte |
| --- | --- |
| Einrichtung | Fortschritt, WLAN-Verbindung/Wechsel, Spotify verbinden/trennen, Lautsprecherwahl, Test |
| Inhalte | Playlist-/Podcast-/Episodenlinks, Freigabe, Reihenfolge, Namen, entfernen, Metadatenstatus; Radio-Suche und eigene URLs |
| Wiedergabe | bevorzugtes Gerät, manuelle Gerätewahl, Lautstärkeobergrenze, Schrittweite, Shuffle/Repeat soweit unterstützt, Startverhalten ohne Autoplay |
| Gerät | Name, Sprache, Zeitzone/Zeitstatus, Helligkeit, Nachtgrenze, Dim-/Fadezeiten, Display-Aus getrennt nach Playback, Haptik/Effekt, Drehrichtung/Schrittweite, Auto-Cover, Netzstatus |
| Wetter und Avatar | Aktiv, Deutschland-Standort/Koordinaten, Zeitzone, Quelle/Quellenzeit, Einheiten, Attribution, Haarwahl und Morgenautomatik; Radar als Nachlieferung kenntlich, erst nach Freischaltung echte Radarparameter |
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

Die [Sonos-Prüfung](12-SONOS-PRUEFUNG.md) beginnt mit Roam und Move als Connect-Zielen. Ein später freigegebener nativer Sonos-Adapter erhält eigene Haushalts-/Raumwahl und erlaubte Favoriten. Aktuelle Gruppenmitglieder sind sichtbar, bevor die Wiedergabe auf zusätzliche Räume ausgeweitet wird. Solange lokale Authentifizierung/Lizenz ungeklärt sind, zeigt die Website keine funktionierende Sonos-Verknüpfung an. Sonos Cloud mit eigenem Auth-/Refreshdienst ist unter den bestätigten Vorgaben keine aktive Alternative.

## 7. Wetterkonfiguration und vollständiger Parameterport

Die Tabellen in [Featureportierung](13-FEATURE-PORTIERUNG.md) sind die Prüfliste für alle übernommenen Parameter. Die heutigen JSON-Verträge enthalten noch nicht sämtliche Display-/Wetteroptionen; P3/P6/P10 erweitern Schema, Defaults, Migration, HTTP-API und UI gemeinsam. Der Morgenavatar übernimmt zunächst Ein/Aus und 06–10 Uhr; frei editierbare Fenster sind eine gesonderte Erweiterung.

Direkt angefragte Wetteranbieter erhalten IP und Standortparameter; Ortssuche ist ein eigener Dienst. Stadt-/Rasterpräzision genügt soweit der Provider unterstützt, keine laufende GPS-Ortung. Providerzugänge liegen getrennt von exportierbaren Einstellungen; Standort im Supportexport optional auslassen. Ein Standortwechsel verwirft alte Forecast-, Radar- und Avatarjobs. Siehe [Wetter](14-WETTER.md).

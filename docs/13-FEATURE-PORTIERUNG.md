# Featureweise Umsetzung: Bestand → eigenständiger RotaryKnob

Stand: 30. September 2026. Referenz: `3.0.1-beta.16`, Commit `9cc5576c2fd4a9beb56cad24e211957c43aa2c62`. Wetter ist auf ausdrücklichen Wunsch enthalten. Dies ist eine **Planungs- und Übernahmetabelle**, kein Implementierungsnachweis. Für Reihenfolge, Aufwände und Abhängigkeiten gilt der [Implementierungsplan](08-IMPLEMENTIERUNGSPLAN.md), für Produktgates die [Abnahme](09-ABNAHME.md).

**Art:** P = Verhalten/Algorithmen portieren; A = vorhandene Funktion mit neuer Anbindung anpassen; N = neu gegenüber dem belegten Bestand; B = bedingte Erweiterung nach eigenem Gate; X = bewusst nicht übernehmen. Auch P benötigt die Ablösung von ESPHome-Bindungen und echte Hardwaretests. Alle Umsetzungen sind offen. Q-Kürzel verweisen auf die Quellkarte am Ende; „Zielauftrag“ bezeichnet neue Nutzeranforderungen. Prüf-IDs bezeichnen Soll-Abnahmen, keine heutigen Erfolge.

**Beschlossene Lieferstaffelung:** Die erste Version umfasst Spotify-Steuerung nach Produktfreigabe, jederzeitige Konfiguration im Heim-WLAN, Senderverwaltung sowie Wettervorhersage, Wetterbilder und Avatar für Deutschland. Wetter- und Radarversorgung dürfen keine laufenden Anbietergebühren verursachen. Radar einschließlich der noch zu qualifizierenden Regenmetadaten bleibt im Gesamtport und wird anschließend vervollständigt; tatsächliche Radioausgabe ist eine spätere Erweiterung. Der Pilot startet mit zwei Knobs im überwiegenden USB-Betrieb. Die beantworteten Q01–Q09 stehen im [Entscheidungsregister](15-OFFENE-PRODUKTENTSCHEIDUNGEN.md); jeder gelieferte Umfang benötigt seine eigenen technischen Nachweise.

## 1. Eingabe und Oberfläche

| ID | Funktion / Herkunft | Art | Problem | Umsetzung kurz | Paket / Abnahme |
| --- | --- | --- | --- | --- | --- |
| I01 | Runddisplay 360×360, QSPI/LVGL (Q1/Q2) | P | Boardrevision, Treiber-/LVGL-Abhängigkeit | Panelinitialisierung isolieren; native Geometrie und feste Objekte portieren | P2.3 / HW-01 |
| I02 | EC1-Dreherkennung (Q3) | P | Zwei Richtungsimpulse, kein Quadraturencoder | PCNT/Filter/Batching übernehmen; GPIO-Profil validieren | P2.3/P3.2 / HW-02 |
| I03 | EC2-Diagnose (Q3/Q6) | A | Zweiter Sensor darf Eingabe nicht verdoppeln | Nur Companion-Diagnose, nie zur EC1-Aktion addieren | P2.3 / HW-02 |
| I04 | Kontextabhängiges Drehen (Q2) | P | Ring darf im Popup nicht Lautstärke ändern | Aktive Ansicht/Modal erhält exklusiv den Ring | P3.2 / UI-04 |
| I05 | Touch, Swipe, Long-Press, Zurück (Q1/Q2) | P | Doppelte Klicks und Aktionen durch Overlays | Feste Hitboxen, Ereignis-Deduplikation und klare Priorität | P3.2 / UI-04 |
| I06 | Erste Berührung/Drehung weckt (Q2) | A | Wake könnte bereits Musik/Lautstärke ändern | Erstes Ereignis konsumieren; Folgereignis normal behandeln | P3.5 / SYS-02 |
| I07 | Haptik, Ein/Aus und Effektwahl (Q2) | P | Haptiklast oder Vibration ohne Aktion | Lokaler DRV2605, begrenzte Rate, Effekt nach angenommener UI-Aktion | P3.2/P6.6 / UI-05 |
| I08 | Navigation/Optionen auf rundem Display (Q1) | A | Alte Hauptseiten enthalten Licht/HA | Medien, Wetter, Uhr und System; Bibliothek/Ausgabe gezielt erreichbar | P3.3/P10.4 / UI-04 |
| I09 | Sofortige Anzeige beim Drehen (Q1/Q2) | P | TLS/Bilder dürfen LVGL nicht blockieren | Kleine Events, Delta-Rendering, Netzwerk-/Decoderworker getrennt | P3/P5/P10 / UI-01 |

## 2. Medienbedienung und Zustand

| ID | Funktion / Herkunft | Art | Problem | Umsetzung kurz | Paket / Abnahme |
| --- | --- | --- | --- | --- | --- |
| M01 | Titel, Interpret/Show, Quellstatus (Q1/Q4) | A | Bisher HA-Snapshot; Teilupdates widersprechen sich | Ein atomarer Provider-Snapshot mit Konto/Ziel/Trackgeneration | P3.1/P5.5 / UI-02 |
| M02 | Fortschritt, vergangene/Gesamtdauer (Q1/Q2) | A | Ohne Dauer/bei Pause keine falsche Bewegung | Monotone Fortschreibung ab bestätigtem Anker; Radio ohne Scheindauer | P5.4 / SP-07 |
| M03 | Play/Pause (Q4) | A | HA-Service entfällt, Capability/Timeout | Typisierten Providerbefehl senden; Soll und bestätigten Status trennen | P5.1 / SP-01/SP-04 |
| M04 | Vorheriger/nächster Titel (Q4) | A | Retry kann doppelt weiterspringen | Serialisieren; nach unklarem Ausgang erst Zustand abgleichen | P5.3 / SP-04 |
| M05 | Ringlautstärke, Prozent/Arc (Q2/Q4) | A | Viele Impulse, späte Werte springen zurück | Lokale Vorschau, absolute Zielwerte bündeln, Bestätigung nachführen | P5.1/P5.4 / UI-01/SP-04 |
| M06 | Lautstärkeschritt/-grenze, Drehrichtung (Zielauftrag) | N | Konto/App können Grenze umgehen | Geräteeinstellungen begrenzen Knob-Befehle; keine Kontosperre behaupten | P3/P6.6 / SYS-01/SP-02 |
| M07 | Shuffle (Q4) | A | Lokaler Toggle könnte trotz Fehler aktiv bleiben | Capability prüfen, explizit setzen, Rückmeldung darstellen | P5.3 / SP-07 |
| M08 | Repeat einzelner Titel/aus (Q4) | A | Genau dieses Verhalten ist belegt | One/Off portieren; Providerzustand ist maßgeblich | P5.3 / SP-07 |
| M09 | Repeat gesamter Kontext (Zielvertrag) | N | Kein belegter Bestandsmodus | Nur nach Providerfähigkeit als zusätzlichen Modus anzeigen | P5.3 / SP-07 |
| M10 | Seek/Scrubbing (Zielvertrag) | N | Alte Fortschrittsleiste ist nur Anzeige | Optionaler expliziter Seek, Position/Capabilities validieren | P5.3 / SP-08 |
| M11 | Neueste Inhaltsauswahl gewinnt (Q4/Q5) | A | Alte Requests können neuere Auswahl überschreiben | Generationen und begrenzte Commandqueue; alte Antworten verwerfen | P3.1/P5.4 / SP-04 |
| M12 | Ladefeedback, Start unklar, Retry (Q2/Q4) | A | Bestand bestätigt nur einzelne Tracks beobachtet | Für alle Aktionen angenommen/bestätigt/unklar/Fehler unterscheiden | P5.4 / SP-03/SP-04 |
| M13 | Fremde App-/Controllerbedienung (Q4) | A | Knob darf fremden Zustand nicht zurückdrehen | Statusabgleich übernimmt fremde Änderungen, kein Besitzkampf | P5.4 / SP-04 |
| M14 | Boot/Reconnect ohne Autoplay (Q4/Q6) | A | HA-Rehydration entfällt; Cache wirkt sonst aktuell | UI lokal starten, Snapshot als alt markieren, Konto/Ziel frisch prüfen | P3/P5.6 / NET-01/SP-02 |

## 3. Cover und Bilddarstellung

| ID | Funktion / Herkunft | Art | Problem | Umsetzung kurz | Paket / Abnahme |
| --- | --- | --- | --- | --- | --- |
| C01 | Cover in Medienansicht (Q2/Q4) | A | MA-Bildproxy und Bridge entfallen | Begrenzter direkter S3-Download/Decoder; Größen-, MIME- und Pixelprüfung | P5.5 / UI-02 |
| C02 | Große Coveransicht mit Text/Batterie (Q2) | A | Bisherige Beschneidung nicht automatisch Spotify-konform | Freigegebenes Bildlayout, Text-/Statuslayer und Platzhalter | P3.4/P5.5 / UI-06 |
| C03 | Automatische Coveransicht bei Playback-Idle (Q2) | A | Darf Auswahl/Morgenavatar/OTA nicht verdrängen | Zentraler Ansichtscheduler, konfigurierbare Verzögerung | P3.4/P10.5 / UI-06/WT-09 |
| C04 | Kurz sichtbarer Lautstärkering im Cover (Q2) | P | Nicht bei jedem Detent ganze Ansicht neu zeichnen | Arc lokal ändern, Ausblendfrist erneuern | P3.3 / UI-01/UI-06 |
| C05 | Coverfallback/Backoff (Q2) | A | Fehlerhafte Bilder, Speicher und Wiederholungen | Neutraler Platzhalter, Abbruch/Backoff, kleiner flüchtiger Cache | P5.5 / UI-02 |
| C06 | Bild+Text atomar und alte Jobs verwerfen (Q2/Q4) | A | Cover/Wetter/Avatar/Radar konkurrieren | Gemeinsamer priorisierter Assetworker, inaktiver Buffer, Generationsswap | P3/P5/P10/P11 / PERF-01 |

## 4. Bibliothek, Playlists und Podcasts

| ID | Funktion / Herkunft | Art | Problem | Umsetzung kurz | Paket / Abnahme |
| --- | --- | --- | --- | --- | --- |
| L01 | Tabs Titel/Playlists/Radio/Podcasts (Q2/Q5) | A | MA-Kategorien entfallen | Lokale erlaubte Referenzen; leere Kategorien verbergen | P6.1/P6.2 / CAT-01 |
| L02 | Virtuelle Zeilen, Touchscroll und Ringfokus (Q2) | P | Lange Listen passen nicht in RAM/Display | Feste Zeilen wiederverwenden, Auswahl sichtbar nachführen | P6.2 / CAT-02 |
| L03 | Paging/Prefetch nahe Listenende (Q5) | A | Spotifyseiten/-rechte anders als MA | Begrenzter Seitencache, Revision/Generation, keine Komplettbibliothek | P6.2 / CAT-02 |
| L04 | Freischalten, Namen, Reihenfolge (Q4/Zielauftrag) | A/N | Alte Filter in HA; Ziel hat eigene Katalogautorität | Website mit atomarer eigener Freigabeliste, Backend prüft jeden Start | P6.1/P6.6 / CAT-01/WEB-01 |
| L05 | Spotifylinks/URIs eingeben (Zielauftrag) | N | Ungültige Hosts/Typen und fremde IDs | Normalisieren/validieren, Typ trennen, Rechte am Provider prüfen | P6.3 / CAT-01 |
| L06 | Playlist als Ganzes starten (Q2/Q4) | A | Einzeltrackstart würde Queue verändern | Erlaubten Playlistkontext starten; nicht Track 0 als Ersatz abspielen | P6.3 / SP-05 |
| L07 | Playlist-Titel anzeigen/auswählen (Q4/Q5) | A | Manche Kontexte liefern keine Tracklisten | Trackseiten/Offset nur bei Fähigkeit; Kontextstart separat erhalten | P6.3 / SP-05/CAT-02 |
| L08 | Podcastkategorie (Q5) | A | Show ist nicht direkt eine Episode | Freigegebene Shows als Sammlungen führen | P6.4 / SP-06 |
| L09 | Episodenbrowser und Resume (Zielauftrag) | N | Nicht als durchgehende Bestandsfunktion belegt | Eigenes Episodenmodell; direkten Start/Position separat nachweisen | P6.4 / SP-06 |
| L10 | Grenzen und entfernte Inhalte (Q4/Zielvertrag) | A | Früher bis 500 Filtereinträge, jetzt 64 Favoriten | Begrenzung offen anzeigen; interne Inhalte paginiert, Entzug sofort wirksam | P6.1 / CAT-03 |
| L11 | Herz-/Like-Anzeige (Q2) | X/B | Bestand ändert nur ein lokales Flag | Kein Spotify-Like versprechen; lokale Webfavoriten decken Freigabe ab | Nicht Kern; CAT-01 |
| L12 | Queueeditor/Enqueue (kein Bestandseditor) | B | Befehlsqueue ist keine Musikqueue | Spätere neue Funktion mit gesonderten Providerrechten | Nicht Kern / G0/G3 |

## 5. Ausgabegeräte und Radio

| ID | Funktion / Herkunft | Art | Problem | Umsetzung kurz | Paket / Abnahme |
| --- | --- | --- | --- | --- | --- |
| O01 | Ein bevorzugter Player (Q4) | A | HA-Entity-ID ist keine Connect-ID | Providerreferenz speichern, wieder auflösen und sichtbar bestätigen | P5.2 / SP-02 |
| O02 | Geräte suchen/wechseln (Zielauftrag) | N | Nicht jedes Gerät sichtbar; Namen nicht eindeutig | Tatsächlich gemeldete Geräte/Flags anzeigen, explizite Zielwahl | P5.2 / SP-01/SP-02 |
| O03 | Sonos als Connect-Ausgabe (Zielauftrag) | N | Herstellername beweist keine Steuerbarkeit | Konkrete Modelle/Standby/Gruppe über Spotify testen | P5.6 / SON-01 |
| O04 | Native Sonos-Favoriten/-Gruppen/-Radio | B | LAN-Zugang, Rechte und Gruppendynamik offen | Separater Adapter/Playeranker, genau eine Steuerroute | Sonos-Zweig / SON-02–08 |
| O05 | Lautsprecher-Power/Mute (Q2 lokal / neu) | X/B | Kein echter Powerbefehl im Bestand | Pause und eigenes Display-Aus getrennt; echte Gerätefunktion nur nach Nachweis | Nicht Kern / SP-02 |
| R01 | Radioeinträge auswählen (Q4/Q5) | A | MA liefert keine Sender mehr | Geräteeigene Liste mit stabilen IDs/Labels | P6.5 / CAT-01/RAD-02 |
| R02 | Radio-Browser-Suche (Zielauftrag) | N | Spiegel/Daten können ausfallen | Begrenzte Suche mit Spiegelwechsel, Ergebnis bewusst übernehmen | P6.5 / RAD-01 |
| R03 | Eigene Stream-URL (Zielauftrag) | N | Redirects, interne Ziele, Playlisten/Codec | URL-/DNS-/Redirectprüfung und begrenzte Probe | P6.5 / RAD-03 |
| R04 | Radio tatsächlich hören (Q4 bisher MA) | A/B | Connect spielt keine freien Radio-URLs | Spätere Erweiterung nach Q03: qualifizierter Sonos-Radiopfad oder optional lokaler Decoder | P8/Sonos / RAD-02/AUD-02/SON-05 |
| R05 | Senderliste speichern ohne Radioausgang | N | Gespeichert darf nicht spielbar bedeuten | Erste Version nach Q03: Liste verwalten; Wiedergabe mit verständlichem Grund deaktivieren | P6.5 / RAD-02 |

## 6. Lokales System, Energie und Diagnose

| ID | Funktion / Herkunft | Art | Problem | Umsetzung kurz | Paket / Abnahme |
| --- | --- | --- | --- | --- | --- |
| S01 | Uhr/Datum/Zeitzone (Q1/Q2/Q6) | A | HA-Zeit entfällt; DST/ungültige Uhr | SNTP/UTC plus lokale Zeitzone, monotone Laufzeit getrennt | P3.5 / SYS-04 |
| S02 | Helligkeit/Dimmen/Fade (Q2) | P | Alte feste Werte sollen einstellbar werden | Gemeinsame Setter für Website/Display, PWM-Fade statt UI-Schleife | P3.5/P6.6 / SYS-02 |
| S03 | Display-Aus je Playback/Idle (Q2/Q7) | A | S3 ist künftig auch Server | USB-Normalbetrieb nach Produktentscheidung Q07: Display/LVGL schlafen, Website und Steuerung bleiben erreichbar | P3.5 / NET-02 |
| S04 | Expliziter Tiefschlaf/Wake (Q2/Q7) | A | Website/Spotify/Wetter dann offline | Explizites Offlineprofil; im USB-Normalbetrieb nicht automatisch aktivieren, Wake und Zustandsneuladung prüfen | P3.5/P10.5 / SYS-02/WT-09 |
| S05 | Batterie/Ladesymbol (Q2) | A | ADC ist kein Fuel Gauge/Chargerstatus | Kalibrierte Schätzung anzeigen; OTA-Energiegate separat messen | P2/P3.5/P7 / HW-03 |
| S06 | DEV Wachhalten (Q2/Q7) | P | Verbraucht Akku, beeinflusst Morgenansicht | Diagnoseoption, Default aus, definierte Schedulerpriorität | P3.5/P10.5 / SYS-02/WT-09 |
| S07 | Persistente Parameter (Q2/Q4) | A | Alte verzögerte Writes können verloren gehen | Versionierter atomarer Save; Erfolg erst nach dauerhaftem Commit | P3.6 / SYS-01 |
| S08 | Offline-Demo (Q2) | A | Automatische Demo würde Ausfall verschleiern | Explizit aktivierbarer, klar markierter Showroommodus | P3/P6.6 / SYS-03 |
| S09 | System-/Netz-/Versionsanzeige (Q2/Q4) | A | HA-Verbindung ist kein neuer Healthstatus | WLAN, Internet, Auth, Ziel, Wetteralter und Peer getrennt anzeigen | P3/P6.6 / SYS-05 |
| S10 | Supportdiagnose/Zähler (Q3/Q4/Q6) | A | Logging kann Echtzeit stören/Secrets leaken | Kleine Ringpuffer, zeitbegrenzter Supportmodus, redigierter Export | P3/P6.6 / SEC-02/PERF-01 |
| S11 | Mehrere Knobs (Q4/Zielauftrag) | A | Konto-/Standort-/Peerzustände vermischen | Individuelle Identität, gerätegebundene Sessions und Referenzen | P4/P9 / PILOT-02 |

## 7. Onboarding und Konfigurationswebsite

| ID | Funktion / Herkunft | Art | Problem | Umsetzung kurz | Paket / Abnahme |
| --- | --- | --- | --- | --- | --- |
| B01 | Werksidentität/Erstinstallation (Q8) | A | Alte Zwei-Endpunkt-/HA-Einrichtung entfällt | Hersteller flasht ein Paar; individueller S3-QR/Setupzugang | P2/P4.1/P9 / ON-01 |
| B02 | WLAN-Scan/Join/Routerwechsel | N | Handy wechselt AP/Heimnetz | Wiederaufnehmbarer Wizard, Scan/manuelle SSID, AP+STA-Fehlerhilfe | P4.1 / NET-01/ON-01 |
| B03 | Geschützte lokale Administration | N | Sicherer Browser-/Pairingweg im Heim-WLAN noch offen | Jederzeitiger LAN-Zugriff ohne WLAN-Wechsel gemäß Q02; vertrauenswürdigen HTTPS-/Pairingweg vor Freigabe nachweisen, Setup-AP für Einrichtung/Recovery | P4.2/P4.3 / G2/SEC-01 |
| B04 | Spotify verknüpfen/trennen | N | Produktzugang und Anmeldung ohne eigenen externen Dienst noch offen | Gemäß Q01 nur genehmigten Weg ohne PassionWave-Verknüpfungsdienst qualifizieren; kein Passwort-/Tokenformular, keine erfundene lokale OAuth-Freigabe | P4.4 / G0/ON-02 |
| B05 | Tokenpflege und Kontoersatz | N | Refresh-/Abbruch-Races, gemeinsame Secrets | Ein Refreshjob, atomare Credentials, Konto erst nach Erfolg ersetzen | P4.5 / ON-03/SEC-02 |
| B06 | Alle Portparameter lokal bearbeiten | A/N | Bisher HA-Entitäten/compile-time Werte | Geräteeinstellungen in gemeinsamer API, Grundwerte zuerst | P6.6/P10.6 / WEB-01 |
| B07 | Inhalte freischalten/reihen/entfernen | A/N | HA-Optionsflow entfällt | Playlist/Show/Episode/Radio getrennt verwalten und validieren | P6.6 / CAT-01/WEB-01 |
| B08 | Wetterort/Provider/Avatar konfigurieren | N/A | Bisher HA-Entity und zwei S3-Avatarparameter | Standort-/Quellen-/Avatarseite; Secretfelder nur schreiben | P10.1/P10.6 / WT-05/WEB-01 |
| B09 | Export/Import und Schreibkonflikte | N | Secrets/Standort, gleichzeitige Änderungen | Revisionsprüfung, Schema/Migration, Secretfreiheit, Standortexport bewusst | P3.6/P6.6 / SEC-02/SYS-01 |
| B10 | Besitzerwechsel/Factory Reset | A/N | Zugangsdaten überleben sonst Besitzer | WLAN/Konten/Standort/Sessions löschen; Trustanker/Hersteller-ID behalten | P4.5/P9 / SEC-02 |
| B11 | Smartphone-/Desktopbedienung | N | Captive Browser, runde Hardware, Accessibility | Vollbrowser-Hilfe, 360-px-Weblayout, Fokus/Labels, keine CDN-Abhängigkeit | P4/P6.6 / UI-03/ON-01 |
| B12 | Hintergrundjobs nach Browserende | N | Tabende darf OTA/Setup nicht zerstören | Gerätejob-ID, wieder abfragbarer Fortschritt/Fehler | P4/P7.4 / OTA-06/WEB-01 |

## 8. OTA, Release und Recovery

| ID | Funktion / Herkunft | Art | Problem | Umsetzung kurz | Paket / Abnahme |
| --- | --- | --- | --- | --- | --- |
| T01 | Eine Produktversion für zwei Chips (Q8) | A | Zwei Images, aber ein Kundenprodukt | Gemeinsame Release-ID und getrennte Rollen im signierten Manifest | P7.1 / OTA-01 |
| T02 | Update prüfen/Kanal/Notizen | A/N | HA-Updateentity entfällt | S3 liest signierten statischen Kanal; Website zeigt beide Iststände | P7.4/P7.6 / OTA-01 |
| T03 | Signiertes Paket hochladen | N | Lokaler Upload darf Prüfung nicht umgehen | Gleiches Größen-/Rollen-/Signaturgate wie Download | P7.1/P7.4 / OTA-01 |
| T04 | UART-Updater im Companion | N | Bestehendes UART ist kein Flashprotokoll | Begrenzte Chunks, eigener Verifier, A/B und Bootreport | P7.2 / OTA-04/OTA-06 |
| T05 | Companion zuerst, dann S3 (Q8) | A | HA-Koordinator verschwindet | S3-Journal und kompatibles Zwischenpaar; lokale Healthchecks | P7.3 / OTA-02/OTA-05 |
| T06 | Wiederaufnahme/Stromausfall | A/N | Ein Prozentwert ist kein Commit | Persistente Phase/Transaktion; sicheren Istzustand beider Chips prüfen | P7.3/P7.5 / OTA-03/OTA-06 |
| T07 | Settings-/Web-/Wetterasset-Rollback | N/A | Neue Bilder/Schema könnten alte Firmware brechen | Signiertes versionsgebundenes Paket, alter Stand bis Abschluss erhalten | P2.4/P7.5 / OTA-07/OTA-09 |
| T08 | USB-Recovery beider Rollen (Q8) | A | Fernreset/BOOT-Leitungen nicht belegt | Dokumentierter physischer Rettungsweg; nicht Remote-Unbrick versprechen | P2.1/P7.5 / OTA-04 |
| T09 | Schlüsselrotation/Secure Boot/Fertigung | N | Irreversible eFuses, Token/Signierschlüssel | Revisionsgebundenes Trustkonzept; Fertigung erst nach Recoveryprüfung | P7.1/P9 / OTA-08/SEC-02 |
| T10 | CI/Pair-Build/Herkunft (Q8) | A | HA-/HACS-Orchestrierung passt nicht mehr | Prinzipien übernehmen; eigene reproduzierbare, signierte Artefakte | P7.6/P9 / G5/G6 |

## 9. Optionales Audio am Knob

| ID | Funktion / Herkunft | Art | Problem | Umsetzung kurz | Paket / Abnahme |
| --- | --- | --- | --- | --- | --- |
| A01 | Klinke/Line-Out (Hardwarekandidat) | B | Reale Guition-Ausgangsstufe nicht bestätigt | Schaltung/Last/Mux/Mute messen, dann I2S-Ausgabe | P8.1/P8.2 / AUD-01 |
| A02 | Kopfhörer (Zieloption) | B | Line-DAC ist kein Kopfhörerverstärker | Nur nach Lastnachweis freigeben; ggf. zusätzliche Ausgangsstufe | P8 / AUD-01 |
| A03 | Radio-MP3/AAC lokal | B | Decoder/TLS/Wetter/UI konkurrieren | Gepufferte Pipeline und sichere Mute-/Unterlaufregeln | P8.3/P8.4 / AUD-02/PERF-01 |
| A04 | Spotify-Audio lokal | B | Web API liefert keinen Audiostream | Separates zugelassenes Empfänger-SDK mit Ressourcen-/Zertifizierungsgate | Separates Projekt / G0/AUD-01 |

## 10. Wetter, Radar, Screensaver und Avatar

### Wetter, Vorhersage und Standort

**Port** übernimmt vorhandene Funktion und Logik. **Anpassung** ersetzt Abhängigkeiten oder korrigiert den Datenvertrag. **Neu** ergänzt eine bisher fehlende Funktion. Der Wetterdienst liefert Daten direkt an den S3; ein dauerhaft laufender Home-Assistant- oder anderer eigener Server ist nicht vorgesehen. Q04/Q05 legen Deutschland und den Betrieb ohne laufende Anbietergebühren fest: DWD zuerst qualifizieren, MET Norway bei Bedarf als passende gebührenfreie Alternative prüfen; kommerzielle Nutzungsrechte bleiben ein Gate. P10 ist Teil der ersten Version, P11 folgt gemäß Q06. Details und verworfene Kostenoptionen stehen in der [Wetterarchitektur](14-WETTER.md).

| ID | Feature / Bestand | Problem | Kurze Umsetzung | Paket / Abnahme |
|---|---|---|---|---|
| W01 | **Port:** Wetter als Hauptansicht mit Temperaturbogen. QW1 | Neue Navigation muss Wetter neben Medien erreichbar halten. | Vorhandene Rundgeometrie und feste LVGL-Objekte übernehmen; nur geänderte Werte zeichnen. | P10 / WT-01 |
| W02 | **Anpassung:** Temperatur, Wettertext und differenzierte Symbole. QW1 | Bisher kommen Werte über HA; alte Daten können sichtbar bleiben. | Direkten Wetteradapter mit Zeitstempel und Gültigkeitsstatus anbinden; fehlende Werte als unbekannt zeigen. | P10 / WT-02 |
| W03 | **Anpassung:** Luftfeuchte und Windgeschwindigkeit. QW1 | Kein belegter eigener Wettersensor; Einheiten und Verfügbarkeit variieren. | Providerwerte nach Prozent und km/h normalisieren; unbekannte Größen niemals durch null ersetzen. | P10 / WT-02 |
| W04 | **Anpassung:** Lokal berechnete gefühlte Temperatur. QW1 | Berechnung mit fehlender Feuchte oder Wind erzeugt irreführende Werte. | Formel übernehmen, aber nur bei vollständigen aktuellen Eingangsdaten auswerten. | P10 / WT-02 |
| W05 | **Anpassung:** Tagesminimum und -maximum als Bogenskala. QW1 | Der bisherige Skalenfallback −10 bis 40 °C darf nicht als Vorhersage erscheinen. | Darstellungsbereich von echten Prognosewerten trennen; unbekannte Min-/Maxwerte kennzeichnen. | P10 / WT-02 |
| W06 | **Anpassung:** Tageskontext mit Jetzt, Morgen, Mittag, Abend und Nacht. QW1 | Alter Parser kann Stunden mit falschem Datum oder zu großem Abstand zuordnen. | UTC korrekt umrechnen; Tagesabschnitte nur mit gültigen, zeitlich passenden Stunden befüllen. | P10 / WT-03 |
| W07 | **Anpassung:** Wetter und Temperaturspanne für morgen und übermorgen. QW1 | Der Bestand puffert fünf Tage; sichtbar sind zwei kommende Tage. Fehlende Tage erhalten teils Ersatzwerte. | Begrenzten Tagescache beibehalten; nur belegte Tageswerte anzeigen. | P10 / WT-03 |
| W08 | **Anpassung:** Stunden- und Tagesvorhersage, bisher bis 48 Stunden und fünf Tage. QW1/QW2 | HA-Broker entfällt; kommerzielle Rechte ohne laufende Gebühren, Quoten und Antwortgrößen sind zu prüfen. | Deutschland zuerst: DWD-Spike, bei Bedarf MET; S3 normalisiert begrenzte Antworten mit Cache, Backoff und zulässigem Abrufintervall. | P10 / WT-04 |
| W09 | **Neu:** Standort und Zeitzone auf der Gerätewebsite einstellen. QW1 | Bisher ist eine HA-Wetterquelle konfiguriert; GPS oder lokale Ortssuche sind nicht vorhanden. | Ortssuche mit manueller Koordinatenalternative, Anzeigename und Zeitzone; bei Wechsel alte Anfragen verwerfen. | P10 / WT-05 |
| W10 | **Anpassung:** Regenbeginn aus der Stundenprognose. QW1 | Stundenwerte sind kein minutengenaues Radar-Nowcast; unbekannt wurde teils als „Kein Regen“ dargestellt. | Zeitraum und Prognosequelle kenntlich machen; „unbekannt“ von „kein Regen im verfügbaren Zeitraum“ trennen. | P10 / WT-03 |

### Regenradar

**Nachlieferung nach Q06:** W11–W15 und die zugehörigen Metadatengates bleiben offen im Gesamtumfang. Sie blockieren die erste Version mit P10 nicht. Vor ihrer Auslieferung ist die gesamte Mischlast mit Radar erneut am Gerät nachzuweisen.

| ID | Feature / Bestand | Problem | Kurze Umsetzung | Paket / Abnahme |
|---|---|---|---|---|
| W11 | **Anpassung:** Radarstatus und Regen-ETA aus externen Rain-Warner-Daten. QW6 | Die bisherige HA-Metadatenquelle entfällt. Eine zulässige direkte Quelle ist noch offen. | Eigenes Metadatengate: ETA nur aus geprüften Daten; sonst klar als Stundenprognose oder unbekannt anzeigen. | P11 / WT-11 |
| W12 | **Anpassung:** Radarstandbild mit drei Zoomstufen, Touch- und Ringbedienung. QW6 | Vollständiger generischer Renderer fehlt im Repo; das HA-Beispiel nutzt einen festen Bayern-Ausschnitt. | Direkte Radarquelle und Kartenprojektion prüfen; Standort zentrieren, drei begrenzte Bildstufen erzeugen. | P11 / WT-10 |
| W13 | **Anpassung:** Niederschlagsrichtung und Bewegungsgeschwindigkeit. QW6 | Dafür wird bisher ein externer Bewegungsvektor verwendet; Windrichtung ist kein Ersatz. | Nur mit validierten Bewegungsmetadaten anzeigen; andernfalls „Richtung nicht verfügbar“. | P11 / WT-11 |
| W14 | **Anpassung:** Radar laden, aktualisieren, zwischenspeichern und Fehler anzeigen. QW6 | Die Bridge lädt bisher fertige LAN-Bilder; künftig konkurriert HTTPS mit Spotify und Website. | Begrenzter S3-Download mit Timeout, Backoff, Bildalter und Standortgeneration; vorhandenes Bild als veraltet kennzeichnen. | P11 / WT-10 |
| W15 | **Port/Anpassung:** Inaktiven Radarpuffer dekodieren, anschließend atomar wechseln. QW6 | Cover, Avatar und Radar dürfen Speicher und Decoder nicht gleichzeitig überlasten. | Gemeinsamer Asset-Scheduler, feste Größenlimits und Freigabe unsichtbarer Puffer; UI bleibt auf ihrem eigenen Ausführungspfad. | P11 / WT-12 |

Radar bleibt zunächst ein **Standbild mit drei Zoomstufen**. Animation, Zeitleiste und lokale Bewegungsanalyse sind neue optionale Erweiterungen; sie gehören nicht zur zugesagten Bestandsportierung.

### Wetterfotos und Screensaver

| ID | Feature / Bestand | Problem | Kurze Umsetzung | Paket / Abnahme |
|---|---|---|---|---|
| W16 | **Port/Anpassung:** 15 Wetterfotos mit analoger Uhr, Datum und Wetterstatus. QW1/QW5 | Unkomprimierte RGB565-Bilder benötigen allein rund 4,06 MB Flash. | Vorhandene Motive als komprimierte lokale Assets übernehmen; Flashlayout und OTA-Rollback gemeinsam nachweisen. | P10 / WT-06 |
| W17 | **Anpassung:** Fotomotiv entsprechend dem Wetterzustand. QW1/QW5 | Unbekanntes Wetter fällt bisher auf „teilweise bewölkt“ zurück. | Neutrale Darstellung und Datenstatus für fehlendes oder veraltetes Wetter ergänzen. | P10 / WT-02, WT-06 |
| W18 | **Port:** Fester Hell-/Dunkelkontrast der Uhr je Fotomotiv. QW1 | Keine automatische Umgebungslichtmessung im Bestand. | Vorhandene Kontrastzuordnung übernehmen; keine zusätzliche Pixelanalyse oder Sensorfunktion behaupten. | P10 / WT-06 |
| W19 | **Anpassung:** Idle-Start, Fade, kurzzeitiger Helligkeitsboost und Rückkehr per Touch/Swipe. QW1 | Cover, Wetterfoto, Avatar und Display-Abschaltung brauchen gemeinsame Prioritäten. | Bestehende Bedienung in einen View-/Power-Scheduler integrieren; Einstellungen über Website und Systemmenü teilen. | P10, P3 / WT-09 |

Die Wetterfotos bleiben im Umfang. Ein allgemeines Fotoalbum oder die bisherigen Lichtfunktionen werden daraus nicht abgeleitet.

### Wetteravatar und Morgenautomatik

| ID | Feature / Bestand | Problem | Kurze Umsetzung | Paket / Abnahme |
|---|---|---|---|---|
| W20 | **Port:** Avatar jederzeit über die Wetteransicht öffnen. QW1 | Öffnen darf keinen Netzabruf oder UI-Stillstand auslösen. | Aktuellen lokalen Snapshot verwenden; ohne gültige Daten neutrale Figur zeigen; Schließgeste vollständig konsumieren. | P10 / WT-07, WT-09 |
| W21 | **Anpassung:** Braun/braune Augen oder blond/haselfarbene Augen, persistent wählbar. QW3/QW5 | HA-Konfiguration entfällt; lokale und Web-Einstellungen dürfen sich nicht überschreiben. | Gemeinsamen validierten Setter und versionierte NVS-Konfiguration verwenden. | P10 / WT-07 |
| W22 | **Anpassung:** Outfit aus vier vollständigen nächsten Prognosestunden. QW2 | Der bisherige Adapter setzt MET-Intervallsemantik voraus; Lücken und Duplikate machen das Fenster ungültig. | Aggregation nativ portieren; Anbietersemantik ausdrücklich prüfen; andernfalls beschrifteten Jetzt-Fallback verwenden. | P10 / WT-03, WT-07 |
| W23 | **Port/Anpassung:** Avatarfrische und getrennte Quellen-/Fensterzeiten. QW2/QW3 | Quellenprüfung, Beobachtung und Modelllauf haben unterschiedliche Gültigkeit. | Avatar-Kontext 20 min; CURRENT auch Beobachtung 20 min, FORECAST nach Anbieter-/Fenstergültigkeit. Neue zentrale Statuslogik mit eigenen Wetter-/Radargrenzen. | P10 / WT-02, WT-07 |
| W24 | **Port:** Neutraler Fallback, „Jetzt“ und „teils unbekannt“. QW2/QW3 | Teilweise fehlende Daten dürfen weder vollständige Empfehlung noch erfundene Nullwerte ergeben. | Pflichtwerte und optionale Felder getrennt validieren; abgelaufene Kleidung sofort ausblenden. | P10 / WT-07 |
| W25 | **Port:** Sechs Temperaturbänder und Kleidung nach niedrigster gültiger Temperatur. QW3 | Grenzwerte müssen nach dem Port identisch bleiben. | Reinen deterministischen Resolver und vorhandene Grenzwerttests übernehmen; kein KI-Dienst erforderlich. | P10 / WT-07 |
| W26 | **Port:** Nässeschutz, Schnee, Wind, Sonnenhut, Nebel, Nacht und Mischlagen. QW3 | Niederschlagsmenge belegt nicht automatisch Regen; Schutzregeln müssen kombinierbar bleiben. | Phasen- und Validitätsflags erhalten; bestehende thermische und Schutzregeln unverändert testen. | P10 / WT-07 |
| W27 | **Port:** Hagel-/Gewitterhinweise und Schutzsymbol. QW3 | Diese Hinweise sind keine amtlichen Wetterwarnungen. | Vorhandene Conditionhinweise übernehmen; unabhängige gültige Gefahrenhinweise auch ohne Kleidungsempfehlung erhalten. | P10 / WT-07 |
| W28 | **Port:** 52 geprüfte Avatar-Schlüssel inklusive Neutralbild und Sonnenhut. QW5 | Flash, Checksummen und Asset-/Firmwarekompatibilität müssen passen. | Bestehende deduplizierte JPEGs und Manifestprüfung übernehmen; Assetwechsel in OTA-Vertrag aufnehmen. | P10 / WT-06 |
| W29 | **Anpassung:** Asynchrones JPEG-Decoding und gemeinsamer Bild-/Textwechsel. QW3 | Der vorhandene Renderer hängt an ESPHome RuntimeImage; S3 trägt künftig zusätzliche Netzwerkaufgaben. | Decoderadapter portieren, nur einen relevanten Assetjob zulassen; abgeschlossene Bilder auf dem UI-Thread übernehmen. | P10 / WT-12 |
| W30 | **Port/Anpassung:** Morgenfenster 06:00 bis 10:00 mit lokaler Zeitzone und Sommerzeit. QW4 | HA-Uhr entfällt; frei wählbare Fensterzeiten sind bisher kein Bestand. | Lokale SNTP-/Zeitzonenbasis nutzen; feste Bestandszeiten und Ein-/Aus-Schalter portieren. | P10 / WT-08 |
| W31 | **Port:** Automatisch nach Inaktivität, manuell auch über 10 Uhr; Modal und OTA haben Vorrang. QW1/QW4 | Die automatische Ansicht darf Bedienung und wichtige Zustände nicht verdrängen. | Eigentümer „manuell/automatisch“ und bestehende Prioritätsregeln zentral erhalten. | P10 / WT-09 |
| W32 | **Anpassung:** Morgen-Wakeup und sichtbare Anzeige auch auf Akku. QW1/QW4 | S3-Deep-Sleep legt künftig auch Website und Spotify-Steuerung schlafen; Batterielaufzeit ist ungemessen. | Gewählter USB-Normalbetrieb hält den Host erreichbar; Display-off getrennt behandeln. Akku-/Offlineprofil nur ausdrücklich aktivieren und gesondert messen. | P10, P3 / WT-08, WT-09 |
| W33 | **Port/Anpassung:** Tests für Wetterregeln, Assets, Frische, Rendering und DST. QW2–QW5 | HA-Transporttests passen nicht zum direkten Gerätebackend; Geräteeindruck ist kein Unit-Test-Ergebnis. | Reine Tests übernehmen, reale P10-Mischlast vor erster Version; vollständige Mischlast mit Radar bei P11 erneut abnehmen und physisch protokollieren. | P10/P11 / WT-01–WT-12 |


## 11. Bewusst nicht übernommene oder zusätzliche Funktionen

| ID | Funktion | Entscheidung / Grund | Erhaltene Technik |
| --- | --- | --- | --- |
| X01 | Lichtslots, Hue/WLED, Szenen/Farben | Nicht im Produktumfang; benötigt eigene Hausintegration | Popup-/Latest-value-/Eingabemuster |
| X02 | Hausgrundriss und HA-Floorplanrenderer | Nicht im Produktumfang | Begrenzte Bildjobs und atomarer Bufferwechsel |
| X03 | Allgemeines Fotoalbum/Slideshow | Nicht angefragt; Wetterfotos bleiben ausdrücklich enthalten | Bilddecoder/Ansichtswechsel |
| X04 | Countdown und lokaler Haptikwecker | Wie im bisherigen Spotify-Konzept außerhalb dieser Edition | Monotone Uhr-/DST-/Schedulerfälle; kein Musikwecker behauptet |
| X05 | HA-Entitäten, MA-URIs, HACS, Native API | Abhängigkeiten entfallen vollständig | Fachliche Regeln werden auf S3 neu implementiert |
| X06 | Spotify-Suche, Kontolikes, Musikqueueeditor | Keine pauschale Freigabe/Bestandsportierung | Spätere Funktionen nach nachgewiesenem Providerweg |
| X07 | Radarfilm/Timeline, amtliche CAP-Warnungen | Zusätzliche Funktionen, keine belegten Bestandsfeatures | Radarstandbild und Condition-/Avatarhinweise bleiben |
| X08 | AirPlay 2, Cast, native WiiM/UPnP/Bluetooth-Ausgabe | Nicht Teil des beauftragten Ports; eigene Adapterprojekte | Vorhandene Connect-Empfänger als reguläre Testziele |

## 12. Quellkarte und erkannte Bestandsfallen

Alle Belege beziehen sich auf denselben Commit, nicht auf beliebige aktuelle Branchköpfe. Quellcode belegt vorhandene Implementierung, keine heutige Geräteabnahme. Die vollständige Herkunfts-/Hardwarekarte steht in [Upstream](06-UPSTREAM.md).

| Kürzel | Primäre Quellbelege / präziser Einstieg |
| --- | --- |
| Q1 | [ui_next_framework.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/ui_next_framework.h): `build_weather_`, `update_weather`, Media-/Navigationobjekte; tatsächliche Aktivierung [dual-mcu-s3-core.yaml](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/dual-mcu-s3-core.yaml#L12) |
| Q2 | [rotaryknob-s3-ui-core.yaml](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/rotaryknob-s3-ui-core.yaml): `handle_fast_touch_swipe`, `media_toggle_favorite`, `media_toggle_power`, Picker ab Zeile 7832, Playlistkontext ab 8051, Ring ab 10120 |
| Q3 | [ec1_pcnt_encoder.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/ec1_pcnt_encoder.h), [rotary-recognition.md](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/docs/rotary-recognition.md) |
| Q4 | [broker.py](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/custom_components/passion_wave/broker.py): Aktionen Zeile 65, `_async_wait_for_track` ab 661; [media.py](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/custom_components/passion_wave/media.py): LatestPlaybackQueue; [Konfiguration](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/docs/configuration.md) |
| Q5 | [dual_mcu_library_proxy.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/dual_mcu_library_proxy.h): `LibraryKind`, Paging; [__init__.py](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/custom_components/passion_wave/__init__.py): Library-/Trackservices, Snapshots |
| Q6 | [dual_mcu_link.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/dual_mcu_link.h), [dual-mcu-ha-bridge.md](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/docs/dual-mcu-ha-bridge.md) |
| Q7 | [responsive_power_policy.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/responsive_power_policy.h), [power-optimization.md](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/docs/power-optimization.md) |
| Q8 | [update.py](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/custom_components/passion_wave/update.py): Paarfolge ab 370; [installation.md](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/docs/installation.md), [build-pipeline.md](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/docs/build-pipeline.md) |
| QW1 | Q1/Q2 plus [dual-mcu-esp32-core.yaml](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/dual-mcu-esp32-core.yaml): Forecastparser ab 2246 und Regenhinweise ab 2337 |
| QW2 | [weather_avatar.py](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/custom_components/passion_wave/weather_avatar.py): Vierstunden-/UTC-/Validitätsaggregation; Q4 Forecastabruf |
| QW3 | [weather_avatar.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/weather_avatar.h), [weather_avatar_context.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/weather_avatar_context.h), [weather_avatar_render.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/weather_avatar_render.h) |
| QW4 | [weather_avatar_schedule.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/weather_avatar_schedule.h): `morning_action`, `next_morning_wake_seconds` |
| QW5 | [Avatar-Manifest](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/assets/weather-avatar/manifest.json), [Screensaver-Herkunft](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/assets/screensaver/README.md), [Avatar-Dokumentation](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/docs/weather-avatar.md) |
| QW6 | Q2/Q6 Radarblöcke, [dual_mcu_radar_proxy.h](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/esphome/dual_mcu_radar_proxy.h), [HA-Radarbeispiel](https://github.com/passion-wave/Passion-Wave-rotaryknob/blob/9cc5576c2fd4a9beb56cad24e211957c43aa2c62/home_assistant/packages/scrollwheel_rain_radar.yaml) |

Bei Widersprüchen gilt der geprüfte Code: UI Next ist im Rollenprofil aktiv; Playliststart erhält den Kontext und spielt nicht ersatzweise nur Track 0. Herz/Power sind lokale Flags. Beobachtete Bestätigung im alten Broker gilt nur für einzelne Tracks. Allgemeine Forecastparser haben Offset-/Ersatzwertprobleme; das präzisere Avatar-Zeitmodell dient als Vorlage. Der dokumentierte universelle Radarrenderer ist im gelieferten Beispiel nicht vollständig enthalten. Diese Unterschiede werden beim Port korrigiert statt als bewiesene Fähigkeiten übernommen.

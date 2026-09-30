# Produktentscheidungen und verbleibende Klärungen

Stand: 30. September 2026. Der Produkteigentümer hat alle neun Rückfragen beantwortet. Die folgende Tabelle hält seine Vorgaben und ihre Auswirkungen auf den [Implementierungsplan](08-IMPLEMENTIERUNGSPLAN.md) fest. Unbekannte Gerätegenerationen, Erweiterungsmengen und Termine bleiben ausdrücklich offen. Nutzerentscheidungen ersetzen keine technische Prüfung oder Anbieterfreigabe.

## Verbindlicher Ausgangspunkt

Eigenständiges Repository und Produkt für weitere Nutzer von Anfang an; kein Home Assistant oder Music Assistant. Spotify Connect bleibt der vorgesehene Wiedergabeweg. Sonos wird als Connect-Ziel berücksichtigt; native Sonos-Steuerung bleibt ein gesonderter Prüfzweig. Konfigurationswebsite, Playlist-/Podcastfreigaben, Radioverwaltung und OTA liegen auf dem Knob. Wetter, Avatar und Radar gehören zum Gesamtplan. Die Onboard-Klinke wird optional auf Line-/Kopfhörerfähigkeit untersucht.

## Antworten und Umsetzung

Alle folgenden Antworten wurden am **30. September 2026** gegeben.

| ID | Rückfrage | Bestätigte Antwort | Verbindliche Konsequenz |
| --- | --- | --- | --- |
| Q01 | Eigener HTTPS-Hilfsdienst für Spotify-Anmeldung? | **Nein. Auch die Anmeldung ohne eigenen externen Dienst.** | P1/P4.4 prüfen einen freigegebenen Partner-/Geräteflow ohne PassionWave-Linkingdienst. Kein eigener Callback-/Tokenbroker, auch keine eigene extern gehostete Callbackseite als stiller Ersatz. Anmeldung und Refresh bleiben unter G0/G2 offen, bis dieser Weg nachgewiesen ist. |
| Q02 | Website im Heimnetz oder nur im Geräte-WLAN? | **Jederzeit im Heim-WLAN, ohne WLAN-Wechsel.** | P4.2–3 muss sicheren schreibenden Browserzugriff im Heimnetz nachweisen. Geschützter Geräte-AP dient Einrichtung/Recovery und erfüllt allein nicht mehr die Alltagsabnahme. Vertrauenswürdiger Transport und Identitäts-/Zertifikatslebenszyklus bleiben technische Prüfaufgaben. |
| Q03 | Radio schon bei der ersten Veröffentlichung hörbar? | **Senderverwaltung zuerst, Radioausgabe später.** | P6.5 bleibt erste Version. Kein aktiver Radio-Play-Button ohne qualifizierten Renderer. P8 oder ein späterer geeigneter Sonos-Ausgabepfad blockieren die erste Veröffentlichung nicht. |
| Q04 | Zielgebiet für Wetter/Radar? | **Deutschland zuerst.** | P10/P11 priorisieren deutsche Abdeckung; DWD als erster Daten-/Radarprüfpfad. Quellen, Stationsentfernung, Gebietsgrenzen und tatsächliche Abdeckung weiterhin nachweisen. |
| Q05 | Laufende Wetter-/Radar-Anbietergebühren? | **Keine; passende freie Quellen priorisieren.** | Nur gebührenfreie, für den Produktbetrieb geeignete Quellen einplanen. DWD und gegebenenfalls MET prüfen. Open-Meteo Customer ist keine aktive Umsetzungsoption. Kostenlose Privatnutzung ersetzt keine kommerzielle Nutzungsfreigabe. Kein Wetter-Abo für Kunden einführen. |
| Q06 | Erste Veröffentlichung vor vollständigem Radar? | **Ja. Spotify, Wettervorhersage, Wetterbilder und Avatar zuerst; Radar anschließend vervollständigen.** | L3 Zweigerätepilot, L4 erste Produktversion ohne Radar-Pflicht, L5 Radar-Nachlieferung. P11 bleibt offener Pflichtumfang des Gesamtports, blockiert aber L3/L4 nicht. Regenbeginn/Richtung/Geschwindigkeit weiter unter eigenem Metadatengate führen. |
| Q07 | USB-/Akkubetrieb? | **Überwiegend USB-Netzteil; Website und Steuerung bleiben erreichbar.** | P3.5 priorisiert Display-Aus bei laufendem S3-Netzwerk. Kein normaler S3-Tiefschlaf und kein verbindliches Akkulaufzeitziel. Akkuanzeige und OTA-Energienachweis bleiben Aufgaben. |
| Q08 | Vorhandene Testhardware? | **Sonos Roam, Sonos Move und derselbe RotaryKnob wie im Passion-Wave-RotaryKnob-Projekt.** | P2.1 identifiziert tatsächliche Boardrevision; P5/Sonos prüfen Roam und Move über Connect. Sonos-Generationen/Firmwarestände sind noch nicht genannt. Keine zusätzlichen vorhandenen Lautsprecher oder bestätigten Testergebnisse ableiten. |
| Q09 | Pilotgröße, Erweiterung, Termin und Wetterbudget? | **„erstmal nur 2 und dann erweiterung“** | Mit zwei RotaryKnobs im ersten Pilot planen. Jahresmenge, Erweiterungstermin und Startdatum bleiben offen. Wetter-Anbieterbudget ist durch Q05 auf keine laufenden Gebühren festgelegt. Zwei Pilotgeräte sind keine Rücknahme des Produkts für weitere Nutzer. |

## Lieferumfang nach den Antworten

| Stufe | Geplanter Umfang | Offene Abnahme |
| --- | --- | --- |
| L3: Pilot mit zwei Knobs | USB-Betrieb, sichere Heimnetz-Website, freigegebener Spotify-Weg ohne eigenen Anmeldedienst, Sonos Roam/Move als Connect-Ziele, Playlists/Podcasts, Radioverwaltung, Wetter/Avatar und Pair-OTA | Gerätebezogene G0–G5/W1-Nachweise, erster Dauer-/Mehrgerätetest; keine allgemeine Vertriebsfreigabe |
| L4: erste Produktversion / Erweiterung | Qualifizierter Umfang des Piloten mit erweiterter Nutzer-/Modellabnahme und Auslieferung | G6 für diesen Umfang; Erweiterungsmenge/-termin noch offen |
| L5: Radar-Nachlieferung | Drei Standbild-Zoomstufen und separat nachgewiesene Regenmetadaten | W-RAD-0, W-RAD-META, W-RAD-1 sowie erneute Ressourcen-/Integrations-/OTA-Abnahme |
| Spätere Optionen | Radioausgabe, lokale Klinke/Kopfhörer; nativer Sonos-Volladapter nur nach Prüfentscheidung | Jeweils eigener Machbarkeits-/Freigabe-/Aufwandsnachweis |

Die zwei Pilotgeräte können nacheinander von weiteren Testpersonen benutzt werden. Die bisher vorgesehenen fünf Onboarding-Erstnutzer beziehungsweise zehn Nutzer der erweiterten Produktabnahme sind Prüfziele und keine Behauptung, dass so viele Pilotgeräte oder Tester verfügbar sind. Ein Pilot nur auf Sonos belegt keine herstellerübergreifende Kompatibilität.

## Verbleibende Klärungen und nächste Arbeit

Nachtrag während der Implementierung am 30.09.2026: Hardware vom Nutzer als **JC3636K518C_I_YR1, SKU 10160002 (2633), 360×360 Touch** benannt und USB-Flashen autorisiert. Angeschlossen wurde ein ESP32-U4WDH mit 4 MiB Flash identifiziert; Originalspeicher vollständig gesichert und gegen das Gerät verifiziert. Der zweite Chip ist noch nicht über USB geprüft. Spotify-Dashboardzugang wurde mitgeteilt; Quotenmodus und Freigabe für den Verkauf sind noch nicht nachgewiesen. Normale Spotify-Kundenanmeldung ohne Käufer-Entwicklerkonto ist ausdrücklich gefordert. Die Rückfragen nach Quotenmodus und Akzeptanz einer mobilen Einrichtungs-App sind offen. Siehe [Kundenanmeldung](02-SPOTIFY.md#käufer-melden-sich-mit-ihrem-normalen-konto-an).

| Punkt | Nächster konkreter Auftrag | Entscheidung / Nachweis |
| --- | --- | --- |
| Anmeldung ohne eigenen Hilfsdienst | P1/P4.4: zugelassenen Produktflow und Rückweg auf das Gerät unter dieser Vorgabe qualifizieren | G0/G2; keine Ersatz-Cloud und kein manueller Tokenimport als fertiges Kunden-Onboarding |
| Sicherer Heimnetz-Webzugriff | P4.3: Browser-/Namens-/Vertrauens-/Erneuerungsprobe auf iOS, Android und Desktop vorbereiten | G2; kein Zertifikatswarnungs-Workaround, keine zusätzliche Infrastruktur still voraussetzen |
| Exakte Hardware | P2.1: Knob-PCB-/Chiprevision und Sonos-Modellgeneration/Firmware am Testbestand erfassen | Geräteinventar; keine erneute grundlegende Produktentscheidung erforderlich |
| Gebührenfreie Wetter-/Radardaten | P10.1/P11.1: konkrete Datensätze, Rechte, Formate, Abdeckung und Flottenquoten prüfen | W0/W-RAD-0; ein Gebührenverzicht macht eine Quelle nicht automatisch geeignet |
| Radar-Metadaten | P11.5: direkte Quelle für Regenbeginn, Richtung und Geschwindigkeit suchen und qualifizieren | W-RAD-META; ohne Quelle bleibt dieser Portteil offen, L4 darf trotzdem nach eigenem Umfang fertig werden |
| Erweiterung nach zwei Geräten | Nach Pilotfeedback Zielmenge, weitere Lautsprecher und Termin festlegen | Q09 bleibt in diesen Detailangaben offen; keine Beschaffung oder feste Lieferzusage ableiten |

Falls der gewünschte Anmelde- oder Verwaltungsweg nicht nachweisbar ist, wird der konkrete Befund mit geeigneten Alternativen zur Entscheidung vorgelegt. Ein eigener externer Dienst oder eine verpflichtende zusätzliche App werden nicht ohne neue Nutzerentscheidung eingeführt. Die übrigen unabhängigen Arbeitspakete können weiterlaufen.

## Nachweise bleiben getrennt

Spotify-Zulassung, Sonos-LAN-Zugang, Speicher-/Latenzmessungen, sichere Browserführung, Wetterrechte, Radarverarbeitung, Kopfhörerlast und reale OTA-Ausfalltests sind technische beziehungsweise externe Gates. Die Antworten oben schließen diese nicht. Es wurden mit dieser Planaktualisierung keine Anbieter angeschrieben, Abonnements abgeschlossen, Dienste bereitgestellt oder Geräte geflasht.

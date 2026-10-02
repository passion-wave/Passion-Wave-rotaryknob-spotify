# Geräte-HTTP-Antworten auf dem Host

```sh
IDF_PATH=/path/to/esp-idf-v5.4.3 python3 tests/http_response_native/run.py
```

Der Test kompiliert den aktuellen `asset_handler` und dessen `headers` sowie
ESP-IDFs originale Funktionen `httpd_resp_send` und `httpd_send_all` direkt aus
den Quellen. IDF-Anfragezustand und `send()` werden simuliert; es wird weder ein
Socket geöffnet noch USB verwendet. ASan/UBSan sind aktiv.

Vier echte Gzip-Assets werden jeweils vollständig sowie mit kurzen Writes von
1, 37, 1440 und 5760 Byte gesendet. Die resultierenden HTTP-Bytefolgen werden auf
exakte Länge und Payload, MIME/CSP/nosniff und Gzip-Dekodierung geprüft.
Sendeabbrüche vor und nach dem Header müssen scheitern und dürfen kein
Abschlussereignis erzeugen. Die bestehende Hostvalidierung selbst prüft weiterhin
`tests/http_native`; hier verhindert ihr negatives Ergebnis die Auslieferung.

Dieser Test belegt die Antwortstruktur und Behandlung partieller Writes. Er
simuliert keine TCP-ACKs, WLAN-Puffer oder Funkinteroperabilität und ersetzt
keine Ladezeitmessung auf dem echten iPhone/ESP32-S3.

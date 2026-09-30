# Signierte Native-OTA-Tests

`python3 tests/update_native/run.py` baut den tatsächlichen C-Verifier mit
ASan/UBSan und linkt ESP-IDFs mbedTLS-Hostbibliothek. `IDF_PATH` und optional
`PW_UPDATE_MBEDTLS_BUILD` konfigurieren die SDK-Quelle bzw. den temporären
Bibliothekscache. Benötigt C-Compiler, CMake und Python `cryptography`.

51 adversariale/signierte Fälle und Bundle-Framing/Unveränderlichkeit bestanden.
Alle P-256-/P-384-Schlüssel und Testpakete werden ausschließlich in einem
`TemporaryDirectory` erzeugt und gelöscht. Keine eingecheckten privaten Keys,
keine Netz-/Flashzugriffe. Die kleinen Binärfixtures sind künstliche ESP-
Appdescriptoren, keine ausführbare Firmware. Hosttest ersetzt keinen Pair-OTA-
oder Powercut-Test am Gerät.

# Companion receiver tests

Run from the repository root:

```sh
python3 tests/companion_update_native/run.py
```

Dependencies: a C compiler with ASan/UBSan, Python `cryptography`, CMake and the
ESP-IDF 5.4.3 source tree (`IDF_PATH`, default `~/esp/esp-idf-v5.4.3`). The runner
builds/caches host mbedTLS under the system temp directory; optionally select the
cache with `PW_UPDATE_MBEDTLS_BUILD`. Keys are generated only in temporary test
directories. No serial port, physical device or production private key is used.

The tests compile the actual receiver, UART CRC and manifest/image verifier. NVS,
flash slots, boot selection and mark-valid are deterministic bounded models, so
these tests do not qualify ESP flash write timing or bootloader recovery.

The current 1,140 assertions cover null trust; malformed and oversized frames;
invalid signatures before erase; duplicate ACK/chunk replay and changed bytes;
offset gaps; short and corrupted images; active-image preservation; midstage
restart; abort/restart; journal failure before activation; bad or stale pair
health; nonce/hash mismatch; commit only after pair health; lost post-mark-valid
COMPLETE write; old-S3 rollback; unconfirmed-boot timeout; corrupt journal;
changed local trust; proof persistence failure before erase; missing/tampered
persisted signatures; and journal target-hash changes with a recomputed CRC.

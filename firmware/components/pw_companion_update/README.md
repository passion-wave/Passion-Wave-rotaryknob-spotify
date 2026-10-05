# Companion update receiver

The ESP32 receives a signed two-image manifest and its own firmware through the
existing bounded UART protocol. The running slot is never written. Integration is
in `firmware/companion/main`; no USB flashing, eFuse change, production key or
automatic trust enrollment is part of this component.

`pw_companion_update_provisioned_config()` deliberately returns `NULL` by default:
all mutating commands return `LOCKED`. A separately qualified provisioning unit
must provide the local public key, key ID, hardware/bootloader/security/protocol
policy and the exact hash of the S3 image paired with this companion release.
Those values must not come from an upload or an unauthenticated boot report.
The running version comes from the actual ESP app descriptor. The caller-owned
configuration and key buffers must remain alive for the lifetime of the receiver.

## Validation and storage

`META_VERIFY` invokes `pw_update_verify` over the exact received manifest bytes and
detached ECDSA-P256 DER signature. It checks both roles, hardware, versions,
bootloader, image sizes, security floors, configuration and peer compatibility.
No inactive-slot erase occurs before this succeeds and both the signed proof and
transaction journal are persisted.

The NVS `journal` partition contains namespace `pw_pair`, blob keys `state` and
`proof`. The loader distinguishes an absent key from an existing wrong NVS type or
read error with `nvs_find_key`; it never erases NVS on an error. The journal is an
explicit 408-byte little-endian format with a version, CRC16, transaction ID,
manifest hash, both target hashes, previous pair hashes, old/new slot addresses,
image lengths, versions, security versions, release ID and trust-key fingerprint.
The CRC detects corruption; it does not authenticate a transaction.

`proof` is `PWP1`, manifest length LE16, signature length U8, a zero byte, then the
original exact JSON bytes followed by DER (at most 16,464 bytes). It is written
before the journal records `RECEIVING`. Every reboot that has a target image
re-verifies this signature against the local key and current policy, compares all
signed target identities with the journal, and checks the actual slot addresses.
Missing proof, a changed key, corrupted signature or changed target hash closes
OTA. A persisted phase alone cannot authorize an image.

Image data is strictly ordered, bounded by the signed length and streamed through
the actual image verifier and `esp_ota_write`. `IMAGE_VERIFY` checks SHA-256,
descriptor identity/version/security, `esp_ota_end`, full ESP image validation and
a fresh hash read from flash. Only then is `VERIFIED` persisted. The SDK may buffer
an incomplete encrypted block; duplicate comparison overlays only the last
accepted 16 bytes while a write is open. Final verification reads flash itself.

## Wire and retry contract

The authoritative version-1 layout is
`firmware/components/pw_protocol/include/pw_update_wire.h`. Existing HELLO is
unchanged. Frames use the existing 192-byte payload limit; metadata chunks hold
at most 173 bytes and image chunks 171 bytes. Every command includes the
transaction ID. ACK binds command kind and sequence, transaction, current
companion boot nonce, phase, manifest digest and next offset.

A sender uses stop-and-wait, a bounded retry count and the identical frame on an
ACK timeout. An identical last sequence/payload returns the cached ACK; changed
bytes for that sequence are rejected. A new sequence that retransmits already
accepted chunk bytes succeeds only if they are identical, without double hashing
or writing. Older sequences and gaps in chunk offsets are rejected. Do not insert
STATUS between a command and its retry when relying on the last-ACK cache.

ACK progress is META/0 after BEGIN, cumulative metadata bytes during META,
RECEIVING/0 after META_VERIFY, cumulative image bytes during RECEIVING, and
VERIFIED/full image length after IMAGE_VERIFY. ABORT before activation stops
staging and persists IDLE; successful ACK has offset, digest and image bytes zero.
It preserves the active slot and echoes the incoming transaction. A failed journal
write returns JOURNAL/RECOVERY. BOOT_REPORT is observational; IDLE/LOCKED report
zero manifest and image hashes and a fresh boot nonce.

## Activation, pair health and recovery

The implemented S3 sender currently stops at VERIFIED; it does not send activation
or commit commands. These receiver paths exist for later coordinated qualification:

1. ACTIVATE requires matching manifest digest and current companion nonce. It
   rechecks actual flash, persists ACTIVATING, sets the verified inactive boot slot,
   sends ACK, then requests restart.
2. On the new image, the original signed proof and running image hash are checked.
   The receiver enters BOOT_PENDING. No startup, HELLO or heartbeat marks it valid.
3. PAIR_HEALTH explicitly binds current S3 session/nonce, current companion nonce,
   the manifest, actual target S3 hash and the required local UI/storage/web/peer
   health flags. Internet availability is not a commit requirement.
4. COMMIT requires that same S3 session and pair health no older than 30 seconds.
   It persists COMMITTING before SDK mark-valid and COMPLETE afterward. A power cut
   after mark-valid but before COMPLETE requires fresh pair health after reboot.
5. ROLLBACK requires the previously recorded S3 hash and a revalidated old companion
   image satisfying the current security floor. It persists ROLLBACK_PENDING
   before selecting the old slot. Returning to the old image reports RECOVERY.

A mid-transfer restart never claims to resume a SHA context: it reports RECOVERY
and requires ABORT plus a new authenticated transfer. An unconfirmed boot times
out after ten minutes and restarts without mark-valid, allowing the IDF rollback
mechanism to choose its previously valid slot. A completed pair updates the
in-memory baseline S3 hash; the provisioned per-release baseline remains necessary
after clearing a transaction and rebooting. Repeated-update/rollback qualification
must verify that baseline for each released pair.

The journal is not a defense against arbitrary physical rewriting of all flash
while Secure Boot is disabled. Original target signatures are rechecked, but
prior-slot history and mutable phase storage still rely on the qualified local
firmware/storage boundary. Secure Boot, manufacturing trust custody, eFuse floors,
peer authenticity and complete two-chip activation are separate release gates.
The build refuses automatic `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` advancement.

## Evidence and remaining device checks

`tests/companion_update_native/run.py` executes this receiver and the real mbedTLS
signature verifier with ASan/UBSan and a bounded flash/NVS/power-cut model. It
currently passes 1,140 assertions. An independent ESP-IDF 5.4.3 ESP32 project build
also succeeds. Main task stack is 12 KiB for crypto/image validation. These are
build/model results, not measurements or OTA confirmation on the connected board.

On-device qualification still needs UART loss/retry, NVS power cuts at every
transition, partition readback, pending-image reboot rollback, complete pair-health
coordination and stack/heap/watchdog measurements with the exact board revision.

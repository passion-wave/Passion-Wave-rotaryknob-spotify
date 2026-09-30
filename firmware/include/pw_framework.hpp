#pragma once

// Framework contract only: no driver, provider, crypto, storage or OTA implementation.
// C++17, standard library only. These C++ layouts are NOT a wire/storage format.
#include <array>
#include <cstddef>
#include <cstdint>

namespace passion_wave::spotify_edition {

using RequestId = std::uint64_t;
using SessionId = std::uint64_t;
using Generation = std::uint64_t;
using ConfigRevision = std::uint64_t;
using MonotonicMs = std::uint64_t;

template <std::size_t Capacity>
struct BoundedText {
  std::array<char, Capacity> bytes{};
  std::uint16_t length{0};
  // Invariant: length <= Capacity; validated UTF-8. Never assume a trailing NUL.
};

using ReleaseId = BoundedText<64>;

struct ByteView {
  const std::uint8_t* data{nullptr};
  std::size_t size{0};
  // Borrowed for the duration of the call only; asynchronous users must copy.
};

enum class Result : std::uint8_t {
  unavailable, accepted, unsupported, unauthenticated, forbidden,
  invalid_argument, busy, stale_generation, timeout, storage_error,
  transport_error, verification_failed, incompatible, resource_limit
};

enum class ProcessorRole : std::uint8_t {
  invalid = 0, s3_controller = 1, esp32_companion = 2
};
enum class ContentKind : std::uint8_t {
  invalid, spotify_playlist, spotify_show, spotify_episode, radio_stream
};
enum class OutputKind : std::uint8_t {
  invalid, spotify_connect, local_line_out, vendor_radio, partner_local_spotify
  // partner_local_spotify is reserved; no receiver provider is implemented.
};
enum class PlaybackState : std::uint8_t {
  unknown, stopped, paused, playing, buffering, unavailable
};
enum class CommandKind : std::uint8_t {
  invalid, play_content, pause, resume, previous, next, set_volume,
  seek, set_shuffle, set_repeat, select_output
};
enum class RepeatMode : std::uint8_t { off, context, one };

struct Capabilities {
  bool spotify_content{false};
  bool radio_stream{false};
  bool play_pause{false};
  bool previous_next{false};
  bool volume{false};
  bool seek{false};
  bool shuffle{false};
  bool repeat{false};
  bool transfer_playback{false};
  bool local_audio{false};
  bool headphones_load_verified{false};
  // All false until product approval AND runtime output capability establish them.
  // A Connect output does not gain arbitrary-URL radio capability.
};

struct ContentRef {
  ContentKind kind{ContentKind::invalid};
  BoundedText<64> id{};  // Validated provider ID or locally stored radio-station ID.
};

struct OutputRef {
  OutputKind kind{OutputKind::invalid};
  BoundedText<128> id{};
};

struct Command {
  RequestId request_id{0};
  SessionId session{0};
  Generation selection_generation{0};
  ConfigRevision config_revision{0};
  CommandKind kind{CommandKind::invalid};
  OutputRef output{};
  ContentRef content{};
  std::uint32_t position_ms{0};
  std::uint8_t volume_percent{0};  // 0..100; additionally subject to local cap.
  bool shuffle{false};
  RepeatMode repeat{RepeatMode::off};
  // Only fields relevant to kind are encoded. No credentials or arbitrary URLs.
  // IDs resolve against the current allowlist; accepted is NOT playback confirmed.
};

struct Snapshot {
  SessionId session{0};
  Generation generation{0};
  RequestId confirmed_request_id{0};
  OutputRef output{};
  ContentRef content{};
  Capabilities capabilities{};
  PlaybackState playback{PlaybackState::unknown};
  BoundedText<192> title{};
  BoundedText<160> creator{};
  BoundedText<128> output_name{};
  BoundedText<1024> cover_url{};
  std::uint32_t position_ms{0};
  std::uint32_t duration_ms{0};
  MonotonicMs observed_at_ms{0};
  std::uint8_t volume_percent{0};
  bool volume_known{false};
  bool position_known{false};
  bool shuffle{false};
  RepeatMode repeat{RepeatMode::off};
  // Publish atomically. Cover completion must match session+generation+content.
  // MonotonicMs is local to a boot, never a cross-processor wall-clock timestamp.
};

struct OutputInfo {
  OutputRef reference{};
  BoundedText<128> name{};
  Capabilities capabilities{};
  bool reachable{false};
  bool restricted{false};
};

class ProviderEvents {
 public:
  virtual ~ProviderEvents() = default;
  virtual void on_snapshot(const Snapshot& snapshot) = 0;
  virtual void on_command_result(RequestId request, Generation generation,
                                 Result result) = 0;
  virtual void on_output(RequestId request, const OutputInfo& output) = 0;
  virtual void on_outputs_complete(RequestId request, Result result) = 0;
};

class MediaProvider {
 public:
  virtual ~MediaProvider() = default;
  virtual Result start(ProviderEvents& events) = 0;
  virtual void stop() = 0;
  virtual Result submit(const Command& command) = 0;
  virtual Result request_snapshot(RequestId request) = 0;
  virtual Result request_outputs(RequestId request) = 0;
  virtual void cancel_older_selections(Generation current) = 0;
  // Implementations enqueue work; no blocking TLS in the input/render task.
  // Cancellation suppresses stale results; it cannot undo a remotely executed action.
  // This interface neither grants Spotify rights nor assumes an eSDK remote API.
};

enum class CredentialKind : std::uint8_t {
  invalid, wifi, provider_refresh_token, provider_session, device_private_key
};

struct CredentialKey {
  CredentialKind kind{CredentialKind::invalid};
  BoundedText<64> account_slot{};
};

class CredentialVisitor {
 public:
  virtual ~CredentialVisitor() = default;
  virtual Result use_secret(ByteView secret) = 0;
  // Secret is borrowed only during this callback; no retaining, logging or export.
};

class CredentialStore {
 public:
  virtual ~CredentialStore() = default;
  virtual Result replace_atomic(const CredentialKey& key, ByteView secret) = 0;
  virtual Result visit(const CredentialKey& key, CredentialVisitor& visitor) = 0;
  virtual Result erase(const CredentialKey& key) = 0;
  virtual Result erase_owner_credentials() = 0;
  // Encrypted storage, key lifecycle, power-loss safety and wiping are required
  // implementation obligations. The interface provides none of them by itself.
};

enum class OtaPhase : std::uint8_t {
  idle, preflight, staging_companion, verifying_companion,
  booting_companion, companion_healthy, staging_s3, verifying_s3,
  booting_s3, pair_healthy, committed, recovering, failed
};

struct ImageDescriptor {
  ProcessorRole role{ProcessorRole::invalid};
  ReleaseId release_id{};
  BoundedText<64> hardware_id{};
  BoundedText<48> version{};
  std::uint32_t byte_length{0};
  std::array<std::uint8_t, 32> sha256{};
  std::uint16_t emitted_protocol{0};
  std::uint16_t accepted_peer_protocol_min{0};
  std::uint16_t accepted_peer_protocol_max{0};
  std::uint16_t readable_config_schema_min{0};
  std::uint16_t readable_config_schema_max{0};
  std::uint16_t security_version{0};
  BoundedText<48> minimum_bootloader{};
  // All protocol/schema values must be nonzero and min <= max. An adapter
  // expands only bounded ranges in its supported protocol/schema universe;
  // it must not allocate an unbounded tuple from untrusted endpoints.
  // Check compatibility in both directions, old S3 + new companion and new pair.
  // Untrusted descriptive data until the backend verifies the signed release.
};

struct PeerHealth {
  RequestId transaction{0};
  ProcessorRole role{ProcessorRole::invalid};
  ReleaseId release_id{};
  BoundedText<48> version{};
  std::array<std::uint8_t, 32> running_image_sha256{};
  SessionId boot_session{0};
  std::uint16_t protocol{0};
  bool booted{false};
  bool self_test_ok{false};
  bool rollback_available{false};
  // Bind to the verified release, expected role/image and fresh boot challenge.
  // A version string or copied old health report is insufficient evidence.
};

struct OtaJournal {
  RequestId transaction{0};
  ReleaseId release_id{};
  OtaPhase phase{OtaPhase::idle};
  std::array<std::uint8_t, 32> manifest_digest{};
  std::uint32_t companion_verified_offset{0};
  std::uint32_t s3_verified_offset{0};
  Result last_result{Result::unavailable};
  // Status projection, NOT the complete persisted journal schema. Backend also
  // retains the authenticated release/old+target slots/schema rollback snapshot.
};

class OtaBackend {
 public:
  virtual ~OtaBackend() = default;
  virtual Result begin(RequestId transaction, ByteView signed_manifest) = 0;
  virtual Result describe_verified_image(ProcessorRole role,
                                        ImageDescriptor& image) const = 0;
  virtual Result write_chunk(ProcessorRole role, std::uint32_t offset,
                            ByteView bytes) = 0;
  virtual Result finish_and_verify(ProcessorRole role) = 0;
  virtual Result activate(ProcessorRole role) = 0;
  virtual Result record_health(const PeerHealth& health) = 0;
  virtual Result commit_pair() = 0;
  virtual Result resume_or_recover() = 0;
  virtual Result read_journal(OtaJournal& journal) const = 0;
  // Backend MUST reject out-of-order/unsigned/wrong-role/incompatible activation.
  // Companion is installed and healthy before S3 activation. Both A/B images
  // retain rollback until pair validation. Journal transitions are durable.
  // CRC/SHA alone is not publisher authentication. No crypto implemented here.
};

enum class PeerMessageKind : std::uint8_t {
  invalid, hello, heartbeat, health, mute, encoder_diagnostics,
  update_begin, update_chunk, update_ack, update_finish,
  update_activate, update_result
};

struct PeerFrame {
  static constexpr std::size_t max_payload = 192;
  std::uint16_t product_protocol{0};
  PeerMessageKind kind{PeerMessageKind::invalid};
  std::uint32_t session{0};
  std::uint32_t sequence{0};
  std::uint16_t length{0};
  std::array<std::uint8_t, max_payload> payload{};
  // Explicit serialization, COBS and CRC belong to a separate tested codec.
  // Product magic, role, length and protocol negotiation are mandatory.
};

class PeerEvents {
 public:
  virtual ~PeerEvents() = default;
  virtual void on_frame(const PeerFrame& frame) = 0;
  virtual void on_peer_health(const PeerHealth& health) = 0;
  virtual void on_link_error(Result error) = 0;
};

class UartPeer {
 public:
  virtual ~UartPeer() = default;
  virtual Result start(PeerEvents& events) = 0;
  virtual Result send(const PeerFrame& frame) = 0;
  virtual void service(MonotonicMs now_ms) = 0;
  virtual void stop() = 0;
  // Bounded queues; control/health outrank bulk; explicit ACK/backpressure.
  // UART is not an audio/PCM transport or a proven hardware unbrick channel.
};

}  // namespace passion_wave::spotify_edition

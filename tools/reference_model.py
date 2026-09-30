"""Executable design model, not firmware, a verifier, or an approval service.

All evidence is supplied by trusted test fixtures/adapters, separately from user
configuration. A boolean here never proves Spotify permission, a signature,
hardware support or actual health. Production must obtain those facts from
approved integration policy, cryptographic verification and measured devices.

OTA snapshots are returned in memory to explain durable checkpoints. This model
does not persist them, flash devices, fetch releases or perform rollback. A real
coordinator must journal every transition before performing its side effect and
protect/validate that journal. Recovery is terminal until a separate recovery
procedure establishes a known-good pair; it cannot be turned into success.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass
from enum import Enum
import hashlib
import json
import re
from typing import Any


class ContentKind(str, Enum):
    SPOTIFY_PLAYLIST = "spotify_playlist"
    SPOTIFY_EPISODE = "spotify_episode"
    SPOTIFY_SHOW = "spotify_show"
    RADIO_STREAM = "radio_stream"


class OutputTarget(str, Enum):
    SPOTIFY_CONNECT = "spotify_connect"
    LOCAL_LINE_OUT = "local_line_out"
    VENDOR_RADIO = "vendor_radio"


class ControllerApproval(str, Enum):
    PENDING = "pending"
    APPROVED = "approved"


@dataclass(frozen=True)
class ProductPermissions:
    """Trusted integration policy input; never deserialize from user config."""

    controller: ControllerApproval = ControllerApproval.PENDING
    radio_product_approved: bool = False


@dataclass(frozen=True)
class RuntimeCapabilities:
    connected: bool = False
    restricted: bool = True
    can_start_playlist: bool = False
    can_start_episode: bool = False
    local_audio_hardware_proven: bool = False
    local_radio_decoder_proven: bool = False
    vendor_radio_api_proven: bool = False


@dataclass(frozen=True)
class OutputDecision:
    allowed: bool
    reason: str


def resolve_output(
    content: ContentKind,
    target: OutputTarget,
    permissions: ProductPermissions,
    runtime: RuntimeCapabilities,
) -> OutputDecision:
    """Conservative capability gate; no unchecked string/config coercion."""
    if not isinstance(content, ContentKind) or not isinstance(target, OutputTarget):
        return OutputDecision(False, "unknown_content_or_target")
    if not isinstance(permissions, ProductPermissions) or not isinstance(
        runtime, RuntimeCapabilities
    ):
        return OutputDecision(False, "trusted_evidence_required")
    if content is ContentKind.SPOTIFY_SHOW:
        return OutputDecision(False, "show_is_browse_only")
    if content in (ContentKind.SPOTIFY_PLAYLIST, ContentKind.SPOTIFY_EPISODE):
        if target is not OutputTarget.SPOTIFY_CONNECT:
            return OutputDecision(False, "spotify_audio_receiver_not_implemented")
        if permissions.controller is not ControllerApproval.APPROVED:
            return OutputDecision(False, "controller_approval_required")
        if runtime.connected is not True or runtime.restricted is not False:
            return OutputDecision(False, "speaker_unavailable_or_restricted")
        capable = (
            runtime.can_start_playlist
            if content is ContentKind.SPOTIFY_PLAYLIST
            else runtime.can_start_episode
        )
        return OutputDecision(capable is True, "allowed" if capable is True else "speaker_capability_unproven")
    if target is OutputTarget.SPOTIFY_CONNECT:
        return OutputDecision(False, "radio_url_cannot_target_spotify_connect")
    if permissions.radio_product_approved is not True:
        return OutputDecision(False, "radio_product_permission_required")
    if runtime.connected is not True:
        return OutputDecision(False, "output_unavailable")
    proven = (
        runtime.local_audio_hardware_proven is True
        and runtime.local_radio_decoder_proven is True
        if target is OutputTarget.LOCAL_LINE_OUT
        else runtime.vendor_radio_api_proven is True
    )
    return OutputDecision(proven, "allowed" if proven else "radio_output_unproven")


class Role(str, Enum):
    COMPANION = "companion"
    S3 = "s3"


@dataclass(frozen=True)
class ImageEvidence:
    role: Role
    hardware: str
    version: str
    size_bytes: int
    signature_verified: bool
    example_only: bool
    protocol: int
    accepts_peer_protocols: tuple[int, ...]
    readable_config_schemas: tuple[int, ...]
    release_id: str
    sha256: str


def _positive_integer(value: Any) -> bool:
    return type(value) is int and value > 0


def _positive_integer_tuple(value: Any) -> bool:
    return (
        type(value) is tuple
        and bool(value)
        and all(_positive_integer(item) for item in value)
    )


def _valid_digest(value: Any) -> bool:
    # Canonical lowercase SHA-256, excluding the conventional empty placeholder.
    return (
        isinstance(value, str)
        and re.fullmatch(r"[0-9a-f]{64}", value) is not None
        and value != "0" * 64
    )


@dataclass(frozen=True)
class OtaPlan:
    companion: ImageEvidence
    s3: ImageEvidence
    companion_hardware: str
    s3_hardware: str
    companion_slot_bytes: int
    s3_slot_bytes: int
    current_s3_protocol: int
    current_s3_accepts_peer_protocols: tuple[int, ...]
    current_config_schema: int
    release_id: str
    product_version: str
    signed_manifest_sha256: str
    example_only: bool = False

    def rejection(self) -> str | None:
        if not isinstance(self.companion, ImageEvidence) or not isinstance(self.s3, ImageEvidence):
            return "image_evidence_required"
        if self.example_only is not False:
            return "example_release_forbidden"
        if not _valid_digest(self.signed_manifest_sha256):
            return "invalid_manifest_digest"
        if not isinstance(self.release_id, str) or not self.release_id.strip():
            return "release_id_required"
        if not isinstance(self.product_version, str) or not self.product_version.strip():
            return "product_version_required"
        if (
            not _positive_integer(self.current_s3_protocol)
            or not _positive_integer(self.current_config_schema)
            or not _positive_integer_tuple(self.current_s3_accepts_peer_protocols)
        ):
            return "invalid_protocol_or_schema_type"
        for expected, image, hardware, capacity in (
            (Role.COMPANION, self.companion, self.companion_hardware, self.companion_slot_bytes),
            (Role.S3, self.s3, self.s3_hardware, self.s3_slot_bytes),
        ):
            if image.role is not expected:
                return "image_role_mismatch"
            if (
                not isinstance(hardware, str) or not hardware.strip()
                or not isinstance(image.hardware, str) or image.hardware != hardware
            ):
                return "hardware_mismatch"
            if image.signature_verified is not True:
                return "signature_not_verified"
            if image.example_only is not False:
                return "example_image_forbidden"
            if (
                not _positive_integer(image.size_bytes)
                or not _positive_integer(capacity)
                or image.size_bytes > capacity
            ):
                return "image_does_not_fit_slot"
            if not isinstance(image.version, str) or not image.version.strip():
                return "version_required"
            if image.version != self.product_version:
                return "pair_product_version_mismatch"
            if not isinstance(image.release_id, str) or image.release_id != self.release_id:
                return "pair_release_id_mismatch"
            if not _valid_digest(image.sha256):
                return "invalid_image_digest"
            if (
                not _positive_integer(image.protocol)
                or not _positive_integer_tuple(image.accepts_peer_protocols)
                or not _positive_integer_tuple(image.readable_config_schemas)
            ):
                return "invalid_protocol_or_schema_type"
            if self.current_config_schema not in image.readable_config_schemas:
                return "configuration_schema_incompatible"
        # The new companion must work with the still-old S3 in BOTH directions.
        if (
            self.current_s3_protocol not in self.companion.accepts_peer_protocols
            or self.companion.protocol not in self.current_s3_accepts_peer_protocols
        ):
            return "intermediate_pair_incompatible"
        if (
            self.s3.protocol not in self.companion.accepts_peer_protocols
            or self.companion.protocol not in self.s3.accepts_peer_protocols
        ):
            return "final_pair_incompatible"
        return None

    def fingerprint(self) -> str:
        """Bind journal to pair IDs and image/manifest digests, not authenticity.

        The adapter must hash actual image bytes, verify signed manifest bytes,
        compare those hashes, and only then supply signature_verified evidence.
        """
        payload = json.dumps(asdict(self), sort_keys=True, separators=(",", ":"))
        return hashlib.sha256(payload.encode("utf-8")).hexdigest()


class OtaState(str, Enum):
    IDLE = "idle"
    COMPANION_INSTALLING = "companion_installing"
    COMPANION_HEALTH_PENDING = "companion_health_pending"
    S3_READY = "s3_ready"
    S3_INSTALLING = "s3_installing"
    S3_HEALTH_PENDING = "s3_health_pending"
    COMPLETE = "complete"
    RECOVERY_REQUIRED = "recovery_required"


class OtaEvent(str, Enum):
    START = "start"
    IMAGE_WRITTEN = "image_written"
    HEALTH_CHECK = "health_check"
    START_S3 = "start_s3"
    FAILURE = "failure"
    REBOOT = "reboot"


@dataclass(frozen=True)
class Transition:
    accepted: bool
    state: OtaState
    reason: str


class OtaCoordinator:
    """Companion-first reference state machine with fail-closed completion.

    IMAGE_WRITTEN stands for the platform adapter confirming a complete write and
    boot selection; it is not an instruction to write. HEALTH_CHECK is explicit
    measured evidence, including a fresh peer check for final S3 acceptance.
    Uncertain interrupted writes require recovery, never optimistic completion.

    Health booleans are NOT transaction-bound proof. Before providing them, the
    production adapter must authenticate each health report and match its role,
    release ID, current OTA transaction ID, expected running-image SHA-256 and
    current boot-session challenge. Reject replayed reports from earlier boots
    or transactions. This transport/evidence validation is outside this model.
    """

    def __init__(self, plan: OtaPlan):
        self.plan = plan
        self.state = OtaState.IDLE
        self.companion_confirmed = False
        self.s3_confirmed = False
        self.recovery_order: tuple[Role, ...] = ()
        self.recovery_reason: str | None = None
        self.last_reason = "created"
        self.sequence = 0

    def _result(self, accepted: bool, reason: str) -> Transition:
        self.last_reason = reason
        self.sequence += 1
        return Transition(accepted, self.state, reason)

    def _recover(self, reason: str) -> Transition:
        s3_started = self.state in (
            OtaState.S3_INSTALLING, OtaState.S3_HEALTH_PENDING, OtaState.COMPLETE
        )
        self.recovery_order = (
            (Role.S3, Role.COMPANION) if s3_started else (Role.COMPANION,)
        )
        self.state = OtaState.RECOVERY_REQUIRED
        self.recovery_reason = reason
        self.s3_confirmed = False
        return self._result(True, reason)

    def apply(
        self,
        event: OtaEvent,
        *,
        role: Role | None = None,
        health_ok: bool | None = None,
        peer_health_ok: bool | None = None,
    ) -> Transition:
        if not isinstance(event, OtaEvent):
            return self._result(False, "unknown_event")
        if self.state is OtaState.RECOVERY_REQUIRED:
            return self._result(False, "recovery_required")
        if event is OtaEvent.FAILURE:
            if self.state is OtaState.IDLE:
                return self._result(False, "no_update_in_progress")
            return self._recover("reported_failure")
        if event is OtaEvent.REBOOT:
            if self.state in (OtaState.COMPANION_INSTALLING, OtaState.S3_INSTALLING):
                return self._recover("interrupted_write_requires_recovery")
            if self.state is OtaState.S3_READY:
                self.companion_confirmed = False
                self.state = OtaState.COMPANION_HEALTH_PENDING
            elif self.state is OtaState.COMPLETE:
                self.s3_confirmed = False
                self.state = OtaState.S3_HEALTH_PENDING
            return self._result(True, "reboot_requires_pending_health_checks")
        if event is OtaEvent.START:
            if self.state is not OtaState.IDLE or role is not Role.COMPANION:
                return self._result(False, "companion_must_start_first")
            rejection = self.plan.rejection()
            if rejection:
                return self._result(False, rejection)
            self.state = OtaState.COMPANION_INSTALLING
            return self._result(True, "companion_update_started")
        if event is OtaEvent.START_S3:
            if self.state is not OtaState.S3_READY or not self.companion_confirmed:
                return self._result(False, "companion_health_required")
            if role is not Role.S3:
                return self._result(False, "event_role_mismatch")
            self.state = OtaState.S3_INSTALLING
            return self._result(True, "s3_update_started")
        if event is OtaEvent.IMAGE_WRITTEN:
            expected = {
                OtaState.COMPANION_INSTALLING: (Role.COMPANION, OtaState.COMPANION_HEALTH_PENDING),
                OtaState.S3_INSTALLING: (Role.S3, OtaState.S3_HEALTH_PENDING),
            }.get(self.state)
            if expected is None or role is not expected[0]:
                return self._result(False, "unexpected_write_confirmation")
            self.state = expected[1]
            return self._result(True, "health_confirmation_required")
        if event is OtaEvent.HEALTH_CHECK:
            if self.state is OtaState.COMPANION_HEALTH_PENDING and role is Role.COMPANION:
                if health_ok is not True or peer_health_ok is not True:
                    return self._recover("companion_or_intermediate_peer_unhealthy")
                self.companion_confirmed = True
                self.state = OtaState.S3_READY
                return self._result(True, "intermediate_pair_confirmed")
            if self.state is OtaState.S3_HEALTH_PENDING and role is Role.S3:
                if health_ok is not True or peer_health_ok is not True:
                    return self._recover("s3_or_peer_unhealthy")
                if not self.companion_confirmed:
                    return self._recover("companion_checkpoint_missing")
                self.s3_confirmed = True
                self.state = OtaState.COMPLETE
                return self._result(True, "both_devices_confirmed")
            return self._result(False, "unexpected_health_confirmation")
        return self._result(False, "event_not_allowed")

    def snapshot(self) -> dict[str, Any]:
        """An in-memory journal proposal; contains no tokens or image bytes."""
        return {
            "schema": 1,
            "plan_fingerprint": self.plan.fingerprint(),
            "state": self.state.value,
            "companion_confirmed": self.companion_confirmed,
            "s3_confirmed": self.s3_confirmed,
            "recovery_order": [role.value for role in self.recovery_order],
            "recovery_reason": self.recovery_reason,
            "last_reason": self.last_reason,
            "sequence": self.sequence,
        }

    @classmethod
    def restore(cls, plan: OtaPlan, snapshot: dict[str, Any]) -> OtaCoordinator:
        """Restore only a structurally consistent journal for this exact plan.

        Storage authenticity is outside this model. After a real reboot, deliver
        REBOOT before continuing so interrupted writes and stale health cannot
        be interpreted as permission to progress.
        """
        model = cls(plan)
        if not isinstance(snapshot, dict) or set(snapshot) != set(model.snapshot()):
            raise ValueError("invalid_journal_fields")
        if type(snapshot["schema"]) is not int or snapshot["schema"] != 1:
            raise ValueError("unsupported_journal_schema")
        if snapshot["plan_fingerprint"] != plan.fingerprint():
            raise ValueError("journal_plan_mismatch")
        if type(snapshot["companion_confirmed"]) is not bool or type(snapshot["s3_confirmed"]) is not bool:
            raise ValueError("invalid_health_checkpoint")
        if type(snapshot["sequence"]) is not int or snapshot["sequence"] < 0:
            raise ValueError("invalid_journal_sequence")
        if not isinstance(snapshot["last_reason"], str):
            raise ValueError("invalid_journal_reason")
        try:
            model.state = OtaState(snapshot["state"])
            model.recovery_order = tuple(Role(value) for value in snapshot["recovery_order"])
        except (TypeError, ValueError) as exc:
            raise ValueError("invalid_journal_state") from exc
        model.companion_confirmed = snapshot["companion_confirmed"]
        model.s3_confirmed = snapshot["s3_confirmed"]
        if model.state is not OtaState.IDLE and plan.rejection():
            raise ValueError("journal_has_invalid_plan")
        if model.state in (OtaState.S3_READY, OtaState.S3_INSTALLING, OtaState.S3_HEALTH_PENDING, OtaState.COMPLETE) and not model.companion_confirmed:
            raise ValueError("journal_missing_companion_checkpoint")
        if model.state is OtaState.COMPLETE and not model.s3_confirmed:
            raise ValueError("journal_missing_s3_checkpoint")
        if model.s3_confirmed and model.state is not OtaState.COMPLETE:
            raise ValueError("journal_has_early_s3_confirmation")
        if model.companion_confirmed and model.state in (OtaState.IDLE, OtaState.COMPANION_INSTALLING, OtaState.COMPANION_HEALTH_PENDING):
            raise ValueError("journal_has_early_companion_confirmation")
        if model.state is OtaState.RECOVERY_REQUIRED:
            if model.recovery_order not in ((Role.COMPANION,), (Role.S3, Role.COMPANION)):
                raise ValueError("invalid_recovery_order")
            if not isinstance(snapshot["recovery_reason"], str) or not snapshot["recovery_reason"]:
                raise ValueError("missing_recovery_reason")
        elif model.recovery_order:
            raise ValueError("unexpected_recovery_order")
        elif snapshot["recovery_reason"] is not None:
            raise ValueError("unexpected_recovery_reason")
        model.recovery_reason = snapshot["recovery_reason"]
        model.last_reason = snapshot["last_reason"]
        model.sequence = snapshot["sequence"]
        return model

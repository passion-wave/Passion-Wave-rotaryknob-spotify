"""Design contract tests; do not claim hardware, Spotify or OTA integration."""

from dataclasses import replace
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from reference_model import (  # noqa: E402
    ContentKind, ControllerApproval, ImageEvidence, OtaCoordinator, OtaEvent,
    OtaPlan, OtaState, OutputTarget, ProductPermissions, Role,
    RuntimeCapabilities, resolve_output,
)


def plan_fixture():
    return OtaPlan(
        companion=ImageEvidence(Role.COMPANION, "companion-v1", "0.2.0", 100, True, False, 1, (1, 2), (1,), "release-0.2.0", "a" * 64),
        s3=ImageEvidence(Role.S3, "display-v1", "0.2.0", 200, True, False, 2, (1,), (1,), "release-0.2.0", "b" * 64),
        companion_hardware="companion-v1", s3_hardware="display-v1",
        companion_slot_bytes=1000, s3_slot_bytes=2000,
        current_s3_protocol=1, current_s3_accepts_peer_protocols=(1,),
        current_config_schema=1,
        release_id="release-0.2.0", product_version="0.2.0", signed_manifest_sha256="c" * 64,
    )


class OutputPolicyTests(unittest.TestCase):
    def setUp(self):
        self.permissions = ProductPermissions(ControllerApproval.APPROVED, True)
        self.runtime = RuntimeCapabilities(True, False, True, True, True, True, True)

    def decision(self, content, target=OutputTarget.SPOTIFY_CONNECT, permissions=None, runtime=None):
        return resolve_output(content, target, permissions or self.permissions, runtime or self.runtime)

    def test_spotify_requires_approved_controller_and_runtime_capability(self):
        for content in (ContentKind.SPOTIFY_PLAYLIST, ContentKind.SPOTIFY_EPISODE):
            with self.subTest(content=content):
                self.assertTrue(self.decision(content).allowed)
                self.assertFalse(self.decision(content, permissions=ProductPermissions()).allowed)
                self.assertFalse(self.decision(content, runtime=replace(self.runtime, restricted=True)).allowed)
                self.assertFalse(self.decision(content, runtime=replace(self.runtime, connected=False)).allowed)
                self.assertFalse(self.decision(content, OutputTarget.LOCAL_LINE_OUT).allowed)
        self.assertFalse(self.decision(ContentKind.SPOTIFY_EPISODE, runtime=replace(self.runtime, can_start_episode=False)).allowed)
        self.assertFalse(self.decision(ContentKind.SPOTIFY_PLAYLIST, runtime=replace(self.runtime, can_start_playlist=False)).allowed)

    def test_show_is_always_browse_only(self):
        for target in OutputTarget:
            self.assertEqual(self.decision(ContentKind.SPOTIFY_SHOW, target).reason, "show_is_browse_only")

    def test_radio_never_targets_connect_even_with_all_capabilities(self):
        self.assertFalse(self.decision(ContentKind.RADIO_STREAM).allowed)
        for target in (OutputTarget.LOCAL_LINE_OUT, OutputTarget.VENDOR_RADIO):
            self.assertTrue(self.decision(ContentKind.RADIO_STREAM, target).allowed)
            self.assertFalse(self.decision(ContentKind.RADIO_STREAM, target, permissions=replace(self.permissions, radio_product_approved=False)).allowed)
        self.assertFalse(self.decision(ContentKind.RADIO_STREAM, OutputTarget.LOCAL_LINE_OUT, runtime=replace(self.runtime, local_audio_hardware_proven=False)).allowed)
        self.assertFalse(self.decision(ContentKind.RADIO_STREAM, OutputTarget.LOCAL_LINE_OUT, runtime=replace(self.runtime, local_radio_decoder_proven=False)).allowed)
        self.assertFalse(self.decision(ContentKind.RADIO_STREAM, OutputTarget.VENDOR_RADIO, runtime=replace(self.runtime, vendor_radio_api_proven=False)).allowed)

    def test_config_dictionary_cannot_self_approve(self):
        result = resolve_output(ContentKind.SPOTIFY_PLAYLIST, OutputTarget.SPOTIFY_CONNECT, {"controller": "approved"}, self.runtime)
        self.assertFalse(result.allowed)
        self.assertEqual(result.reason, "trusted_evidence_required")
        self.assertFalse(self.decision(ContentKind.SPOTIFY_PLAYLIST, permissions=ProductPermissions("approved", True)).allowed)

    def test_unknown_types_and_truthy_flags_fail_closed(self):
        self.assertFalse(self.decision("radio_stream").allowed)
        self.assertFalse(self.decision(ContentKind.SPOTIFY_PLAYLIST, "spotify_connect").allowed)
        self.assertFalse(self.decision(ContentKind.SPOTIFY_PLAYLIST, runtime=replace(self.runtime, can_start_playlist="yes")).allowed)
        self.assertFalse(self.decision(ContentKind.RADIO_STREAM, OutputTarget.VENDOR_RADIO, permissions=replace(self.permissions, radio_product_approved=1)).allowed)


class OtaTests(unittest.TestCase):
    def setUp(self):
        self.plan = plan_fixture()
        self.model = OtaCoordinator(self.plan)

    def start_companion(self):
        self.assertTrue(self.model.apply(OtaEvent.START, role=Role.COMPANION).accepted)

    def companion_healthy(self):
        self.start_companion()
        self.model.apply(OtaEvent.IMAGE_WRITTEN, role=Role.COMPANION)
        self.model.apply(OtaEvent.HEALTH_CHECK, role=Role.COMPANION, health_ok=True, peer_health_ok=True)
        self.assertEqual(self.model.state, OtaState.S3_READY)

    def s3_health_pending(self):
        self.companion_healthy()
        self.model.apply(OtaEvent.START_S3, role=Role.S3)
        self.model.apply(OtaEvent.IMAGE_WRITTEN, role=Role.S3)
        self.assertEqual(self.model.state, OtaState.S3_HEALTH_PENDING)

    def test_success_needs_both_final_health_checks(self):
        self.s3_health_pending()
        result = self.model.apply(OtaEvent.HEALTH_CHECK, role=Role.S3, health_ok=True, peer_health_ok=True)
        self.assertTrue(result.accepted)
        self.assertEqual(result.state, OtaState.COMPLETE)

    def test_manifest_rejections_before_any_update(self):
        invalid = [
            (replace(self.plan, companion=replace(self.plan.companion, role=Role.S3)), "image_role_mismatch"),
            (replace(self.plan, s3=replace(self.plan.s3, role=Role.COMPANION)), "image_role_mismatch"),
            (replace(self.plan, s3=replace(self.plan.s3, hardware="wrong")), "hardware_mismatch"),
            (replace(self.plan, companion=replace(self.plan.companion, signature_verified=False)), "signature_not_verified"),
            (replace(self.plan, s3=replace(self.plan.s3, signature_verified="true")), "signature_not_verified"),
            (replace(self.plan, s3=replace(self.plan.s3, example_only=True)), "example_image_forbidden"),
            (replace(self.plan, s3=replace(self.plan.s3, size_bytes=2001)), "image_does_not_fit_slot"),
            (replace(self.plan, companion=replace(self.plan.companion, size_bytes=0)), "image_does_not_fit_slot"),
            (replace(self.plan, s3=replace(self.plan.s3, readable_config_schemas=(2,))), "configuration_schema_incompatible"),
            (replace(self.plan, companion=replace(self.plan.companion, accepts_peer_protocols=(2,))), "intermediate_pair_incompatible"),
            (replace(self.plan, current_s3_accepts_peer_protocols=(3,)), "intermediate_pair_incompatible"),
            (replace(self.plan, s3=replace(self.plan.s3, accepts_peer_protocols=(4,))), "final_pair_incompatible"),
        ]
        for plan, reason in invalid:
            with self.subTest(reason=reason):
                model = OtaCoordinator(plan)
                result = model.apply(OtaEvent.START, role=Role.COMPANION)
                self.assertFalse(result.accepted)
                self.assertEqual(result.reason, reason)
                self.assertEqual(model.state, OtaState.IDLE)

    def test_pair_product_version_and_release_id_must_match_manifest(self):
        invalid = (
            (replace(self.plan, s3=replace(self.plan.s3, version="0.3.0")), "pair_product_version_mismatch"),
            (replace(self.plan, companion=replace(self.plan.companion, version="0.1.0")), "pair_product_version_mismatch"),
            (replace(self.plan, product_version="0.4.0"), "pair_product_version_mismatch"),
            (replace(self.plan, s3=replace(self.plan.s3, release_id="another-release")), "pair_release_id_mismatch"),
            (replace(self.plan, companion=replace(self.plan.companion, release_id="another-release")), "pair_release_id_mismatch"),
            (replace(self.plan, release_id="different-pair"), "pair_release_id_mismatch"),
        )
        for plan, reason in invalid:
            with self.subTest(reason=reason):
                model = OtaCoordinator(plan)
                result = model.apply(OtaEvent.START, role=Role.COMPANION)
                self.assertFalse(result.accepted)
                self.assertEqual(result.reason, reason)
                self.assertEqual(model.state, OtaState.IDLE)

    def test_image_and_signed_manifest_digest_changes_invalidate_journal(self):
        self.companion_healthy()
        snapshot = self.model.snapshot()
        changes = (
            replace(self.plan, companion=replace(self.plan.companion, sha256="d" * 64)),
            replace(self.plan, s3=replace(self.plan.s3, sha256="e" * 64)),
            replace(self.plan, signed_manifest_sha256="f" * 64),
        )
        for changed in changes:
            with self.subTest(fingerprint=changed.fingerprint()), self.assertRaisesRegex(ValueError, "journal_plan_mismatch"):
                OtaCoordinator.restore(changed, snapshot)

    def test_malformed_and_placeholder_digests_rejected(self):
        for digest in (None, 123, "", "abc", "g" * 64, "A" * 64, "0" * 64, "a" * 65):
            for field in ("companion", "s3", "manifest"):
                with self.subTest(field=field, digest=digest):
                    changed = (
                        replace(self.plan, signed_manifest_sha256=digest)
                        if field == "manifest"
                        else replace(self.plan, **{field: replace(getattr(self.plan, field), sha256=digest)})
                    )
                    model = OtaCoordinator(changed)
                    self.assertFalse(model.apply(OtaEvent.START, role=Role.COMPANION).accepted)
                    self.assertEqual(model.state, OtaState.IDLE)

    def test_protocol_and_schema_fields_require_positive_integers_not_bool_or_float(self):
        for value in (True, False, 1.0, 0, -1, "1", None):
            changed_plans = (
                replace(self.plan, current_s3_protocol=value),
                replace(self.plan, current_config_schema=value),
                replace(self.plan, current_s3_accepts_peer_protocols=(value,)),
                replace(self.plan, companion=replace(self.plan.companion, protocol=value)),
                replace(self.plan, s3=replace(self.plan.s3, protocol=value)),
                replace(self.plan, companion=replace(self.plan.companion, accepts_peer_protocols=(value,))),
                replace(self.plan, s3=replace(self.plan.s3, readable_config_schemas=(value,))),
            )
            for changed in changed_plans:
                with self.subTest(value=value, plan=changed):
                    self.assertEqual(changed.rejection(), "invalid_protocol_or_schema_type")
        for values in (None, [], (), [1], "1"):
            with self.subTest(values=values):
                self.assertEqual(replace(self.plan, current_s3_accepts_peer_protocols=values).rejection(), "invalid_protocol_or_schema_type")
                self.assertEqual(replace(self.plan, s3=replace(self.plan.s3, readable_config_schemas=values)).rejection(), "invalid_protocol_or_schema_type")

    def test_sizes_and_capacities_require_positive_integers(self):
        for value in (True, False, 100.0, 0, -1, "100", None):
            changed_plans = (
                replace(self.plan, companion_slot_bytes=value),
                replace(self.plan, s3_slot_bytes=value),
                replace(self.plan, companion=replace(self.plan.companion, size_bytes=value)),
                replace(self.plan, s3=replace(self.plan.s3, size_bytes=value)),
            )
            for changed in changed_plans:
                with self.subTest(value=value):
                    self.assertEqual(changed.rejection(), "image_does_not_fit_slot")

    def test_examples_and_missing_identity_evidence_rejected(self):
        for changed in (
            replace(self.plan, example_only=True),
            replace(self.plan, example_only=0),
            replace(self.plan, companion=None),
            replace(self.plan, s3=replace(self.plan.s3, example_only=True)),
            replace(self.plan, release_id=""),
            replace(self.plan, product_version=True),
            replace(self.plan, companion=replace(self.plan.companion, version=1)),
            replace(self.plan, s3=replace(self.plan.s3, hardware=None)),
        ):
            with self.subTest(plan=changed):
                model = OtaCoordinator(changed)
                self.assertFalse(model.apply(OtaEvent.START, role=Role.COMPANION).accepted)
                self.assertEqual(model.state, OtaState.IDLE)

    def test_s3_cannot_start_before_companion_health(self):
        self.assertFalse(self.model.apply(OtaEvent.START, role=Role.S3).accepted)
        self.start_companion()
        self.assertFalse(self.model.apply(OtaEvent.START_S3, role=Role.S3).accepted)
        self.model.apply(OtaEvent.IMAGE_WRITTEN, role=Role.COMPANION)
        self.assertFalse(self.model.apply(OtaEvent.START_S3, role=Role.S3).accepted)
        self.assertEqual(self.model.state, OtaState.COMPANION_HEALTH_PENDING)

    def test_role_swapped_events_and_unknown_events_rejected(self):
        self.start_companion()
        self.assertFalse(self.model.apply(OtaEvent.IMAGE_WRITTEN, role=Role.S3).accepted)
        self.assertFalse(self.model.apply("complete").accepted)
        self.assertFalse(self.model.apply(OtaEvent.HEALTH_CHECK, role=Role.S3, health_ok=True, peer_health_ok=True).accepted)
        self.assertEqual(self.model.state, OtaState.COMPANION_INSTALLING)

    def test_false_or_missing_health_requires_recovery(self):
        for health, peer in ((False, True), (True, False), (None, True), (True, None), (1, True)):
            with self.subTest(health=health, peer=peer):
                self.model = OtaCoordinator(self.plan)
                self.s3_health_pending()
                self.model.apply(OtaEvent.HEALTH_CHECK, role=Role.S3, health_ok=health, peer_health_ok=peer)
                self.assertEqual(self.model.state, OtaState.RECOVERY_REQUIRED)
                self.assertEqual(self.model.recovery_order, (Role.S3, Role.COMPANION))
                self.assertFalse(self.model.apply(OtaEvent.HEALTH_CHECK, role=Role.S3, health_ok=True, peer_health_ok=True).accepted)

    def test_companion_failure_cannot_be_overridden_by_s3_success(self):
        self.start_companion()
        self.model.apply(OtaEvent.IMAGE_WRITTEN, role=Role.COMPANION)
        self.model.apply(OtaEvent.HEALTH_CHECK, role=Role.COMPANION, health_ok=False, peer_health_ok=True)
        for event in (OtaEvent.START_S3, OtaEvent.IMAGE_WRITTEN, OtaEvent.HEALTH_CHECK, OtaEvent.START):
            self.assertFalse(self.model.apply(event, role=Role.S3, health_ok=True, peer_health_ok=True).accepted)
        self.assertEqual(self.model.recovery_order, (Role.COMPANION,))
        self.assertEqual(self.model.state, OtaState.RECOVERY_REQUIRED)

    def test_failure_at_each_stage_never_completes(self):
        actions = [
            (OtaEvent.START, {"role": Role.COMPANION}),
            (OtaEvent.IMAGE_WRITTEN, {"role": Role.COMPANION}),
            (OtaEvent.HEALTH_CHECK, {"role": Role.COMPANION, "health_ok": True, "peer_health_ok": True}),
            (OtaEvent.START_S3, {"role": Role.S3}),
            (OtaEvent.IMAGE_WRITTEN, {"role": Role.S3}),
            (OtaEvent.HEALTH_CHECK, {"role": Role.S3, "health_ok": True, "peer_health_ok": True}),
        ]
        for count in range(1, len(actions) + 1):
            with self.subTest(stage=count):
                model = OtaCoordinator(self.plan)
                for event, args in actions[:count]:
                    model.apply(event, **args)
                model.apply(OtaEvent.FAILURE)
                for event, args in actions[count:]:
                    self.assertFalse(model.apply(event, **args).accepted)
                self.assertEqual(model.state, OtaState.RECOVERY_REQUIRED)
                self.assertFalse(model.s3_confirmed)

    def test_resume_from_journal_preserves_checkpoint(self):
        self.companion_healthy()
        restored = OtaCoordinator.restore(self.plan, self.model.snapshot())
        self.assertEqual(restored.snapshot(), self.model.snapshot())
        restored.apply(OtaEvent.REBOOT)
        self.assertEqual(restored.state, OtaState.COMPANION_HEALTH_PENDING)
        self.assertFalse(restored.apply(OtaEvent.START_S3, role=Role.S3).accepted)
        restored.apply(OtaEvent.HEALTH_CHECK, role=Role.COMPANION, health_ok=True, peer_health_ok=True)
        self.assertTrue(restored.apply(OtaEvent.START_S3, role=Role.S3).accepted)

    def test_reboot_during_each_write_requires_recovery(self):
        self.start_companion()
        self.model = OtaCoordinator.restore(self.plan, self.model.snapshot())
        self.model.apply(OtaEvent.REBOOT)
        self.assertEqual(self.model.state, OtaState.RECOVERY_REQUIRED)
        self.model = OtaCoordinator(self.plan)
        self.companion_healthy()
        self.model.apply(OtaEvent.START_S3, role=Role.S3)
        self.model = OtaCoordinator.restore(self.plan, self.model.snapshot())
        self.model.apply(OtaEvent.REBOOT)
        self.assertEqual(self.model.recovery_order, (Role.S3, Role.COMPANION))
        self.assertEqual(self.model.state, OtaState.RECOVERY_REQUIRED)

    def test_completed_reboot_requires_fresh_local_and_peer_health(self):
        self.s3_health_pending()
        self.model.apply(OtaEvent.HEALTH_CHECK, role=Role.S3, health_ok=True, peer_health_ok=True)
        self.model = OtaCoordinator.restore(self.plan, self.model.snapshot())
        self.model.apply(OtaEvent.REBOOT)
        self.assertEqual(self.model.state, OtaState.S3_HEALTH_PENDING)
        self.model.apply(OtaEvent.HEALTH_CHECK, role=Role.S3, health_ok=True, peer_health_ok=False)
        self.assertEqual(self.model.state, OtaState.RECOVERY_REQUIRED)

    def test_journal_rejects_wrong_plan_and_fabricated_inconsistent_state(self):
        self.companion_healthy()
        snapshot = self.model.snapshot()
        with self.assertRaises(ValueError):
            OtaCoordinator.restore(replace(self.plan, s3_hardware="different"), snapshot)
        for changes in (
            {"companion_confirmed": False},
            {"state": "complete", "s3_confirmed": False},
            {"state": "unknown"},
            {"companion_confirmed": "true"},
            {"recovery_order": ["s3"]},
        ):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                OtaCoordinator.restore(self.plan, {**snapshot, **changes})

    def test_recovery_journal_preserves_original_failure_after_rejected_events(self):
        self.s3_health_pending()
        self.model.apply(OtaEvent.FAILURE)
        self.model.apply(OtaEvent.HEALTH_CHECK, role=Role.S3, health_ok=True, peer_health_ok=True)
        restored = OtaCoordinator.restore(self.plan, self.model.snapshot())
        self.assertEqual(restored.recovery_reason, "reported_failure")
        self.assertEqual(restored.recovery_order, (Role.S3, Role.COMPANION))
        self.assertFalse(restored.apply(OtaEvent.REBOOT).accepted)
        self.assertEqual(restored.state, OtaState.RECOVERY_REQUIRED)


if __name__ == "__main__":
    unittest.main()

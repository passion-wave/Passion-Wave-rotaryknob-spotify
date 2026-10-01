"""Real temporary build metadata; esptool is mocked and no USB is accessed."""
from contextlib import redirect_stderr, redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location(
    'check_firmware_versions',
    Path(__file__).resolve().parents[2] / 'tools' / 'check_firmware_versions.py')
versions = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(versions)


class FirmwareVersionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='pw-image-version-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.version = self.root / 'VERSION'
        self.version.write_text('0.1.0-dev.4\n')
        self.build = self.root / 's3'
        self.build.mkdir()
        self.metadata = {'version': '1.2', 'project_version': '0.1.0-dev.4',
                         'project_name': 'passionwave_spotify', 'target': 'esp32s3',
                         'app_bin': 'passionwave_spotify.bin'}
        self.write_metadata()
        self.image = self.build / self.metadata['app_bin']
        self.image.write_bytes(b'not-a-real-image; subprocess-is-mocked')
        self.result = SimpleNamespace(returncode=0, stdout=(
            'esptool.py v4\nProject name: passionwave_spotify\nApp version: 0.1.0-dev.4\n'),
            stderr='')

    def write_metadata(self):
        (self.build / 'project_description.json').write_text(json.dumps(self.metadata))

    def run_check(self, *directories):
        self.stdout, self.stderr = io.StringIO(), io.StringIO()
        with patch.object(versions, 'VERSION_FILE', self.version), \
                patch.object(versions.subprocess, 'run', return_value=self.result) as process, \
                redirect_stdout(self.stdout), redirect_stderr(self.stderr):
            status = versions.main([str(p) for p in directories or (self.build,)])
        self.process = process
        return status

    def test_matching_metadata_and_image_use_official_image_info_only(self):
        self.assertEqual(self.run_check(), 0)
        self.process.assert_called_once_with(
            [sys.executable, '-m', 'esptool', 'image_info', '--version', '2', str(self.image)],
            capture_output=True, text=True, timeout=30, check=False)
        self.assertIn('passionwave_spotify 0.1.0-dev.4', self.stdout.getvalue())
        self.assertEqual(self.stderr.getvalue(), '')

    def test_stale_cmake_version_rejected_before_image(self):
        self.metadata['project_version'] = '0.1.0-dev.1'
        self.write_metadata()
        self.assertEqual(self.run_check(), 1)
        self.process.assert_not_called()
        self.assertIn('CMake project_version', self.stderr.getvalue())

    def test_stale_actual_binary_rejected_even_with_current_cmake_metadata(self):
        self.result.stdout = self.result.stdout.replace('0.1.0-dev.4', '0.1.0-dev.1')
        self.assertEqual(self.run_check(), 1)
        self.assertIn('binary App version', self.stderr.getvalue())

    def test_missing_image_rejected(self):
        self.image.unlink()
        self.assertEqual(self.run_check(), 1)
        self.process.assert_not_called()

    def test_wrong_project_or_wrong_project_target_rejected(self):
        self.result.stdout = self.result.stdout.replace('passionwave_spotify', 'passionwave_companion')
        self.assertEqual(self.run_check(), 1)
        self.assertIn('binary Project name', self.stderr.getvalue())
        self.metadata['target'] = 'esp32'
        self.write_metadata()
        self.assertEqual(self.run_check(), 1)
        self.process.assert_not_called()

    def test_missing_ambiguous_and_inexact_image_fields_rejected(self):
        original = self.result.stdout
        for output in ('unrecognized output', original + 'App version: 0.1.0-dev.4\n',
                       original.replace('App version: 0.1.0-dev.4', 'App version: 0.1.0-dev.4-extra')):
            with self.subTest(output=output):
                self.result.stdout = output
                self.assertEqual(self.run_check(), 1)

    def test_esptool_failure_does_not_echo_its_raw_output(self):
        self.result.returncode = 2
        self.result.stdout = self.result.stderr = 'UNEXPECTED-RAW-OUTPUT'
        self.assertEqual(self.run_check(), 1)
        self.assertNotIn('UNEXPECTED-RAW-OUTPUT', self.stdout.getvalue() + self.stderr.getvalue())

    def test_missing_or_invalid_json_fails_without_subprocess(self):
        metadata = self.build / 'project_description.json'
        for content in ('{invalid', '[]', '{"project_version":null}'):
            with self.subTest(content=content):
                metadata.write_text(content)
                self.assertEqual(self.run_check(), 1)
                self.process.assert_not_called()
        metadata.unlink()
        self.assertEqual(self.run_check(), 1)
        self.process.assert_not_called()

    def test_all_requested_build_directories_checked_after_one_fails(self):
        missing = self.root / 'missing-build'
        self.assertEqual(self.run_check(missing, self.build), 1)
        self.process.assert_called_once()
        self.assertIn('OK', self.stdout.getvalue())

    def test_timeout_returns_short_failure(self):
        with patch.object(versions.subprocess, 'run', side_effect=subprocess.TimeoutExpired(['fake'], 30)):
            with self.assertRaisesRegex(versions.VersionError, 'could not finish'):
                versions.check_build(self.build, '0.1.0-dev.4')


if __name__ == '__main__':
    unittest.main()

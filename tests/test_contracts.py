from copy import deepcopy
import unittest
from tools.check import read_json, validate_config, validate_manifest

class ConfigurationContractTests(unittest.TestCase):
    def setUp(self): self.data = read_json('examples/config.example.json')
    def test_secret_cannot_enter_public_configuration(self):
        self.data['refresh_token'] = 'must-not-export'
        with self.assertRaises(ValueError): validate_config(self.data)
    def test_approval_cannot_be_self_enabled(self):
        self.data['settings']['spotify_approved'] = True
        with self.assertRaises(ValueError): validate_config(self.data)
    def test_duplicate_favorite_rejected(self):
        self.data['catalog']['favorites'] *= 2
        with self.assertRaises(ValueError): validate_config(self.data)
    def test_show_cannot_masquerade_as_playlist(self):
        self.data['catalog']['favorites'][0]['kind'] = 'spotify_show'
        with self.assertRaises(ValueError): validate_config(self.data)
    def test_boolean_is_not_numeric_volume(self):
        self.data['settings']['volume_limit_percent'] = True
        with self.assertRaises(ValueError): validate_config(self.data)
    def test_uri_credentials_rejected(self):
        self.data['catalog']['stations'][0]['url'] = 'https://user:pass@example.invalid/radio'
        with self.assertRaises(ValueError): validate_config(self.data)
    def test_examples_are_valid_data(self): validate_config(self.data)

class ManifestContractTests(unittest.TestCase):
    def setUp(self): self.data = read_json('examples/update-manifest.example.json')
    def test_role_duplication_rejected(self):
        self.data['images'][1]['role'] = 'companion_esp32'
        with self.assertRaises(ValueError): validate_manifest(self.data)
    def test_pair_release_mismatch_rejected(self):
        self.data['images'][1]['version'] = '0.2.0'
        with self.assertRaises(ValueError): validate_manifest(self.data)
    def test_compatibility_range_cannot_allocate_unbounded_memory(self):
        self.data['images'][0]['accepted_peer_protocol_max'] = 65000
        with self.assertRaises(ValueError): validate_manifest(self.data)
    def test_example_or_cleared_example_flag_never_authorizes_install(self):
        for marker in [True, False]:
            self.data['example_only'] = marker
            with self.assertRaises(ValueError): validate_manifest(self.data, installable=True)
    def test_structural_example_valid(self): validate_manifest(self.data)

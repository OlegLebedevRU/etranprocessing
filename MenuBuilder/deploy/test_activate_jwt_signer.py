import importlib.util
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "activation", Path(__file__).with_name("activate_jwt_signer.py")
)
activation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(activation)


class ActivationTests(unittest.TestCase):
    def test_nginx_rotation_changes_only_public_key(self):
        original = (
            'route unchanged; auth_jwt_key "-----BEGIN PUBLIC KEY-----\n'
            'old\n-----END PUBLIC KEY-----"; tail;'
        )
        new = "-----BEGIN PUBLIC KEY-----\nnew\n-----END PUBLIC KEY-----\n"
        result = activation.replace_public_key(original, new)
        self.assertEqual(result, original.replace("old", "new"))
        for invalid in ("no key", original + original):
            with self.assertRaises(ValueError):
                activation.replace_public_key(invalid, new)

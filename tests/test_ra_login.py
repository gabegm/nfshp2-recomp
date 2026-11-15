"""The one-time login keeps the password out of the saved credentials."""
import importlib.util
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


class Terminal:
    def isatty(self):
        return True


class Response:
    def __enter__(self):
        return io.BytesIO(json.dumps({"Success": True, "User": "Player", "Token": "private-token"}).encode())

    def __exit__(self, *args):
        return False


class LoginTests(unittest.TestCase):
    def test_login_saves_only_token_with_private_permissions(self):
        path = Path(__file__).resolve().parents[1] / 'scripts/ra-login.py'
        spec = importlib.util.spec_from_file_location('ra_login', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as folder:
            credentials = Path(folder) / 'ra-credentials'
            with patch.object(sys, 'argv', ['ra-login.py', str(credentials)]), \
                    patch.object(sys, 'stdin', Terminal()), \
                    patch('builtins.input', return_value='Player'), \
                    patch('getpass.getpass', return_value='secret-password'), \
                    patch('urllib.request.urlopen', return_value=Response()) as send:
                self.assertEqual(module.main(), 0)
            self.assertEqual(credentials.read_text(), 'Player\nprivate-token\n')
            self.assertEqual(os.stat(credentials).st_mode & 0o777, 0o600)
            self.assertIn(b'r=login2', send.call_args.args[0].data)
            self.assertIn(b'p=secret-password', send.call_args.args[0].data)


if __name__ == '__main__':
    unittest.main()

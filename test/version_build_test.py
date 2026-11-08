"""Exercise build numbering without consuming real firmware build numbers."""
import contextlib
from io import StringIO
from pathlib import Path
import runpy
import tempfile
import unittest

configure_version = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'version_build.py'))['configure_version']


class BuildEnvironment(dict):
    def __init__(self, config):
        super().__init__(PIOENV='esp32dev')
        self.config = config

    def subst(self, value):
        assert value == '$PROJECT_CONFIG'
        return str(self.config)

    def IsCleanTarget(self):
        return False

    def IsIntegrationDump(self):
        return False

    def Append(self, **values):
        self.update(values)


class BuildVersionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'platformio.ini'
        self.original = ('; Keep comments and unrelated settings\n[env:esp32dev]\n'
                         'custom_version = 1.1.9 # Major.Feature.Build\nplatform = espressif32\n'
                         '[env:other]\ncustom_version = 2.0.0\n')
        self.path.write_text(self.original)

    def run_build(self, targets=(), release_version=None):
        env = BuildEnvironment(self.path)
        with contextlib.redirect_stdout(StringIO()):
            configure_version(env, targets, release_version)
        return env

    def test_consecutive_builds_increment_and_share_version_with_compiler_and_artifacts(self):
        for number in (10, 11):
            env = self.run_build()
            version = f'1.1.{number}'
            self.assertEqual(self.path.read_text(), self.original.replace('1.1.9', version))
            self.assertEqual(env['FIRMWARE_VERSION'], version)
            defines = dict(env['CPPDEFINES'])
            self.assertEqual(defines['STRVERSION'], '\\"' + version + '\\"')
            self.assertRegex(defines['STRBUILDDATE'], r'\d{4}\.\d{2}\.\d{2}')

    def test_non_firmware_commands_do_not_consume_build_numbers(self):
        for target in ('clean', 'cleanall', 'idedata', '__idedata', 'envdump', 'nobuild',
                       'buildfs', 'uploadfs', 'uploadfsota', 'erase', 'compiledb', 'sizedata'):
            with self.subTest(target=target):
                self.assertEqual(self.run_build([target])['FIRMWARE_VERSION'], '1.1.9')
                self.assertEqual(self.path.read_text(), self.original)

    def test_upload_and_explicit_build_increment(self):
        self.assertEqual(self.run_build(['upload'])['FIRMWARE_VERSION'], '1.1.10')
        self.assertEqual(self.run_build(['buildprog'])['FIRMWARE_VERSION'], '1.1.11')

    def test_invalid_version_does_not_modify_config(self):
        invalid = self.original.replace('1.1.9', 'invalid')
        self.path.write_text(invalid)
        with self.assertRaises(ValueError):
            self.run_build()
        self.assertEqual(self.path.read_text(), invalid)

    def test_release_starts_at_zero_then_normal_builds_increment(self):
        env = self.run_build(release_version='1.4.0')
        self.assertEqual(env['FIRMWARE_VERSION'], '1.4.0')
        self.assertEqual(self.path.read_text(), self.original.replace('1.1.9', '1.4.0'))
        self.assertEqual(self.run_build()['FIRMWARE_VERSION'], '1.4.1')

    def test_invalid_release_does_not_modify_config(self):
        for version in ('1.4', '-1.4.0', '1.4.0junk', ''):
            with self.assertRaises(ValueError):
                self.run_build(release_version=version)
            self.assertEqual(self.path.read_text(), self.original)

    def test_clean_does_not_apply_release_version(self):
        self.assertEqual(self.run_build(['clean'], '1.4.0')['FIRMWARE_VERSION'], '1.1.9')
        self.assertEqual(self.path.read_text(), self.original)


if __name__ == '__main__':
    unittest.main()

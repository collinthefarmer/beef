"""Run the real installer against disposable mod directories through both copy paths."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NAME = 'Example'
PLUGIN = Path('SKSE/Plugins')
INI = PLUGIN / f'{NAME}.ini'
DLL = PLUGIN / f'{NAME}.dll'
RECIPES = PLUGIN / NAME / 'recipes'


class InstallTests(unittest.TestCase):
    def fixture(self, root, backend):
        repository = root / 'source tree'
        repository.mkdir()
        shutil.copy2(ROOT / 'install.sh', repository / 'install.sh')
        (repository / 'tools').mkdir()
        shutil.copy2(ROOT / 'tools/mo2-mods-dir.sh', repository / 'tools/mo2-mods-dir.sh')
        (repository / 'CMakeLists.txt').write_text(f'project({NAME} VERSION 0.1.0)\n')
        stage = repository / 'dist' / NAME
        (stage / PLUGIN).mkdir(parents=True)
        (stage / DLL).write_text('new binary')
        (stage / INI).write_text('default settings')
        binaries = root / 'bin'
        binaries.mkdir()
        for tool in ('bash', 'dirname', 'sed', 'mkdir', 'cp', 'tar') + (('rsync',) if backend == 'rsync' else ()):
            executable = shutil.which(tool)
            self.assertIsNotNone(executable, f'{tool} required; run inside nix develop')
            (binaries / tool).symlink_to(executable)
        mods = root / 'MO2 mods'
        mods.mkdir()
        env = dict(os.environ, PATH=str(binaries), MO2_MODS_DIR=str(mods))
        return repository, stage, mods / NAME, env, binaries

    def run_install(self, repository, env):
        return subprocess.run([str(repository / 'install.sh')], env=env,
                              text=True, capture_output=True, cwd=repository.parent)

    def test_fresh_install_and_upgrade_preserve_authored_files(self):
        for backend in ('rsync', 'tar'):
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as folder:
                repository, stage, target, env, _ = self.fixture(Path(folder), backend)
                (stage / RECIPES).mkdir(parents=True)
                (stage / RECIPES / 'stale.json').write_text('unwanted staged recipe')
                result = self.run_install(repository, env)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((target / INI).read_text(), 'default settings')
                self.assertEqual((target / DLL).read_text(), 'new binary')
                self.assertFalse((target / RECIPES).exists())
                (target / INI).write_text('user settings')
                (target / RECIPES).mkdir(parents=True)
                (target / RECIPES / 'stale.json').write_text('authored recipe')
                (target / 'user-extra.txt').write_text('keep')
                (stage / DLL).write_text('upgraded binary')
                result = self.run_install(repository, env)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((target / DLL).read_text(), 'upgraded binary')
                self.assertEqual((target / INI).read_text(), 'user settings')
                self.assertEqual((target / RECIPES / 'stale.json').read_text(), 'authored recipe')
                self.assertEqual((target / 'user-extra.txt').read_text(), 'keep')

    def test_missing_inputs_leave_target_untouched(self):
        for backend in ('rsync', 'tar'):
            for missing in (DLL, INI):
                with self.subTest(backend=backend, missing=missing), tempfile.TemporaryDirectory() as folder:
                    repository, stage, target, env, _ = self.fixture(Path(folder), backend)
                    (stage / missing).unlink()
                    result = self.run_install(repository, env)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertFalse(target.exists())
                    self.assertNotIn('installed to', result.stdout)

    def test_mods_directory_comes_from_local_env_when_unset(self):
        with tempfile.TemporaryDirectory() as folder:
            repository, _, target, env, _ = self.fixture(Path(folder), 'tar')
            mods = env.pop('MO2_MODS_DIR')
            (repository / 'local.env').write_text(f'MO2_MODS_DIR="{mods}"\n')
            result = self.run_install(repository, env)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((target / DLL).read_text(), 'new binary')

    def test_unset_mods_directory_is_refused(self):
        with tempfile.TemporaryDirectory() as folder:
            repository, _, _, env, _ = self.fixture(Path(folder), 'tar')
            del env['MO2_MODS_DIR']
            result = self.run_install(repository, env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('local.env', result.stderr)
            self.assertNotIn('installed to', result.stdout)

    def test_missing_mods_directory_is_refused_and_not_created(self):
        with tempfile.TemporaryDirectory() as folder:
            repository, _, _, env, _ = self.fixture(Path(folder), 'tar')
            missing = Path(folder) / 'not a mods folder'
            env['MO2_MODS_DIR'] = str(missing)
            result = self.run_install(repository, env)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('not an existing directory', result.stderr)
            self.assertFalse(missing.exists())

    def test_copy_failure_is_not_reported_as_success(self):
        for backend in ('rsync', 'tar'):
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as folder:
                repository, _, target, env, binaries = self.fixture(Path(folder), backend)
                failing = binaries / backend
                failing.unlink()
                failing.write_text('#!/usr/bin/env bash\nexit 23\n')
                failing.chmod(0o755)
                result = self.run_install(repository, env)
                self.assertEqual(result.returncode, 23, result.stderr)
                self.assertIn('installation incomplete', result.stderr)
                self.assertNotIn('installed to', result.stdout)
                self.assertFalse((target / INI).exists())

    def test_existing_ini_symlink_is_preserved(self):
        for backend in ('rsync', 'tar'):
            with self.subTest(backend=backend), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                repository, _, target, env, _ = self.fixture(root, backend)
                (target / PLUGIN).mkdir(parents=True)
                external = root / 'user.ini'
                (target / INI).symlink_to(external)
                result = self.run_install(repository, env)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue((target / INI).is_symlink())
                self.assertFalse(external.exists())
                external.write_text('shared settings')
                result = self.run_install(repository, env)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(external.read_text(), 'shared settings')

    def test_default_ini_copy_failure_is_reported(self):
        with tempfile.TemporaryDirectory() as folder:
            repository, _, target, env, binaries = self.fixture(Path(folder), 'tar')
            failing = binaries / 'cp'
            failing.unlink()
            failing.write_text('#!/usr/bin/env bash\nexit 24\n')
            failing.chmod(0o755)
            result = self.run_install(repository, env)
            self.assertEqual(result.returncode, 24, result.stderr)
            self.assertIn('installation incomplete', result.stderr)
            self.assertNotIn('installed to', result.stdout)
            self.assertTrue((target / DLL).is_file())


if __name__ == '__main__':
    unittest.main()

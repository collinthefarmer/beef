"""Check the gate's comment scan, layer graph and stage flow."""
import importlib.util
import io
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('gate', ROOT / 'tools/gate.py')
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)

NOTICE = '// GPL-3.0-only with the additional permission in COPYING.md.\n'
DERIVED = ''.join(line + '\n' for line in gate.LEGAL_NOTICES[1])


def comment_lines(text: str) -> list[int]:
    return [int(finding.split(':')[1]) for finding in gate.comment_findings('src/A.cpp', text)]


class CommentTests(unittest.TestCase):
    def test_literals_are_not_comments(self) -> None:
        self.assertEqual(comment_lines('a = "x // y"; b = \'"\'; c = "q\\"//";\n'), [])
        self.assertEqual(comment_lines('auto s = R"x(a )" // "b" /* c)x"; // real\nint after;\n'), [1])
        for prefix in ('u8R', 'LR', 'uR', 'UR'):
            self.assertEqual(comment_lines(prefix + '"(//)"'), [])
        self.assertEqual(comment_lines('BAR"(" // c'), [1])
        self.assertEqual(comment_lines("int n = 1'000'000; // count\nchar c = u8'/';\n"), [1])

    def test_block_comments_report_their_first_line(self) -> None:
        self.assertEqual(comment_lines('int a; /* one\ntwo */ int b;\n// three\n'), [1, 3])

    def test_notices_are_exempt_only_when_exact_and_first(self) -> None:
        self.assertEqual(comment_lines(NOTICE + 'int a; // NOLINT\n'), [])
        self.assertEqual(comment_lines(DERIVED + 'int a;\n'), [])
        self.assertEqual(comment_lines(NOTICE + NOTICE), [2])
        self.assertEqual(comment_lines('#pragma once\n' + NOTICE), [2])
        self.assertEqual(comment_lines(NOTICE.rstrip() + ' extra\n'), [1])
        self.assertEqual(comment_lines(gate.LEGAL_NOTICES[1][0] + '\n'), [1])

    def test_malformed_text_never_raises(self) -> None:
        for text in ('"unterminated', "'", 'R"', 'R"abc', 'R"(no end', '/* open', '//', '\\', "1'"):
            with self.subTest(text=text):
                gate.comment_findings('src/A.cpp', text)
                gate.layer_findings('src/recipe/A.cpp', text)


class LayerTests(unittest.TestCase):
    def findings(self, sources: dict[str, str]) -> list[str]:
        return [finding for name, text in sources.items() for finding in gate.layer_findings(name, text)]

    def test_allowed_edges_pass(self) -> None:
        self.assertEqual(self.findings({
            'src/recipe/A.cpp': '#include "Core.h"\n#include "recipe/B.h"\n',
            'src/engine/A.cpp': '#include "render/C.h"\n#include "PCH.h"\n#include <RE/Skyrim.h>\n',
            'src/main.cpp': '#include "BuildIdentity.h"\n#include "menu/Menu.h"\n',
            'src/recipe/B.cpp': '// #include "engine/Manager.h"\nauto s = "RE::Actor";\n',
            'tests/recipe/A.cpp': '#include "engine/Manager.h"\n'}), [])

    def test_upward_rootless_rowless_and_engine_names_are_reported(self) -> None:
        self.assertEqual(self.findings({
            'src/recipe/A.cpp': '#include "engine/Manager.h"\nREL::ID id;\n',
            'src/mesh/A.cpp': '#include "Loose.h"\n',
            'src/validator/Main.cpp': '#include "engine/TextFile.h"\n',
            'src/studio/A.cpp': 'SKSE::Load();\n',
            'src/Core.h': 'RE::Actor *a;\n',
            'src/newlayer/A.cpp': '',
            'src/Stray.cpp': ''}), [
            'src/recipe/A.cpp:1: recipe may not include "engine/Manager.h"',
            'src/recipe/A.cpp:2: engine-free code names an engine symbol',
            'src/mesh/A.cpp:1: mesh may not include "Loose.h"',
            'src/validator/Main.cpp:1: validator may not include "engine/TextFile.h"',
            'src/studio/A.cpp:1: engine-free code names an engine symbol',
            'src/Core.h:1: engine-free code names an engine symbol',
            'src/newlayer/A.cpp: newlayer has no row in the layer graph in tools/gate.py',
            'src/Stray.cpp: Stray.cpp has no row in the layer graph in tools/gate.py'])

    @unittest.skipUnless((ROOT / '.git').exists(), 'lists sources through git')
    def test_current_tree_passes_the_layer_and_comment_rules(self) -> None:
        with patch.object(gate, 'ROOT', ROOT):
            sources = gate.working_sources()
        self.assertEqual([f for name, text in sources.items()
                          for f in gate.layer_findings(name, text) + gate.comment_findings(name, text)], [])


class StageTests(unittest.TestCase):
    def setUp(self) -> None:
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        self.root = Path(folder.name)
        shutil.copy2(ROOT / '.clang-format', self.root / '.clang-format')
        (self.root / 'src').mkdir()
        (self.root / 'src/Core.h').write_text(NOTICE + '#pragma once\n')
        self.git('init', '-q')
        self.git('add', '.')

    def git(self, *args: str) -> None:
        subprocess.run(['git', *args], cwd=self.root, check=True, capture_output=True)

    def commit(self) -> str:
        self.git('add', '.')
        self.git('-c', 'user.name=gate', '-c', 'user.email=gate@test', 'commit', '-qm', 'state')
        return subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=self.root, text=True).strip()

    def gate(self, stage: str, stdin: str = '',
             extra: tuple[str, ...] = ()) -> tuple[str | None, list[tuple[str, ...]]]:
        previous = Path.cwd()
        try:
            with patch.object(gate, 'ROOT', self.root), patch.object(gate, 'run') as run, \
                    patch.dict(os.environ, {'BEEF_DEV_SHELL': '1'}), \
                    patch('sys.argv', ['gate.py', stage, *extra]), patch('sys.stdin', io.StringIO(stdin)):
                try:
                    gate.main()
                    error = None
                except SystemExit as exit:
                    error = str(exit.code)
                return error, [call.args for call in run.call_args_list]
        finally:
            os.chdir(previous)

    def test_commit_checks_the_staged_blob_and_later_stages_build(self) -> None:
        self.assertEqual(self.gate('commit'), (None, []))
        (self.root / 'src/Core.h').write_text(NOTICE + '#pragma once\n// prose\n')
        self.assertEqual(self.gate('commit'), (None, []))
        error, calls = self.gate('push')
        self.assertIn('src/Core.h:3: // prose', error)
        self.assertEqual(calls, [])
        self.git('add', '.')
        self.assertIn('src/Core.h:3', self.gate('commit')[0])
        (self.root / 'src/Core.h').write_text(NOTICE + '#pragma once\nint  badly_formatted ;\n')
        self.assertIn('src/Core.h: needs clang-format', self.gate('push')[0])
        (self.root / 'src/Core.h').write_text(NOTICE + '#pragma once\n')
        self.assertEqual(self.gate('push')[1][-1], ('ctest', '--preset', 'native-sanitized'))
        calls = self.gate('release')[1]
        self.assertIn(('cmake', '--build', '--preset', 'windows-release', '--target', 'all'), calls)
        self.assertEqual(calls[-1][1:], ('tools/tidy.py', '--check'))


class PushRecordTests(StageTests):
    def push_line(self, commit: str) -> str:
        return f'refs/heads/main {commit} refs/heads/main {gate.ZERO_COMMIT}\n'

    def test_prepush_needs_a_recorded_pass_for_the_pushed_commit(self) -> None:
        head = self.commit()
        error, calls = self.gate('prepush', self.push_line(head))
        self.assertIn('Push blocked', error)
        self.assertEqual(calls, [])
        self.assertIsNone(self.gate('push')[0])
        self.assertEqual(self.gate('prepush', self.push_line(head)), (None, []))
        (self.root / 'src/Core.h').write_text(NOTICE + '#pragma once\nint value = 1;\n')
        newer = self.commit()
        self.assertIn('Push blocked', self.gate('prepush', self.push_line(newer))[0])

    def test_a_dirty_tree_is_never_recorded(self) -> None:
        head = self.commit()
        (self.root / 'src/Core.h').write_text(NOTICE + '#pragma once\nint value = 1;\n')
        self.assertIsNone(self.gate('push')[0])
        self.assertIn('Push blocked', self.gate('prepush', self.push_line(head))[0])

    def test_deleting_a_remote_branch_needs_no_pass(self) -> None:
        self.commit()
        deletion = f'(delete) {gate.ZERO_COMMIT} refs/heads/old {gate.ZERO_COMMIT}\n'
        self.assertEqual(self.gate('prepush', deletion), (None, []))

    def test_ship_refuses_a_dirty_tree_and_pushes_after_the_gate(self) -> None:
        self.commit()
        (self.root / 'src/Core.h').write_text(NOTICE + '#pragma once\nint value = 1;\n')
        error, calls = self.gate('ship')
        self.assertIn('Ship blocked', error)
        self.assertEqual(calls, [])
        self.commit()
        error, calls = self.gate('ship', extra=('origin', 'main'))
        self.assertIsNone(error)
        self.assertEqual(calls[-1], ('git', 'push', 'origin', 'main'))
        self.assertEqual(calls[-2], ('ctest', '--preset', 'native-sanitized'))


if __name__ == '__main__':
    unittest.main()

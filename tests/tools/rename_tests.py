"""Exercise the rename planner's parsing, symbol choice and edit application without clangd."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('rename', ROOT / 'tools/rename.py')
TOOL = importlib.util.module_from_spec(SPEC)
sys.modules['rename'] = TOOL
SPEC.loader.exec_module(TOOL)


def symbol(name, container, kind='Struct', path='src/studio/Rows.h', line=3, character=7):
    return TOOL.Symbol(name, container, kind, Path(path), TOOL.Position(line, character))


def edit(start_line, start_character, end_line, end_character, text):
    return TOOL.TextEdit(TOOL.Position(start_line, start_character), TOOL.Position(end_line, end_character), text)


class ParseTests(unittest.TestCase):
    def test_qualified_name_and_matching_new_qualifier(self):
        request = TOOL.parse_request('Studio::SlotRow', 'Studio::SlotLine', 'Struct', 'w', ROOT)
        self.assertEqual(request.target, TOOL.ByName(('Studio',), 'SlotRow', 'Struct'))
        self.assertEqual(request.new, 'SlotLine')

    def test_new_qualifier_that_moves_the_symbol_is_rejected(self):
        with self.assertRaisesRegex(TOOL.RenameError, 'another scope'):
            TOOL.parse_request('Studio::SlotRow', 'Menu::SlotRow', None, 'w', ROOT)

    def test_position_counts_from_one_and_stays_in_the_tree(self):
        request = TOOL.parse_request('src/studio/Rows.cpp:40:12', 'value', None, 'w', ROOT)
        self.assertEqual(request.target, TOOL.ByPosition(Path('src/studio/Rows.cpp'), TOOL.Position(39, 11)))
        for outside in ('/etc/passwd:1:1', 'src/extern/SKSEMenuFramework.h:1:1', '../x.cpp:1:1'):
            with self.subTest(outside=outside), self.assertRaisesRegex(TOOL.RenameError, 'outside'):
                TOOL.parse_request(outside, 'value', None, 'w', ROOT)
        with self.assertRaisesRegex(TOOL.RenameError, 'count from 1'):
            TOOL.parse_request('src/a.cpp:0:1', 'value', None, 'w', ROOT)

    def test_garbage_names_and_kinds_are_rejected(self):
        for old, new, kind in (('1x', 'y', None), ('a::', 'b', None), ('a b', 'c', None),
                               ('a', 'b-c', None), ('a', 'b', 'Struk'), ('src/a.cpp:1:1', 'b', 'Struct')):
            with self.subTest(old=old, new=new, kind=kind), self.assertRaises(TOOL.RenameError):
                TOOL.parse_request(old, new, kind, 'w', ROOT)


class SymbolTests(unittest.TestCase):
    def test_qualifier_matches_whole_scopes_only(self):
        wanted = TOOL.ByName(('Studio',), 'Row', None)
        self.assertTrue(TOOL.matches(symbol('Row', 'BetterEnchantmentEffects::Studio'), wanted))
        self.assertTrue(TOOL.matches(symbol('Row', 'Studio'), wanted))
        self.assertFalse(TOOL.matches(symbol('Row', 'BetterEnchantmentEffects::NotStudio'), wanted))

    def test_ambiguous_and_missing_names_fail_with_the_candidates(self):
        candidates = [symbol('Row', 'A::Studio'), symbol('Row', 'A::Menu', path='src/menu/Rows.h')]
        with self.assertRaisesRegex(TOOL.RenameError, r'2 symbols match Row(.|\n)*src/menu/Rows.h'):
            TOOL.choose_symbol(candidates, TOOL.ByName((), 'Row', None), 'w')
        with self.assertRaisesRegex(TOOL.RenameError, 'no symbol named Studio::Line'):
            TOOL.choose_symbol(candidates, TOOL.ByName(('Studio',), 'Line', None), 'w')
        self.assertEqual(TOOL.choose_symbol(candidates + candidates[:1], TOOL.ByName(('Studio',), 'Row', None), 'w'),
                         candidates[0])

    def test_symbols_outside_the_tree_are_dropped(self):
        entry = {'name': 'Row', 'kind': 23, 'containerName': 'RE',
                 'location': {'uri': (ROOT / 'src/extern/RE.h').as_uri(),
                              'range': {'start': {'line': 1, 'character': 2}, 'end': {'line': 1, 'character': 5}}}}
        self.assertIsNone(TOOL.symbol_from_lsp(entry))
        entry['location']['uri'] = (ROOT / 'src/studio/Rows.h').as_uri()
        self.assertEqual(TOOL.symbol_from_lsp(entry), symbol('Row', 'RE', line=1, character=2))
        with self.assertRaisesRegex(TOOL.RenameError, 'malformed'):
            TOOL.symbol_from_lsp({'name': 'Row', 'location': entry['location']})

    def test_a_position_that_names_a_symbol_declared_elsewhere_is_an_alias(self):
        def details(path, line, character):
            return {'id': 'x', 'name': 'Row',
                    'declarationRange': {'uri': (ROOT / path).as_uri(),
                                         'range': {'start': {'line': line, 'character': character},
                                                   'end': {'line': line, 'character': character + 3}}}}
        declaration = symbol('Row', 'A::Studio', path='src/studio/Rows.h', line=3, character=7)
        using = symbol('Row', 'A::Menu', kind='Namespace', path='src/menu/Stack.cpp', line=26, character=14)
        self.assertFalse(TOOL.is_alias(declaration, TOOL.declarations_from_lsp([details('src/studio/Rows.h', 3, 7)])))
        self.assertTrue(TOOL.is_alias(using, TOOL.declarations_from_lsp(
            [details('src/studio/Rows.h', 3, 7), details('src/menu/Stack.cpp', 26, 14)])))
        self.assertFalse(TOOL.is_alias(using, TOOL.declarations_from_lsp(None)))
        self.assertFalse(TOOL.is_alias(using, TOOL.declarations_from_lsp([{'id': 'x', 'name': 'Row'}])))
        with self.assertRaisesRegex(TOOL.RenameError, 'malformed'):
            TOOL.declarations_from_lsp([{'declarationRange': {'uri': 1}}])

    def test_stale_index_column_settles_on_the_nearest_spelling(self):
        text = 'int a;\nstruct Row { Row(); };\n'
        self.assertEqual(TOOL.settle_position(text, TOOL.Position(1, 5), 'Row'), TOOL.Position(1, 7))
        self.assertEqual(TOOL.settle_position(text, TOOL.Position(1, 12), 'Row'), TOOL.Position(1, 13))
        self.assertIsNone(TOOL.settle_position(text, TOOL.Position(0, 0), 'Row'))
        self.assertIsNone(TOOL.settle_position(text, TOOL.Position(9, 0), 'Row'))


class EditTests(unittest.TestCase):
    def test_edits_apply_right_to_left_and_keep_crlf(self):
        text = 'Row a;\r\nRow b; Row c;\r\n'
        result = TOOL.apply_text_edits(text, [edit(1, 7, 1, 10, 'Line'), edit(0, 0, 0, 3, 'Line'),
                                              edit(1, 0, 1, 3, 'Line')], 'w')
        self.assertEqual(result, 'Line a;\r\nLine b; Line c;\r\n')

    def test_non_ascii_columns_count_code_points(self):
        self.assertEqual(TOOL.apply_text_edits('"é🙂" Row;', [edit(0, 5, 0, 8, 'Line')], 'w'), '"é🙂" Line;')

    def test_overlapping_and_out_of_range_edits_fail(self):
        with self.assertRaisesRegex(TOOL.RenameError, 'overlapping'):
            TOOL.apply_text_edits('Row a;', [edit(0, 0, 0, 3, 'X'), edit(0, 2, 0, 4, 'Y')], 'w')
        with self.assertRaisesRegex(TOOL.RenameError, 'past the end of line 1'):
            TOOL.apply_text_edits('Row\nb', [edit(0, 2, 0, 9, 'X')], 'w')
        with self.assertRaisesRegex(TOOL.RenameError, 'line 5 is past'):
            TOOL.apply_text_edits('Row\n', [edit(4, 0, 4, 1, 'X')], 'w')

    def test_workspace_edit_outside_the_tree_or_with_file_operations_fails(self):
        change = {'range': {'start': {'line': 0, 'character': 0}, 'end': {'line': 0, 'character': 3}}, 'newText': 'X'}
        parsed = TOOL.edits_from_workspace_edit({'changes': {(ROOT / 'src/a.cpp').as_uri(): [change]}})
        self.assertEqual(parsed, {Path('src/a.cpp'): [edit(0, 0, 0, 3, 'X')]})
        for uri in ((ROOT / 'decompiled/a.cpp').as_uri(), (ROOT.parent / 'other/a.cpp').as_uri()):
            with self.subTest(uri=uri), self.assertRaisesRegex(TOOL.RenameError, 'outside'):
                TOOL.edits_from_workspace_edit({'changes': {uri: [change]}})
        with self.assertRaisesRegex(TOOL.RenameError, 'document changes'):
            TOOL.edits_from_workspace_edit({'documentChanges': []})

    def test_word_at_reads_the_identifier_under_the_column(self):
        self.assertEqual(TOOL.word_at('int rowCount = 0;', TOOL.Position(0, 6), 'w'), 'rowCount')
        with self.assertRaisesRegex(TOOL.RenameError, 'no identifier'):
            TOOL.word_at('int rowCount = 0;', TOOL.Position(0, 13), 'w')


class ReportTests(unittest.TestCase):
    def test_leftover_search_matches_whole_words(self):
        text = '// Row is drawn\nRowCount x;\nauto r = "Row";\n'
        self.assertEqual(TOOL.find_word(text, 'Row'), [(1, '// Row is drawn'), (3, 'auto r = "Row";')])

    def test_unchanged_files_drop_out_of_the_diff(self):
        changes = TOOL.changed_files({Path('a.h'): 'Row\n', Path('b.h'): 'x\n'},
                                     {Path('a.h'): 'Line\n', Path('b.h'): 'x\n'})
        self.assertEqual([change.path for change in changes], [Path('a.h')])
        self.assertIn('-Row\n+Line', TOOL.render_diff(changes))

    def test_unindexed_sources_skip_headers_and_untouchable_trees(self):
        tracked = [Path(name) for name in ('src/a.cpp', 'src/a.h', 'src/extern/x.cpp', 'tests/b.cpp')]
        self.assertEqual(TOOL.unindexed_sources([Path('src/a.cpp')], tracked), [Path('tests/b.cpp')])

    def test_long_leftover_lists_are_truncated(self):
        leftovers = [TOOL.Leftover(Path('a.md'), number, 'Row') for number in range(1, 46)]
        report = TOOL.render_leftovers('Row', leftovers)
        self.assertIn('Row is still spelled in 45 place(s)', report)
        self.assertTrue(report.endswith('(5 more)'))


if __name__ == '__main__':
    unittest.main()

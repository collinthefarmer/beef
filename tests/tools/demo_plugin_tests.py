"""Exercise the narrow demo writer without redistributing Skyrim records."""
import importlib.util
import json
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('demo_plugin', ROOT / 'tools/demo-plugin.py')
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)


def fixture():
    armor = [(b'EDID', b'ArmorDwarvenCuirass\0'), (b'FULL', b'abcd'),
             (b'DESC', b'efgh'), (b'KSIZ', demo.u32(4)),
             (b'KWDA', struct.pack('<4I', 1, 2, 3, 4)),
             (b'MODL', demo.u32(0x3F7FE)), (b'DATA', b'12345678')]
    effect = [(b'EDID', b'EnchFortifyMagickaConstantSelf\0'),
              (b'FULL', b'1234'), (b'DNAM', b'5678'), (b'DATA', bytes(152))]
    ench = [(b'EDID', b'EnchArmorFortifyMagicka01\0'), (b'FULL', b'1234'),
            (b'ENIT', bytes(36)), (b'EFID', demo.u32(0x49504)),
            (b'EFIT', struct.pack('<fII', 20.0, 0, 0))]
    return b''.join(demo.group(k, demo.record(k, i, p)) for k, i, p in
                    [(b'ARMO', 0x1394D, armor), (b'MGEF', 0x49504, effect),
                     (b'ENCH', 0x49508, ench)])


class DemoPluginTests(unittest.TestCase):
    def setUp(self):
        self.master = fixture()
        self.output = demo.build(self.master)
        self.records = {i: (k, dict(demo.subrecords(b)), demo.subrecords(b))
                        for k, i, _, b in demo.records(self.output)}

    def test_header_and_new_records(self):
        self.assertEqual(set(self.records), {0, *demo.IDS.values()})
        header = self.records[0][1]
        self.assertEqual(header[b'MAST'], b'Skyrim.esm\0')
        version, count, next_id = struct.unpack('<fII', header[b'HEDR'])
        self.assertAlmostEqual(version, 1.7)
        self.assertEqual((count, next_id), (8, 0x804))
        self.assertEqual(self.output, demo.build(self.master))

    def test_armor_preserves_model_and_adds_required_keyword(self):
        armor = self.records[demo.IDS['armor']][1]
        self.assertEqual(armor[b'MODL'], demo.u32(0x3F7FE))
        self.assertEqual(armor[b'DATA'], b'12345678')
        self.assertEqual(struct.unpack('<5I', armor[b'KWDA']),
                         (1, 2, 3, 4, demo.IDS['keyword']))
        self.assertEqual(armor[b'EITM'], demo.u32(demo.IDS['enchantment']))
        self.assertTrue(armor[b'FULL'].endswith(b'\0'))
        self.assertEqual(armor[b'DESC'], b'\0')

    def test_secondary_effect_and_localized_text_conversion(self):
        parts = self.records[demo.IDS['enchantment']][2]
        self.assertEqual([struct.unpack('<I', v)[0] for k, v in parts if k == b'EFID'],
                         [0x493AA, demo.IDS['effect']])
        self.assertEqual([struct.unpack('<fII', v) for k, v in parts if k == b'EFIT'],
                         [(100.0, 0, 0), (25.0, 0, 0)])
        effect = self.records[demo.IDS['effect']][1]
        self.assertEqual(effect[b'DATA'], bytes(152))
        self.assertIn(b'<mag>', effect[b'DNAM'])
        self.assertTrue(effect[b'FULL'].endswith(b'\0'))

    def test_recipe_keys_refer_to_generated_records(self):
        recipe = json.loads((ROOT / 'recipes/examples/arcane-circuit.json').read_text())
        self.assertEqual(recipe['keys'], [
            {'keyword': f"0x800~{demo.PLUGIN}"},
            {'magicEffect': f"0x801~{demo.PLUGIN}"}])
        self.assertEqual(recipe['signals']['magnitude'], {'enchantment': 'magnitude'})

    def test_rejects_missing_and_truncated_records(self):
        for value in (b'', self.master[:-1], b'GRUP' + bytes(20)):
            with self.subTest(size=len(value)), self.assertRaises(ValueError):
                demo.build(value)
        with self.assertRaises(ValueError):
            demo.subrecords(b'EDID\x08\x00short')
        with self.assertRaises(ValueError):
            demo.subrecords(b'XXXX\x04\x00\x00\x01\x00\x00')


if __name__ == '__main__':
    unittest.main()

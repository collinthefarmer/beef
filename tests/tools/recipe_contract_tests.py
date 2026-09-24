"""Compare structural schema expectations with the plugin's native parser."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = None


def recipe(**fields):
    return dict({'format': 1, 'keys': ['default']}, **fields)


class RecipeContractTests(unittest.TestCase):
    def check_case(self, document, schema_valid, parser_valid):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'contract.json'
            path.write_text(json.dumps(document))
            schema = subprocess.run(['check-jsonschema', '--schemafile',
                                     str(ROOT / 'schema/recipe.schema.json'), str(path)],
                                    text=True, capture_output=True)
            self.assertIn(schema.returncode, (0, 1), schema.stdout + schema.stderr)
            self.assertEqual(schema.returncode == 0, schema_valid, schema.stdout + schema.stderr)
            parsed = subprocess.run([str(VALIDATOR), str(path)], text=True, capture_output=True)
            self.assertIn(parsed.returncode, (0, 1), parsed.stdout + parsed.stderr)
            self.assertEqual(parsed.returncode == 0, parser_valid, parsed.stdout + parsed.stderr)

    def test_required_fields_and_format(self):
        cases = [({}, False), ({'format': 1}, False),
                 ({'keys': ['default']}, False), (recipe(), True),
                 (recipe(keys=[]), False), (recipe(keys=['unknown']), False)]
        cases += [(recipe(format=value), type(value) is int and value == 1) for value in
                  (-1, 0, 1, 2, 4294967297, 18446744073709551615, '1', True)]
        for document, valid in cases:
            with self.subTest(document=document):
                self.check_case(document, valid, valid)

    def test_priority_boundaries(self):
        for value in (-2147483649, -2147483648, 0, 2147483647, 2147483648,
                      18446744073709551615, 1.5, '1', True):
            valid = type(value) is int and -2147483648 <= value <= 2147483647
            with self.subTest(value=value):
                self.check_case(recipe(priority=value), valid, valid)

    def test_optional_objects_and_unknown_fields(self):
        for document, valid in ((recipe(clock={}), True),
                                (recipe(clock={'speed': 1}), True),
                                (recipe(clock=[]), False),
                                (recipe(clock=None), False),
                                (recipe(clock=1), False),
                                (recipe(clock={'speed': 'fast'}), False),
                                (recipe(clock={'unknown': 1}), False),
                                (recipe(unknown=True), False),
                                (recipe(shell={}), True),
                                (recipe(outputs=[]), True)):
            with self.subTest(document=document):
                self.check_case(document, valid, valid)

    def test_cluster_defaults_and_bounds(self):
        cases = [({}, True)]
        cases += [({'clusters': value}, 1 <= value <= 8) for value in (0, 1, 8, 9)]
        cases += [({'iterations': value}, 1 <= value <= 256) for value in (0, 1, 256, 257)]
        cases += [({'seed': value}, 0 <= value <= 2147483647)
                  for value in (-1, 0, 2147483647, 2147483648, 4294967297)]
        cases += [({'weights': {'roughness': value}}, 0 <= value <= 10)
                  for value in (-0.1, 0, 10, 10.1)]
        for settings, valid in cases:
            with self.subTest(settings=settings):
                self.check_case(recipe(sources={'clusters': {'materialClusters': settings}}), valid, valid)

    def test_partition_boundaries_before_narrowing(self):
        for value in (29, 30, 61, 62, 4294967326, -4294967266):
            with self.subTest(value=value):
                valid = 30 <= value <= 61
                self.check_case(recipe(sources={'part': {'bake': {'partition': value}}}), valid, valid)

    def test_floating_point_range(self):
        for value in (-3.4028234663852886e38, 3.4028234663852886e38, -1e39, 1e39):
            valid = abs(value) <= 3.4028234663852886e38
            documents = [recipe(clock={'speed': value}),
                         recipe(signals={'scalar': {'constant': value}}),
                         recipe(signals={'vector': {'constant': [0, value]}}),
                         recipe(shell={'pose': {'scalePoint': [0, value, 0]}}),
                         recipe(outputs=[{'target': 'material', 'slot': 'emissive', 'strength': value}])]
            for document in documents:
                with self.subTest(document=document):
                    self.check_case(document, valid, valid)

    def test_values_previously_clamped(self):
        for value in (-0.1, 0, 1, 1.1):
            with self.subTest(alphaTest=value):
                self.check_case(recipe(shell={'alphaTest': value}), 0 <= value <= 1, 0 <= value <= 1)
        for value in (-1, 0, 1):
            with self.subTest(seed=value):
                self.check_case(recipe(signals={'noise': {'noise': {'seed': value}}}), value >= 0, value >= 0)
            with self.subTest(mip=value):
                self.check_case(recipe(sources={'image': {'image': {'path': 'test.dds', 'mip': value}}}), value >= 0, value >= 0)
        for value in (0.29, 0.3, 1, 1.1):
            with self.subTest(minShare=value):
                light = {'target': 'light', 'bones': {'skinned': {'minShare': value}},
                         'color': [1, 1, 1], 'intensity': 1}
                self.check_case(recipe(outputs=[light]), 0.3 <= value <= 1, 0.3 <= value <= 1)

    def test_event_filter_bounds(self):
        for bounds, valid in (([None, None], True), ([0, 1], True),
                              ([None, 1e39], False), ([-1e39, None], False),
                              (['bad', None], False), ([None, True], False)):
            with self.subTest(bounds=bounds):
                self.check_case(recipe(signals={'event': {'trigger': {
                    'event': 'equip', 'filter': {'value': bounds}}}}), valid, valid)

    def test_required_output_fields(self):
        for slot, fields in {'emissive': {'strength': 1}, 'height': {'scale': 1},
                             'fuzz': {'color': [1, 1, 1], 'weight': 1},
                             'coat': {'roughness': 0.5, 'level': 1},
                             'subsurface': {'color': [1, 1, 1], 'thickness': 1}}.items():
            output = dict(target='material', slot=slot, **fields)
            with self.subTest(slot=slot, missing=None):
                self.check_case(recipe(outputs=[output]), True, True)
            for missing in fields:
                with self.subTest(slot=slot, missing=missing):
                    self.check_case(recipe(outputs=[{key: value for key, value in output.items() if key != missing}]), False, False)
        light = {'target': 'light', 'bones': {'named': ['NPC Spine [Spn0]']},
                 'color': [1, 1, 1], 'intensity': 1}
        self.check_case(recipe(outputs=[light]), True, True)
        for missing in ('bones', 'color', 'intensity'):
            with self.subTest(missing=missing):
                self.check_case(recipe(outputs=[{key: value for key, value in light.items() if key != missing}]), False, False)

    def test_variant_structure_and_override_semantics(self):
        signals = {'value': {'constant': 1}, 'vector': {'constant': [1, 2, 3]}}
        base = {'name': 'Armor variant', 'key': {'armor': 'ArmorIronCuirass'}}
        cases = [(base, True, True),
                 (dict(base, name=''), False, False),
                 ({'key': base['key']}, False, False),
                 ({'name': 'missing key'}, False, False),
                 (dict(base, key={'selector': []}), True, True),
                 (dict(base, overrides={'value': 2}), True, True),
                 (dict(base, overrides={'vector': [3, 2, 1]}), True, True),
                 (dict(base, overrides={'missing': 1}), True, False),
                 (dict(base, overrides={'value': [1, 2, 3]}), True, False),
                 (dict(base, overrides={'vector': 1}), True, False),
                 (dict(base, overrides=[]), False, False),
                 (dict(base, overrides={'bad-name': 1}), False, False)]
        for variant, schema_valid, parser_valid in cases:
            with self.subTest(variant=variant):
                self.check_case(recipe(signals=signals, variants=[variant]), schema_valid, parser_valid)

    def test_reference_types_and_cycles(self):
        cases = [({'value': {'constant': 1}, 'copy': {'smooth': {'of': '@value'}}}, True, True),
                 ({'copy': {'smooth': {'of': '@missing'}}}, True, False),
                 ({'copy': {'smooth': {'of': '@bad-name'}}}, False, False),
                 ({'first': {'expr': '@second'}, 'second': {'expr': '@first'}}, True, False)]
        for signals, schema_valid, parser_valid in cases:
            with self.subTest(signals=signals):
                self.check_case(recipe(signals=signals), schema_valid, parser_valid)

    def test_collection_boundaries(self):
        for count in (4096, 4097):
            valid = count <= 4096
            for document in (recipe(keys=['default'] * count),
                             recipe(signals={f'row{i}': {'constant': 1} for i in range(count)}),
                             recipe(variants=[{'name': f'Variant {i}', 'key': {'selector': []}} for i in range(count)])):
                with self.subTest(count=count, section=list(document)[-1]):
                    self.check_case(document, valid, valid)

    def test_document_depth_is_a_parser_safety_limit(self):
        for depth in (32, 33):
            nested = 0
            for _ in range(depth - 1):
                nested = {'child': nested}
            with self.subTest(depth=depth):
                self.check_case(recipe(meta=nested), True, depth == 32)

    def test_bone_names_are_nonempty(self):
        for bones, valid in (([], False), ([''], False), (['NPC Spine [Spn0]'], True)):
            with self.subTest(bones=bones):
                self.check_case(recipe(sources={'weights': {'bake': {'boneWeight': bones}}}), valid, valid)
                self.check_case(recipe(outputs=[{'target': 'light', 'bones': {'named': bones},
                                                 'color': [1, 1, 1], 'intensity': 1}]), valid, valid)

    def test_parser_semantics_beyond_structural_schema(self):
        for expression in ('1 +', '@missing'):
            with self.subTest(expression=expression):
                self.check_case(recipe(signals={'value': {'expr': expression}}), True, False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--validator', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    VALIDATOR = args.validator.resolve()
    unittest.main(argv=[__file__, *remaining])

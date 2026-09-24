import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CALL = re.compile(r'\b(?:ImGui|ImGuiMCP|SKSEMenuFramework|FontAwesome)::(?:\w+::)*(\w+)\s*\(')
EXPORT = re.compile(r'Get(?:MenuFrameworkFunction|Function)<[^;]+?>\(\s*"([^"]+)"')


def required_exports():
    header = (ROOT / 'src/extern/SKSEMenuFramework.h').read_text()
    source = '\n'.join(path.read_text() for path in (ROOT / 'src').rglob('*')
                       if path.suffix in ('.h', '.cpp')
                       and 'extern' not in path.parts and 'cs' not in path.parts)
    pending = set(CALL.findall(source))
    visited = set()
    exports = set()
    while pending:
        name = pending.pop()
        if name in visited:
            continue
        visited.add(name)
        pattern = re.compile(r'\b' + re.escape(name) + r'\s*\([^;{}]*\)\s*\{')
        bodies = []
        for match in pattern.finditer(header):
            start = match.end()
            depth = 1
            end = start
            while depth and end < len(header):
                depth += (header[end] == '{') - (header[end] == '}')
                end += 1
            bodies.append(header[start:end - 1])
        if not bodies and name not in {'ImVec2', 'ImVec4'}:
            raise AssertionError(f'Cannot resolve framework wrapper: {name}')
        for body in bodies:
            exports.update(EXPORT.findall(body))
            pending.update(CALL.findall(body))
    return exports


class MenuExportsTests(unittest.TestCase):
    def test_registration_checks_every_used_wrapper_export(self):
        source = (ROOT / 'src/engine/MenuDependency.cpp').read_text()
        inventory = source.split('kMenuExports[]{', 1)[1].split('};', 1)[0]
        declared = re.findall(r'"([^"]+)"', inventory)
        self.assertEqual(len(declared), len(set(declared)))
        self.assertEqual(set(declared), required_exports())


if __name__ == '__main__':
    unittest.main()

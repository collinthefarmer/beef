"""Validate a target profile and derive CMake, loader, and package declarations."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def packed(version):
    if (not isinstance(version, list) or len(version) != 4 or
            any(type(v) is not int or not 0 <= v <= limit
                for v, limit in zip(version, (255, 255, 4095, 15)))):
        raise ValueError('version must fit the SKSE packed version format')
    a, b, c, d = version
    return (a << 24) | (b << 16) | (c << 4) | d


def validate(profile, root=None):
    fields = {'schema', 'id', 'compiled', 'runtimes', 'minimum_skse',
              'struct_compatibility', 'dependencies', 'address_library',
              'peers', 'runtime_verified'}
    if set(profile) != fields or profile['schema'] != 1:
        raise ValueError('unknown or missing compatibility profile fields')
    if not re.fullmatch(r'[a-z0-9][a-z0-9.-]{0,63}', profile['id']):
        raise ValueError('unsafe profile id')
    compiled = profile['compiled']
    if (set(compiled) != {'se', 'ae', 'vr'} or
            any(type(v) is not bool for v in compiled.values()) or
            compiled['vr'] or not (compiled['se'] or compiled['ae'])):
        raise ValueError('profiles require SE/AE compilation; VR is not reviewed')
    runtimes = profile['runtimes']
    if not isinstance(runtimes, list) or not 1 <= len(runtimes) <= 16:
        raise ValueError('an explicit list of 1 to 16 runtimes is required')
    seen = set()
    for runtime in runtimes:
        if set(runtime) != {'version', 'storefront'}:
            raise ValueError('runtime requires version and storefront')
        version = runtime['version']
        number = packed(version)
        if number in seen:
            raise ValueError('duplicate runtime')
        seen.add(number)
        if version[:2] not in ([1, 5], [1, 6]):
            raise ValueError('runtime family needs a new source compatibility review')
        family = 'se' if version[1] == 5 else 'ae'
        if not compiled[family]:
            raise ValueError('runtime family is not compiled')
        if runtime['storefront'] not in ('steam', 'gog') or version[3] != (
                1 if runtime['storefront'] == 'gog' else 0):
            raise ValueError('runtime subtype does not match storefront')
    if packed(profile['minimum_skse']) == 0:
        raise ValueError('a candidate SKSE minimum must be explicit')
    if profile['struct_compatibility'] != 'Independent':
        raise ValueError('only the existing CommonLib structure mode is reviewed')
    deps = profile['dependencies']
    if (set(deps) != {'commonlib', 'spdlog', 'rapidcsv'} or
            not re.fullmatch(r'[0-9a-f]{40}', deps['commonlib']) or
            any(not re.fullmatch(r'v[0-9]+(?:\.[0-9]+){1,2}', deps[k])
                for k in ('spdlog', 'rapidcsv'))):
        raise ValueError('invalid dependency pins')
    address = profile['address_library']
    if (set(address) != {'required', 'edition', 'package_version'} or
            address['required'] is not True or address['edition'] not in ('SE', 'AE') or
            any(('SE' if r['version'][1] == 5 else 'AE') != address['edition']
                for r in runtimes) or
            (address['package_version'] is not None and
             not isinstance(address['package_version'], str))):
        raise ValueError('Address Library edition must match the target runtimes')
    peers = profile['peers']
    if set(peers) != {'community_shaders', 'menu_framework'}:
        raise ValueError('missing peer baselines')
    for name, path, role in (
            ('community_shaders', 'src/cs/BSLightingShaderMaterialPBR.h', 'true_pbr_required'),
            ('menu_framework', 'src/extern/SKSEMenuFramework.h', 'editor_only')):
        peer = peers[name]
        if (set(peer) != {'candidate_version', 'source_revision', 'header', 'sha256', role} or
                not re.fullmatch(r'[0-9]+(?:\.[0-9]+){2}', peer['candidate_version']) or
                not re.fullmatch(r'[0-9a-f]{40}', peer['source_revision']) or
                not re.fullmatch(r'[0-9a-f]{64}', peer['sha256']) or
                peer['header'] != path or peer[role] is not True):
            raise ValueError('invalid peer baseline')
        if root and hashlib.sha256((root / path).read_bytes()).hexdigest() != peer['sha256']:
            raise ValueError(f'{name} profile differs from the vendored header')
    if profile['runtime_verified'] is not False:
        raise ValueError('runtime acceptance is not established by a build profile')
    return profile


def version_cpp(version):
    return 'REL::Version{ ' + ', '.join(map(str, version)) + ' }'


def variables(profile):
    return {
        'BEEF_COMMONLIB_REVISION': profile['dependencies']['commonlib'],
        'BEEF_SPDLOG_REVISION': profile['dependencies']['spdlog'],
        'BEEF_RAPIDCSV_REVISION': profile['dependencies']['rapidcsv'],
        **{f'BEEF_COMPILE_{k.upper()}': 'ON' if v else 'OFF'
           for k, v in profile['compiled'].items()},
        'BEEF_STRUCT_COMPATIBILITY': profile['struct_compatibility'],
        'BEEF_RUNTIME_DECLARATION': '{ ' + ', '.join(
            version_cpp(r['version']) for r in profile['runtimes']) + ' }',
        'BEEF_MINIMUM_SKSE': version_cpp(profile['minimum_skse'])}


def declaration(profile, name, version):
    parts = version.split('.')
    if len(parts) != 3 or any(not p.isdigit() for p in parts):
        raise ValueError('plugin version must have three numeric components')
    values = dict(variables(profile), PROJECT_NAME=name,
                  PROJECT_VERSION_MAJOR=parts[0], PROJECT_VERSION_MINOR=parts[1],
                  PROJECT_VERSION_PATCH=parts[2])
    template = (Path(__file__).resolve().parents[1] / 'cmake/Plugin.cpp.in').read_text()
    for key, value in values.items():
        template = template.replace('@' + key + '@', value)
    if re.search(r'@[A-Z_]+@', template):
        raise ValueError('unresolved declaration field')
    return template


def write_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def generate(path, output, root):
    profile = validate(json.loads(path.read_text()), root)
    if path.stem != profile['id']:
        raise ValueError('profile id must match its filename')
    write_changed(output / 'compatibility.json', json.dumps(profile, sort_keys=True, indent=2) + '\n')
    write_changed(output / 'compatibility.cmake', ''.join(
        f'set({key} "{value}")\n' for key, value in variables(profile).items()))
    checks = ' || '.join(f'a_runtime == {packed(r["version"])}u' for r in profile['runtimes'])
    header = ('#pragma once\n#include <cstdint>\n'
              'namespace BetterEnchantmentEffects::BuildCompatibility {\n'
              f'inline constexpr const char *profile = "{profile["id"]}";\n'
              f'inline constexpr std::uint32_t minimumSKSE = {packed(profile["minimum_skse"])}u;\n'
              'inline constexpr bool SupportsRuntime(std::uint32_t a_runtime) noexcept {\n'
              f'  return {checks};\n' + '}\n}\n')
    write_changed(output / 'BuildCompatibility.h', header)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--profile', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--root', type=Path, required=True)
    args = parser.parse_args()
    generate(args.profile, args.output, args.root)

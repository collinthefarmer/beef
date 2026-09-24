import argparse
import hashlib
import json
import pathlib
import re
import subprocess


def write_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def source_identity(root):
    paths = []
    for folder in ("src", "cmake", "shaders"):
        paths.extend(p for p in (root / folder).rglob("*")
                     if p.is_file())
    for name in ("CMakeLists.txt", "CMakePresets.json", "COPYING.md", "flake.nix", "flake.lock",
                 "tools/build-identity.py", "tools/presenter-textures.py", "tools/compatibility.py"):
        if (root / name).is_file():
            paths.append(root / name)
    digest = hashlib.sha256()
    for path in sorted(paths):
        name = path.relative_to(root).as_posix().encode()
        content = path.read_bytes()
        digest.update(len(name).to_bytes(8, "little"))
        digest.update(name)
        digest.update(len(content).to_bytes(8, "little"))
        digest.update(content)
    return digest.hexdigest(), len(paths)


def profile_hash(profile):
    return hashlib.sha256(json.dumps(profile, sort_keys=True).encode()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path)
    parser.add_argument("--config")
    parser.add_argument("--compatibility", type=pathlib.Path)
    parser.add_argument("--write-provenance", action="store_true")
    parser.add_argument("--allow-modified-source", action="store_true")
    args = parser.parse_args()
    if not args.write_provenance and (args.output is None or args.config is None):
        parser.error("building identity requires --output and --config")
    root = args.root.resolve()
    fingerprint, input_files = source_identity(root)
    profile = json.loads(args.compatibility.read_text()) if args.compatibility else None
    provenance_path = root / "SOURCE_PROVENANCE.json"
    provenance = None
    if (root / ".git").exists():
        revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
                                  capture_output=True, text=True, check=True).stdout.strip()
    else:
        if args.write_provenance:
            parser.error("provenance must be written from a Git checkout")
        if not provenance_path.is_file():
            parser.error("source archive requires SOURCE_PROVENANCE.json from its producer")
        try:
            provenance = json.loads(provenance_path.read_text())
        except (ValueError, OSError) as error:
            parser.error(f"invalid SOURCE_PROVENANCE.json: {error}")
        if (not isinstance(provenance, dict)
                or set(provenance) != {"schema", "revision", "source_sha256", "compatibility_sha256"}
                or type(provenance["schema"]) is not int or provenance["schema"] != 1
                or any(not isinstance(provenance[key], str)
                       or not re.fullmatch(pattern, provenance[key]) for key, pattern in (
                           ("revision", r"[0-9a-f]{40}"),
                           ("source_sha256", r"[0-9a-f]{64}"),
                           ("compatibility_sha256", r"[0-9a-f]{64}")))):
            parser.error("invalid SOURCE_PROVENANCE.json schema or fingerprints")
        revision = provenance["revision"]
        if provenance["compatibility_sha256"] != profile_hash(profile):
            parser.error("archive compatibility profile differs from provenance")
        if provenance["source_sha256"] != fingerprint and not args.allow_modified_source:
            parser.error("archive source differs from provenance; use --allow-modified-source "
                         "(CMake BEEF_ALLOW_MODIFIED_SOURCE=ON) for intentional edits")
    if args.write_provenance:
        write_changed(provenance_path, json.dumps({"schema": 1, "revision": revision,
                      "source_sha256": fingerprint, "compatibility_sha256": profile_hash(profile)},
                      indent=2) + "\n")
        return
    configuration_hash = hashlib.sha256(json.dumps(
        {'configuration': args.config, 'compatibility': profile}, sort_keys=True).encode()).hexdigest()
    build_id = f"{revision[:12]}-{fingerprint[:16]}-{args.config}-{configuration_hash[:8]}"
    manifest = {"schema": 1, "build": build_id, "revision": revision,
                "source_sha256": fingerprint, "configuration": args.config,
                "input_files": input_files, "configuration_sha256": configuration_hash}
    if provenance and provenance["source_sha256"] != fingerprint:
        manifest["archive_source_sha256"] = provenance["source_sha256"]
    if profile is not None:
        manifest["compatibility"] = profile
        manifest["compatibility_profile"] = profile["id"]
    header = "#pragma once\nnamespace BetterEnchantmentEffects::BuildIdentity {\n"
    for key, value in manifest.items():
        if isinstance(value, str):
            header += f"inline constexpr const char *{key} = {json.dumps(value)};\n"
    header += "}\n"
    write_changed(args.output / "BuildIdentity.h", header)
    write_changed(args.output / "build-identity.json", json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()

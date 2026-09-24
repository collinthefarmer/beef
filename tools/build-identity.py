import argparse
import hashlib
import json
import pathlib
import subprocess


def write_changed(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--config", required=True)
    parser.add_argument("--compatibility", type=pathlib.Path)
    args = parser.parse_args()
    root = args.root.resolve()
    paths = []
    for folder in ("src", "cmake", "shaders"):
        paths.extend(p for p in (root / folder).rglob("*")
                     if p.is_file())
    for name in ("CMakeLists.txt", "CMakePresets.json", "flake.nix", "flake.lock",
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
    revision = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
                              capture_output=True, text=True, check=True).stdout.strip()
    fingerprint = digest.hexdigest()
    profile = json.loads(args.compatibility.read_text()) if args.compatibility else None
    configuration_hash = hashlib.sha256(json.dumps(
        {'configuration': args.config, 'compatibility': profile}, sort_keys=True).encode()).hexdigest()
    build_id = f"{revision[:12]}-{fingerprint[:16]}-{args.config}-{configuration_hash[:8]}"
    manifest = {"schema": 1, "build": build_id, "revision": revision,
                "source_sha256": fingerprint, "configuration": args.config,
                "input_files": len(paths), "configuration_sha256": configuration_hash}
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

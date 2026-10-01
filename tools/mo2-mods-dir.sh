#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
dir="${MO2_MODS_DIR:-}"
if [ -z "$dir" ] && [ -f local.env ]; then
	dir=$(sed -n '/^MO2_MODS_DIR=/{s///;s/^"\(.*\)"$/\1/;p;q;}' local.env)
fi
if [ -z "$dir" ]; then
	echo "set MO2_MODS_DIR to the Mod Organizer 2 mods directory, or add the line MO2_MODS_DIR=<path> to local.env" >&2
	exit 1
fi
if [ ! -d "$dir" ]; then
	echo "MO2_MODS_DIR is not an existing directory: $dir" >&2
	exit 1
fi
printf '%s\n' "$dir"

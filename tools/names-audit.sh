#!/usr/bin/env bash
#
# names-audit.sh — discovery kit for a noun/verb naming-consistency audit.
#
# Emits LEADS with evidence (signatures / types / file:line), never verdicts.
# A shared word is a lead, not a finding: two names are inconsistent only when
# they match on return shape, fallibility, direction, and mutation. Confirm
# every hit by opening the definition; rename through clangd (tools/rename.py).
#
#   names-audit.sh sigs                 declared functions, full signature (both layouts)
#   names-audit.sh types                the type vocabulary (struct/class/enum/using)
#   names-audit.sh overloads            member fields whose name wears >=2 distinct types
#   names-audit.sh census <word>        every type a given name wears (verify a suspect)
#
# Scope: NAMES_AUDIT_SRC (default "src"). Third-party is always excluded via
# NAMES_AUDIT_EXCLUDES (default "extern _old").
#
# Reliability: sigs/types/overloads are anchored to declaration lines and are
# reliable leads. `overloads` covers MEMBER fields only — the fully general
# "every name incl. params and locals" sweep is NOT regex-reliable (statement
# bigrams like `return x` pollute it). Read params off `sigs`; use clangd for
# locals. `sigs` catches on-one-line-or-split signatures but not macro-generated
# ones. Everything here is a lead; the verdict is in-situ reading.

set -uo pipefail

SRC="${NAMES_AUDIT_SRC:-src}"
read -r -a EXCLUDE_DIRS <<<"${NAMES_AUDIT_EXCLUDES:-extern _old}"

rg_excludes=()
for d in "${EXCLUDE_DIRS[@]}"; do rg_excludes+=(-g "!**/${d}/**"); done
grep_excludes="/($(
  IFS='|'
  echo "${EXCLUDE_DIRS[*]}"
))/"

usage() {
  sed -n '3,26p' "$0" | sed 's/^# \{0,1\}//'
}

need_rg() {
  command -v rg >/dev/null 2>&1 || {
    echo "names-audit: rg (ripgrep) not on PATH; run inside 'nix develop'" >&2
    exit 127
  }
}

cmd="${1:-}"
[ "$#" -gt 0 ] && shift

case "$cmd" in
sigs)
  need_rg
  rg -U --pcre2 --no-heading -n -g '*.h' -g '*.cpp' "${rg_excludes[@]}" \
    '(?:\[\[nodiscard\]\]\s*)?[A-Za-z_][A-Za-z0-9_:<>,*&]*[ \t]*\n?[ \t]*\b[A-Z][A-Za-z0-9_]*\([^;{]*\)\s*(?:const|noexcept|\s)*[;{]' \
    "$SRC" || true
  ;;
types)
  grep -rnE '^[[:space:]]*(struct|class|enum class|using) [A-Z][A-Za-z0-9_]*' \
    "$SRC" --include='*.h' | grep -vE "$grep_excludes" || true
  ;;
overloads)
  need_rg
  rg --pcre2 -oN --no-filename -r '$2|$1' -g '*.h' "${rg_excludes[@]}" \
    '^[ \t]{2,}((?:const\s+)?(?:std::|RE::|Studio::)?[A-Za-z_][A-Za-z0-9_:]*(?:<[^>]*>)?\s*[*&]{0,2})\s+([a-z][A-Za-z0-9_]*)\s*(?:=[^;]*)?;' \
    "$SRC" |
    awk -F'|' '{if(!(($1 SUBSEP $2) in seen)){seen[$1 SUBSEP $2]=1; types[$1]=types[$1] "  " $2; count[$1]++}}
               END{for(n in count) if(count[n]>=2) print count[n] "\t" n " ->" types[n]}' |
    sort -rn || true
  ;;
census)
  need_rg
  word="${1:-}"
  [ -n "$word" ] || {
    echo "names-audit: census needs a WORD" >&2
    exit 2
  }
  rg --pcre2 -oN --no-filename -r '$1' -g '*.cpp' -g '*.h' "${rg_excludes[@]}" \
    '(?:^|[(,])\s*((?:const\s+)?(?:std::|RE::|Studio::)?[A-Za-z_][A-Za-z0-9_:]*(?:<[^>]*>)?\s*[*&]{0,2})\s+(?:a_)?'"${word}"'\b' \
    "$SRC" | sed -E 's/^[[:space:]]+//; s/[[:space:]]+$//' | sort | uniq -c | sort -rn || true
  ;;
*)
  usage
  [ -z "$cmd" ] && exit 0 || exit 1
  ;;
esac

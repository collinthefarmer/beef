# Session handoff (2026-09-22, end of day)

> Archived 2026-09-22. Superseded as a work plan by [Alpha preparation](../plans/alpha-preparation-2026-09-22.md). The tasks and status claims below are historical; unchecked items are not active commitments. Consult current code and component documentation for implemented behavior.

Two sessions of work sit uncommitted in the working tree. The morning
landed the composable-proximity bundle; the afternoon extended the
vector ops, polished the bakes, and ran a four-agent source sweep whose
findings were all addressed. The native suite is green (70 binaries)
and every change below is built into the installed DLL.

## The format changes (all pre-freeze, schema updated, recipes migrated)

| Change | Items |
|---|---|
| Expression language: `length`, `distance`, `dot`, `cross`, `normalize` (ops 38-42, pins extended, scalar operands rejected — the GPU splats scalars) | 54, 59 |
| `actorState`: `target` + `hasTarget` added, `hostileDistance` deleted | 55, 56, 57 |
| Bakes: `normal` added, `worldUp` deleted (derivable via `dot`), `uv` folded in as one vec2 bake (UvSource deleted), `localPosition` works on skinned armor via a measured bound | 60, 61, 64 |
| `distance` source: point-form deleted (exactly derivable — `2·kPositionFrame == kDistanceFrame`), node-form kept | 65 |
| `material`: grid completed with `normalRgb` and `rmaosRgb`; every channel of every map now reachable via `dot()` | 66 |
| Bake rendering: alpha marks coverage, two-texel dilation gutter before mips | 62 |

## Verified in play

The complement check (`expr-mask-length-distance`) confirmed GPU ops
38/39 render correctly once its saturate ring moved off the body; the
facing recipe (`inspect-target-direction`) verified `dot`/`normalize`
and the normal bake, and en route established two facts now in
REFERENCE.md: the player's `target` follows the attack/crosshair focus
(NPCs use combat AI), and mirrored L/R armor UVs destroy left/right in
every bake (item 48), so facing effects are front/back only.

## Code consolidation (item 67)

`BakeKey{definition, pixels}` replaced the encode-then-reparse string
keys; `MaterialClustersSource` holds `ClusterSettings` directly
(bridges deleted); the classify dialect renamed to clusters end to
end; `BindTarget`/`UnbindTarget` factored under both draw paths;
`DescribeTexture` deduplicated; `kPassSrvs` named; twin cluster caps
merged; ripple `direction` validated. Declined with reasons recorded:
the `SourcePreparer`/`SourceInspector` merge (two policies, not one
spelled twice). Audit correction: `RecipeInputsAreActorIndependent`
was right all along — bake textures are per-mesh, so bakes correctly
block cross-actor sharing.

## Open

- **Residual decision (item 66):** the material naming still mixes
  map-prefixed raw reads, meaning-named scalars, and computed words; a
  rename-only restructure is available pre-freeze if wanted.
- **Item 63:** id maps (componentId, chartId, the cluster map) want
  nearest sampling; bilinear fabricates ids at island borders.
- **Item 58 (optional add):** a `nearestHostile` selector for
  player-worn "any hostile nearby" effects.
- **In-game verification owed:** the migrated `source-uv-tile` and
  `source-distance-radial` recipes under the new format; whether the
  dilation gutter visibly reduced the seam artifacts; the old item-47
  carryovers (throttle revert, directional ripple).
- **Freeze writing:** unchanged list (22, 23, 27, 42, 43, 52, halves
  of 44/53) plus the new facts above; decisions 38, 45, 50, 53a still
  the user's.

## Pointers

- Backlog: `docs/history/format-row-reference-2026-09-21.md` (items 54
  to 67 all carry DONE/DECLINED records dated 2026-09-22).
- The four audit reports live in this conversation only; their
  surviving content is in the backlog rows and REFERENCE.md.
- Nothing is committed. The debug folder gained
  `expr-mask-length-distance`, `inspect-target-direction`,
  `inspect-target-tint`, `inspect-bake-position`; `actorstate-test/`
  and `pre-freeze-stress/` were removed from MO2 by the user.

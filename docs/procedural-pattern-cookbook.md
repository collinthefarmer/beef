# Procedural pattern cookbook

Copyable recipe expressions for armor-aware markings and animation. These use
the current parser vocabulary; examples are schema/parser validated, but their
appearance on an armor has not been validated in game.

For reusable material selectors, event envelopes, resource responses and other
inputs to these patterns, see the [recipe fragments cookbook](recipe-fragments-cookbook.md).
Its final example combines placement, a procedural pattern and hit-driven heat.

## Using the cookbook

The starter below is a complete recipe keyed to the existing demo cuirass,
`0x803~BetterEnchantmentEffectsDemo.esp`. That ESP must be present; replace the
key for another item. Each numbered entry is a **fragment**, not a standalone
recipe: merge its `signals`, `sources`, `curves`, and `masks` by row name into a
fresh copy of the starter. Replace rows with matching names. Keep its outputs.
Always begin from a fresh starter when switching entries: otherwise an earlier
entry's `cbActivity` or extra sources can remain behind.

For an existing recipe, copy the dependencies and wire the resulting mask into
an appropriate layer instead of adding the starter's entire output. Its emissive
`strength` is a material scalar, so another recipe writing it can change the
combined result. Use the editor's solo mode for a standalone study. This document
does not install recipes or modify the working examples.

The `cb` prefix reduces naming collisions. Rename references together with rows
if your recipe already uses these names. All fragments below are JSON without
comments or placeholder tokens.

## Starter: placement, marking, activity

`cbEligible` selects where the effect belongs. It starts with bright, smooth
material regions, following the edited Arcane Circuit approach. Inspect this
selection on the actual armor and adjust its thresholds before adding detail.
It is a material heuristic, not a detector for plates or engravings.

`cbPattern` defines the marking. `cbActivity` defines its active illumination.
`cbCoverage` and `cbActive` apply the armor selection once to each. The starter
shows the former faintly and the latter more strongly; static entries use the
same field for both. Colors and output strengths are starting values only.

```json
{
  "format": 1,
  "name": "Cookbook study",
  "keys": [
    {
      "armor": "0x803~BetterEnchantmentEffectsDemo.esp"
    }
  ],
  "merge": "stack",
  "signals": {
    "cbAngle": {
      "constant": 0
    },
    "cbDensity": {
      "constant": 12
    },
    "cbWidth": {
      "constant": 0.08
    },
    "cbSoftness": {
      "constant": 0.025
    },
    "cbPhase": {
      "expr": "time * 0.15"
    }
  },
  "sources": {
    "cbUV": {
      "bake": "uv"
    },
    "cbLuma": {
      "material": "diffuseLuma"
    },
    "cbRoughness": {
      "material": "roughness"
    }
  },
  "masks": {
    "cbU": "dot(@cbUV, [1, 0])",
    "cbV": "dot(@cbUV, [0, 1])",
    "cbEligible": "smoothstep(0.25, 0.55, @cbLuma) * (1 - smoothstep(0.35, 0.65, @cbRoughness))",
    "cbPattern": "1 - smoothstep(@cbWidth, @cbWidth + @cbSoftness, abs(frac(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * @cbDensity) - 0.5))",
    "cbActivity": "@cbPattern",
    "cbCoverage": "saturate(@cbEligible * @cbPattern)",
    "cbActive": "saturate(@cbEligible * @cbActivity)"
  },
  "outputs": [
    {
      "target": "material",
      "slot": "emissive",
      "strength": 1,
      "resolution": "half",
      "stack": [
        {
          "source": [
            1,
            1,
            1
          ],
          "blend": "replace",
          "opacity": 0.12,
          "color": [
            0.12,
            0.65,
            1
          ],
          "mask": "@cbCoverage"
        },
        {
          "source": [
            1,
            1,
            1
          ],
          "blend": "add",
          "opacity": 0.65,
          "color": [
            0.12,
            0.65,
            1
          ],
          "mask": "@cbActive"
        }
      ]
    }
  ]
}
```

### Coordinate and expression rules

- `cbU`/`cbV` are the UV bake's two components. The expressions rotate those
  coordinates inline about UV (0.5, 0.5), using `cbAngle` in radians. They do not unwrap
  or align separate islands. Overlapping/mirrored UVs repeat or mirror patterns.
- Densities are repeats per UV unit, not marks per plate or world unit. Use
  output selectors/variants or an authored map where islands need different
  orientations, origins or scale. A single global rotation cannot fix an atlas.
- Access components with `dot(@cbUV, [1, 0])`, not `.x`/`.y`. Reference rows
  with `@name`. Put spatial expressions in `masks`; `signals` cannot read UVs
  or material texels. Keep time-based phase in a signal and reference it from
  masks. `pi` is supported.
- `cbWidth` and `cbSoftness` must stay positive, with their sum below 0.5 for
  the repeating cell-line expressions. Smoothstep needs ordered, distinct edges.
  Avoid exact zero feather widths and increase feathering when reducing resolution.
- Curves transform `x`; they cannot reference rows such as `@charge`. Use a
  mask expression for a spatial transform involving signals. No `atan2`,
  per-texel `noise()` or derivative-based antialiasing function is provided.
- Named masks are RGBA8 UNORM textures: values are quantized and clipped to
  0–1 between rows. Keep signed coordinates, cell indices and intermediate
  values above 1 inside a single expression. This cookbook inlines that math;
  it shares only bounded fields. The same limit applies to fine UV-coordinate
  precision, so narrow high-frequency lines need visual inspection.
- A mask referenced by another mask may require a rendered intermediate. Build
  only the pattern you need, reuse common rows, and start at half resolution.
  Thin detail can alias or disappear; high density is not free detail.

## Patterns

| Markings | Motion and transformation |
|---|---|
| [Parallel bands](#1-parallel-bands) | [Traveling packets](#6-traveling-packets-on-fixed-markings) |
| [Interrupted bands](#2-interrupted-bands) | [Interference windows](#7-interference-windows) |
| [Chevrons](#3-repeating-chevrons) | [Growth front](#8-relief-biased-growth-front) |
| [Diamond lattice](#4-diamond-lattice) | [Patch flicker](#10-stable-patches-with-independent-flicker) |
| [Dots and studs](#5-staggered-dots-and-studs) | [UV rings](#11-concentric-uv-rings) |
| [Crack-like lines](#9-warped-crack-like-lines) | [Hit ripple](#12-broken-chest-anchored-hit-ripple) |
| | [Relief glints](#13-procedural-glints-within-original-detail) |

### 1. Parallel bands

A repeatable starting point for conductors, narrow inlays, and panel markings. Bands follow the rotated V direction and repeat across the rotated U direction.

```json
{
  "masks": {
    "cbPattern": "1 - smoothstep(@cbWidth, @cbWidth + @cbSoftness, abs(frac(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * @cbDensity) - 0.5))"
  }
}
```

Density 4–20, width 0.03–0.15, softness 0.01–0.05. Width is a half-width in repeat-cell units: 0.08 covers roughly 16% of each cell before feathering. Rotate cbAngle to align with a panel; 1.5708 radians is a quarter turn.

### 2. Interrupted bands

Cut periodic gaps along the bands. The stripe direction and the interruption direction remain independently tunable.

```json
{
  "masks": {
    "cbBand": "1 - smoothstep(@cbWidth, @cbWidth + @cbSoftness, abs(frac(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * @cbDensity) - 0.5))",
    "cbSegment": "1 - smoothstep(0.28, 0.36, abs(frac((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * 8) - 0.5))",
    "cbPattern": "@cbBand * @cbSegment"
  }
}
```

The literal 8 is segments per UV unit along the rotated along coordinate. Lower the 0.28/0.36 thresholds for shorter segments. Combine with a packet to suggest energy traveling through a segmented conductor.

### 3. Repeating chevrons

Fold one coordinate into a triangular wave, then use it to bend parallel bands into repeating V shapes.

```json
{
  "masks": {
    "cbFold": "abs(frac(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * 4) - 0.5) * 2",
    "cbChevronDistance": "abs(frac((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * @cbDensity + 0.7 * @cbFold) - 0.5)",
    "cbPattern": "1 - smoothstep(@cbWidth, @cbWidth + @cbSoftness, @cbChevronDistance)"
  }
}
```

4 controls horizontal repetition; 0.7 controls the bend in units of vertical band spacing. Begin with density 6–12 and width 0.04–0.08. The fold is a repeated zigzag, not an automatic breastplate centerline.

### 4. Diamond lattice

Combine two diagonal line families. max forms their union without doubling brightness at intersections.

```json
{
  "masks": {
    "cbDiagonalA": "1 - smoothstep(@cbWidth, @cbWidth + @cbSoftness, abs(frac((((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) + (-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle))) * @cbDensity) - 0.5))",
    "cbDiagonalB": "1 - smoothstep(@cbWidth, @cbWidth + @cbSoftness, abs(frac((((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) - (-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle))) * @cbDensity) - 0.5))",
    "cbPattern": "max(@cbDiagonalA, @cbDiagonalB)"
  }
}
```

Use width 0.02–0.06 for engraving. Multiplying the two families instead of max leaves small intersection marks. Changing the two diagonal coordinate combinations to unequally weighted diagonals changes the diamond proportions. UV distortion changes the result on the mesh.

### 5. Staggered dots and studs

Offset alternating rows by half a cell, then place a soft circular dot at each cell center.

```json
{
  "masks": {
    "cbDotU": "frac(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * @cbDensity + frac(floor((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * @cbDensity) * 0.5))",
    "cbDotV": "frac((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * @cbDensity)",
    "cbDotDistance": "sqrt((@cbDotU - 0.5) * (@cbDotU - 0.5) + (@cbDotV - 0.5) * (@cbDotV - 0.5))",
    "cbPattern": "1 - smoothstep(0.12, 0.18, @cbDotDistance)"
  }
}
```

0.12/0.18 are inner/outer radii in cell units; keep the outer radius below 0.5. These are circular in UV space, not necessarily on the armor. Replace the dot expression with smoothstep(0.08, 0.11, @cbDotDistance) * (1 - smoothstep(0.15, 0.18, @cbDotDistance)) for small ring inlays.

### 6. Traveling packets on fixed markings

Keep interrupted bands stationary while a periodic narrow pulse travels along them. cbCoverage stays faintly visible; cbActive carries the moving highlight.

```json
{
  "masks": {
    "cbBand": "1 - smoothstep(@cbWidth, @cbWidth + @cbSoftness, abs(frac(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * @cbDensity) - 0.5))",
    "cbSegment": "1 - smoothstep(0.28, 0.36, abs(frac((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * 8) - 0.5))",
    "cbPattern": "@cbBand * @cbSegment",
    "cbPacket": "pow(0.5 + 0.5 * cos(((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * 3 - @cbPhase) * 2 * pi), 8)",
    "cbActivity": "@cbPattern * @cbPacket"
  }
}
```

The phase signal advances at 0.15 cycles/second: the complete pattern repeats every 6.67 seconds. With 3 spatial cycles per UV unit, packet centers move at 0.05 UV units/second. Exponent 4–16 controls pulse concentration. Negate cbPhase to reverse direction; set its signal to constant 0 for placement inspection.

### 7. Interference windows

Two broad waves intersect to produce an evolving field of bright windows. This can animate a selected panel without imposing fine linework.

```json
{
  "masks": {
    "cbWaveA": "0.5 + 0.5 * sin((((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * 7 + (-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * 3 - @cbPhase) * 2 * pi)",
    "cbWaveB": "0.5 + 0.5 * cos((((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * 2 - (-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * 9 + @cbPhase * 0.6) * 2 * pi)",
    "cbPattern": "smoothstep(0.35, 0.75, @cbWaveA * @cbWaveB)"
  }
}
```

Start with low emissive opacity and frequencies below 10. For stationary markings with interference only in their illumination, keep another entry’s cbPattern and set cbActivity to @cbPattern * @cbWaveA * @cbWaveB instead. This is interference-like modulation, not a physical wave simulation.

### 8. Relief-biased growth front

Advance a reveal boundary along the rotated along coordinate. Original material relief and a slow spatial wave delay different texels, producing an irregular front.

```json
{
  "signals": {
    "cbGrowth": {
      "ramp": {
        "from": 0,
        "to": 1,
        "seconds": 5
      }
    }
  },
  "sources": {
    "cbRelief": {
      "material": "relief"
    }
  },
  "masks": {
    "cbDelay": "saturate(0.65 * @cbRelief + 0.35 * (0.5 + 0.5 * sin(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * 12 * pi)))",
    "cbArrival": "0.1 + 0.65 * saturate((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) + 0.5) + 0.15 * @cbDelay",
    "cbPattern": "smoothstep(@cbArrival, @cbArrival + 0.08, @cbGrowth)"
  }
}
```

The front is absent at growth 0 and reaches every eligible texel by growth 1. The recipe clock drives the ramp; it is not a fresh timer for every hit or editor visit. Relief uses displacement when available/nonflat, otherwise occlusion: inspect its polarity before treating it as recess depth. Use 1 - @cbRelief if the useful regions are reversed. At nonzero cbAngle, clamping the rotated coordinate compresses the reveal at its extremes.

### 9. Warped crack-like lines

Bend two thin line families with different sine fields and join them. The result suggests veins or irregular etched fractures.

```json
{
  "masks": {
    "cbVeinA": "1 - smoothstep(0.04, 0.11, abs((sin((((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * 10 + 0.18 * sin((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * 14 * pi)) * pi))))",
    "cbVeinB": "1 - smoothstep(0.03, 0.08, abs((sin(((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * 13 + 0.14 * sin(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * 10 * pi)) * pi))))",
    "cbPattern": "max(@cbVeinA, @cbVeinB)"
  }
}
```

The warp amounts 0.18/0.14 bend the lines; increasing them produces tangled shapes. This is intersecting warped linework, not branching growth or Voronoi cracks. Restrict to an appropriate material and keep the frequency low enough to survive the chosen resolution.

### 10. Stable patches with independent flicker

Use a cheap deterministic sine hash of integer UV cells to give each patch its own phase. The patches remain fixed while their brightness varies.

```json
{
  "masks": {
    "cbCellSeed": "frac(sin((floor(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * 8)) * 12.9898 + (floor((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * 8)) * 78.233) * 43758.5453)",
    "cbPatch": "step(0.4, @cbCellSeed)",
    "cbPattern": "@cbPatch",
    "cbActivity": "@cbPattern * pow(0.5 + 0.5 * sin((@cbPhase + @cbCellSeed) * 2 * pi), 6)"
  }
}
```

8 controls cell size; the 0.4 cutoff leaves about 60% of cells in a hash with a roughly uniform distribution. This is a visual pseudo-random field, not guaranteed randomness or noise. Large sine hashes can differ across CPU/GPU precision; do not use their cell values as persistent IDs. Cell edges are intentionally hard; use existing ornament as cbPattern and the cell phase only for activity if the grid is too apparent.

### 11. Concentric UV rings

Measure radius from a UV center and repeat narrow bands along that radius. Moving phase makes rings expand.

```json
{
  "masks": {
    "cbRadius": "sqrt((@cbU - 0.5) * (@cbU - 0.5) + (@cbV - 0.5) * (@cbV - 0.5))",
    "cbPattern": "1 - smoothstep(@cbWidth, @cbWidth + @cbSoftness, abs(frac(@cbRadius * @cbDensity - @cbPhase) - 0.5))"
  }
}
```

Change the two center coordinates from 0.5 to place the origin on a chosen UV island. This does not center a wave on the character’s chest: separate islands may show unrelated ring fragments. Use the next entry for a skeleton-anchored event wave. Multiplying the U offset by a scale factor can compensate for a known UV aspect ratio.

### 12. Broken, chest-anchored hit ripple

Use the built-in ripple source for a wave centered on the spine node in the armor’s bind-pose geometry. A UV gate breaks its visible band into segments.

```json
{
  "signals": {
    "cbHit": {
      "trigger": {
        "event": "hit.received",
        "anchor": {
          "node": "NPC Spine2 [Spn2]"
        },
        "lifetime": 1.2,
        "max": 3
      },
      "curve": "@cbRelease"
    },
    "cbHitLevel": {
      "expr": "saturate(@cbHit)"
    }
  },
  "curves": {
    "cbRelease": "pow(1 - x, 2)"
  },
  "sources": {
    "cbRing": {
      "ripple": {
        "trigger": "@cbHit",
        "speed": 80,
        "width": 10,
        "decay": 0.5
      }
    }
  },
  "masks": {
    "cbBreaks": "smoothstep(0.2, 0.4, 0.5 + 0.5 * sin(((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * 16 * pi))",
    "cbPattern": "saturate(@cbRing) * @cbBreaks * @cbHitLevel"
  }
}
```

Speed and width are spatial units, not UV cycles. The stripe gate is still UV-based, so interruptions may jump at seams even though the wave is spatially centered. Set cbBreaks to 1 to inspect the uninterrupted wave. The node anchors the wave; this is not the weapon’s precise impact point. The hit signal also fades the marking; do not invent a world-space vec3 hit payload for this scalar event.

### 13. Procedural glints within original detail

Keep original relief as the structure, then illuminate small moving intersections within it.

```json
{
  "sources": {
    "cbRelief": {
      "material": "relief"
    }
  },
  "masks": {
    "cbDetail": "smoothstep(0.35, 0.7, @cbRelief)",
    "cbGlintA": "pow(0.5 + 0.5 * sin((((@cbU - 0.5) * cos(@cbAngle) + (@cbV - 0.5) * sin(@cbAngle)) * 11 - @cbPhase) * 2 * pi), 12)",
    "cbGlintB": "pow(0.5 + 0.5 * cos(((-(@cbU - 0.5) * sin(@cbAngle) + (@cbV - 0.5) * cos(@cbAngle)) * 7 + @cbPhase * 0.7) * 2 * pi), 12)",
    "cbPattern": "@cbDetail",
    "cbActivity": "@cbPattern * @cbGlintA * @cbGlintB"
  }
}
```

Reduce exponents toward 4 for larger, softer highlights. The effect is animated emissive detail, not view-dependent specular reflection or the material glint slot. Replace cbDetail with an authored ornament mask when relief does not identify the desired decoration.

## Fit a pattern to the armor

Swap the starter's placement mask before changing the pattern itself. The
following fragments are alternatives; all retain the starter's coordinate rows.

### Metal-only placement

```json
{
  "sources": {
    "cbMetallic": {
      "material": "metallic"
    }
  },
  "masks": {
    "cbEligible": "smoothstep(0.25, 0.75, @cbMetallic)"
  }
}
```

### Anatomical and material placement

A bone-weight mask provides a soft anatomical region, not a rigid plate boundary.
These named bones must exist in the armor's skinning. Inspect each source first.

```json
{
  "sources": {
    "cbMetallic": {
      "material": "metallic"
    },
    "cbSpine": {
      "bake": {
        "boneWeight": [
          "NPC Spine2 [Spn2]"
        ]
      }
    },
    "cbClavicle": {
      "bake": {
        "boneWeight": [
          "NPC R Clavicle [RClv]"
        ]
      }
    }
  },
  "masks": {
    "cbEligible": "max(@cbSpine, @cbClavicle) * smoothstep(0.25, 0.75, @cbMetallic)"
  }
}
```

To also retain the starter's bright/smooth selection, multiply that expression
by its luma and roughness factors. Do not multiply the same soft eligibility
mask repeatedly in downstream rows: that progressively erodes its boundary.

An authored image mask can provide exact decoration boundaries when sampled
material channels cannot distinguish them. Its texture path and UV layout are
armor-specific, so no fictitious asset path is supplied here.

## Use the same field across material outputs

The following is an optional pair of outputs for frost/material studies.
Append the objects to the starter's `outputs` array (do not replace the array
unless you also intend to remove the glow). Both use the existing `cbCoverage`.
The diffuse layer changes RGB; the RMAOS layer changes roughness only.

```json
{
  "outputs": [
    {
      "target": "material",
      "slot": "diffuse",
      "resolution": "half",
      "stack": [
        {
          "source": [
            0.58,
            0.78,
            0.88
          ],
          "blend": "replace",
          "opacity": 0.35,
          "mask": "@cbCoverage",
          "channels": "rgb"
        }
      ]
    },
    {
      "target": "material",
      "slot": "rmaos",
      "resolution": "half",
      "stack": [
        {
          "source": [
            0.82,
            0.82,
            0.82
          ],
          "blend": "replace",
          "opacity": 0.4,
          "mask": "@cbCoverage",
          "channels": "r"
        }
      ]
    }
  ]
}
```

For reactive changes, use `cbActive` instead. Adjust diffuse, roughness and
emission separately: equal mask values do not imply equal perceptual strength.
Do not feed a scalar mask directly into the normal slot. Height additionally
sets a displacement scale affecting the base height map, so it deserves a
separate, armor-specific test rather than a universal default here.

## Suggested studies on the Dwarven cuirass

1. Arcane: use the bright/smooth placement, interrupted bands and traveling
   packets. First freeze phase; align the static markings, then restore motion.
2. Ward: use anatomical/material placement and the anchored hit ripple. The
   cookbook output is material emissive for inspection; transferring it to a
   shell also requires intentional shell material, alpha/blend and pose settings.
3. Winterglass: use metal-only placement and the relief-biased growth front;
   add the diffuse/roughness outputs. Try fine warped lines only after the broad
   transformation reads clearly.

Check front, back, seams, mirrored parts and both armor models. None of these
expressions automatically discovers engraving paths, follows a surface geodesic,
or guarantees that one UV density fits every piece. Save a useful material mask
and a useful motion field separately so they can be recombined.

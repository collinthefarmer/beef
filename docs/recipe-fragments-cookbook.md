# Reusable recipe fragments

Companion to the [procedural pattern cookbook](procedural-pattern-cookbook.md).
These modules expose a mask, scalar signal or color. They do not choose an item,
write an output, or install a recipe. Combine the result with an existing pattern
and output. JSON/type validation does not establish how a particular mesh,
texture, skeleton or animation graph will behave in game.

## Composition contract

- Merge the top-level `signals`, `sources`, `masks` and `curves` objects by row
  name. Do not replace an existing whole section. Names are distinct between
  these entries; when duplicating an entry, rename its definitions and references
  together. The `rf` prefix is only a naming convention.
- A **mask** can be a layer `mask`, a layer `source`, or an input to another mask.
  A **scalar signal** can drive layer opacity, light intensity, material scalars,
  or a mask expression. A **color signal** can drive a layer/light color.
  Signals cannot sample masks, material maps or mesh bakes.
- Stored masks are 0–1, quantized textures. Decode signed normals or integer IDs
  inside the final expression; do not put an unbounded intermediate in another
  mask row. Signal values are not constrained by this texture storage rule.
- Source values describe the captured original material or baked geometry, not
  automatic feedback from another recipe's newly written output.
- Raw triggers return newest-firing normalized age, with **1 at idle**. An
  envelope such as `pow(1 - x, 2)` turns that into a flash. Counters and
  accumulators consume firing counts, not the shaped trigger brightness.
- Stateful signals belong to a live recipe instance and use its clock. Do not
  treat them as persistent gameplay state, damage values, or guaranteed timers
  restarted on every editor visit. Event/node/texture prerequisites are listed
  beside the fragments that require them.

## Fragment index

| Fragment | Role | Result |
|---|---|---|
| [Diffuse-color neighborhood](#fragment-1) | Placement | `@rfColorMask` (mask) |
| [One material cluster](#fragment-2) | Placement | `@rfClusterMask` (mask) |
| [Upward-facing surfaces](#fragment-3) | Placement | `@rfUpMask` (mask) |
| [Soft ring around a bone](#fragment-4) | Placement | `@rfBoneRing` (mask) |
| [A sweep shared across geometry pieces](#fragment-5) | Motion | `@rfRootScan` (mask) |
| [One UV chart](#fragment-6) | Placement | `@rfChartMask` (mask) |
| [Soft combat gate](#fragment-7) | Response | `@rfCombatLevel` (scalar) |
| [Resource-spending response](#fragment-8) | Response | `@rfSpendLevel` (scalar) |
| [Low-resource crossing flash](#fragment-9) | Event | `@rfLowFlash` (scalar) |
| [Hit heat with a palette](#fragment-10) | Event / response | `@rfHeatLevel` (scalar) |
| [Three-hit meter with a combat reset](#fragment-11) | Event / response | `@rfChargeLevel` (scalar) |
| [Two-pulse hit envelope](#fragment-12) | Event | `@rfHitEcho` (scalar) |
| [Alternating footstep ripples](#fragment-13) | Event / spatial motion | `@rfStepMask` (mask) |
| [Facing the last attacker](#fragment-14) | Event / placement | `@rfAttackFacing` (mask) |
| [Combat-target proximity](#fragment-15) | Response | `@rfNearTarget` (scalar) |
| [Organic temporal flicker](#fragment-16) | Response | `@rfFlicker` (scalar) |
| [Capture resource level on combat entry](#fragment-17) | Event / response | `@rfEntryLevel` (scalar) |
| [Two-layer scrolling texture field](#fragment-18) | Spatial detail | `@rfFlowMask` (mask) |

<a id="fragment-1"></a>

## 1. Diffuse-color neighborhood

Select colors near a chosen diffuse RGB value, including equally bright colors that a luma threshold cannot separate.

**Result:** `@rfColorMask` (mask). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfTint": {
      "constant": [
        0.55,
        0.34,
        0.12
      ]
    }
  },
  "sources": {
    "rfDiffuse": {
      "material": "diffuseRgb"
    }
  },
  "masks": {
    "rfColorMask": "1 - smoothstep(0.12, 0.25, distance(@rfDiffuse, @rfTint))"
  }
}
```

Sample the actual material source to choose rfTint; these bronze-like numbers are illustrative, not a universal brass detector. Distances are RGB-space distances, not perceptually uniform color differences. 0.12/0.25 define full selection and complete rejection. Use as a layer mask or multiply into placement.

<a id="fragment-2"></a>

## 2. One material cluster

Select one group produced by the material analysis rather than hand-authoring several channel thresholds.

**Result:** `@rfClusterMask` (mask). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfClusterID": {
      "constant": 0
    }
  },
  "sources": {
    "rfClusters": {
      "materialClusters": {
        "clusters": 4,
        "weights": {
          "color": 1
        },
        "seed": 1,
        "iterations": 32
      }
    }
  },
  "masks": {
    "rfClusterMask": "1 - step(0.5, abs(@rfClusters * 255 - @rfClusterID))"
  }
}
```

IDs are integers from 0 through the actual cluster count minus one. Inspect the map first: ID 0 is not a named material, and IDs are local to each geometry. Changing weights, seed or input textures can reassign IDs. RGB color weighting requires the material-editing build; older builds lack weights.color. Analysis/rendering is more expensive than a simple material threshold, particularly while tuning. Keep the integer decode inside this expression.

<a id="fragment-3"></a>

## 3. Upward-facing surfaces

Bias snow, dust, frost or intermittent glints toward surfaces whose bind-pose normals point upward.

**Result:** `@rfUpMask` (mask). No other cookbook fragment is required.

```json
{
  "sources": {
    "rfNormal": {
      "bake": "normal"
    }
  },
  "masks": {
    "rfUpMask": "smoothstep(0.2, 0.7, dot(@rfNormal * 2 - [1, 1, 1], [0, 0, 1]))"
  }
}
```

The bake is encoded 0–1; decode it inside the expression. This is mesh-normal orientation in the bind-pose/root frame, not the normal texture, camera facing, or current world-up after animation. Replace the direction vector for another face selection. The output is soft coverage, not a displacement value.

<a id="fragment-4"></a>

## 4. Soft ring around a bone

Select a band at a chosen spatial distance from a skeleton node, independent of UV orientation.

**Result:** `@rfBoneRing` (mask). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfRadius": {
      "constant": 32
    },
    "rfHalfWidth": {
      "constant": 6
    }
  },
  "sources": {
    "rfSpineDistance": {
      "distance": "NPC Spine2 [Spn2]"
    }
  },
  "masks": {
    "rfBoneRing": "1 - smoothstep(@rfHalfWidth, @rfHalfWidth + 3, abs(@rfSpineDistance * 256 - @rfRadius))"
  }
}
```

The current distance source encodes 0–256 bind-pose units as 0–1. Radius and half-width above are therefore spatial units; the feather is 3 units. This is straight-line distance, not distance traveling along the armor surface. The node must resolve. Animate rfRadius with a wave for an oscillating band; a repeated ramp needs explicit retriggering logic, not the one-shot ramp signal.

<a id="fragment-5"></a>

## 5. A sweep shared across geometry pieces

Create a repeating band in the shared root-position frame so separate pieces can agree on its height.

**Result:** `@rfRootScan` (mask). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfScanPhase": {
      "expr": "time * 0.25"
    }
  },
  "sources": {
    "rfRootPosition": {
      "bake": "position"
    }
  },
  "masks": {
    "rfRootScan": "1 - smoothstep(0.04, 0.08, abs(frac(dot(@rfRootPosition, [0, 0, 1]) * 8 - @rfScanPhase) - 0.5))"
  }
}
```

Eight cycles span the current 256-unit position frame, giving 32-unit spacing and 8 units/second motion at this phase speed. The bake clamps outside its frame and has texture precision limits. position is shared across pieces; localPosition normalizes each geometry around its own bound and is suitable for per-piece reveals instead. Neither is a live world-position texture.

<a id="fragment-6"></a>

## 6. One UV chart

Restrict another effect to one UV island before choosing its orientation or density.

**Result:** `@rfChartMask` (mask). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfChartID": {
      "constant": 0
    }
  },
  "sources": {
    "rfCharts": {
      "bake": "chartId"
    }
  },
  "masks": {
    "rfChartMask": "1 - step(0.5, abs(@rfCharts * 255 - @rfChartID))"
  }
}
```

Use the mesh analysis to pick an ID. IDs are geometry-local and mesh-dependent, not persistent plate names; isolate the intended geometry with an output selector when necessary. Swap chartId for componentId to select connected mesh components. This mask selects a region but does not unwrap, reorient or rescale its UVs.

<a id="fragment-7"></a>

## 7. Soft combat gate

Ease a whole effect on or off as the wearer enters or leaves combat.

**Result:** `@rfCombatLevel` (scalar). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfInCombat": {
      "actorState": "inCombat"
    },
    "rfCombatLevel": {
      "smooth": {
        "of": "@rfInCombat",
        "seconds": 0.3
      }
    }
  }
}
```

Use rfCombatLevel as layer opacity, or multiply it into another scalar response. seconds is an exponential smoothing time constant, not an exact transition duration. Initial state may already be combat; this is a transition smoother, not a guaranteed fade-in on recipe application. sneaking, weaponDrawn, swimming and sprinting can be substituted for inCombat.

<a id="fragment-8"></a>

## 8. Resource-spending response

React to falling magicka rather than its remaining amount.

**Result:** `@rfSpendLevel` (scalar). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfSpendMagicka": {
      "av": "Magicka"
    },
    "rfSpendMax": {
      "av": {
        "of": "Magicka",
        "measure": "max"
      }
    },
    "rfSpendFraction": {
      "expr": "saturate(@rfSpendMagicka / max(@rfSpendMax, 1))"
    },
    "rfSpendRate": {
      "rate": "@rfSpendFraction"
    },
    "rfSpendRaw": {
      "expr": "saturate(-@rfSpendRate * 2)"
    },
    "rfSpendLevel": {
      "smooth": {
        "of": "@rfSpendRaw",
        "seconds": 0.15
      }
    }
  }
}
```

A loss rate of 0.5 of maximum magicka per second reaches full response with gain 2. Regeneration contributes zero. This detects any decrease, including drains or changes in the maximum; it does not identify a spell-cast event. Substitute Stamina or Health in both actor-value reads. The smoothing softens frame-scale spikes; it does not make rate a damage measurement.

<a id="fragment-9"></a>

## 9. Low-resource crossing flash

Fire once when magicka crosses below a threshold, with a shaped release.

**Result:** `@rfLowFlash` (scalar). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfWarnMagicka": {
      "av": "Magicka"
    },
    "rfWarnMax": {
      "av": {
        "of": "Magicka",
        "measure": "max"
      }
    },
    "rfLowState": {
      "expr": "if(@rfWarnMagicka / max(@rfWarnMax, 1) < 0.25, 1, 0)"
    },
    "rfLowFlash": {
      "trigger": {
        "when": "@rfLowState",
        "lifetime": 0.8,
        "max": 1
      },
      "curve": "@rfLowRelease"
    }
  },
  "curves": {
    "rfLowRelease": "pow(1 - x, 3)"
  }
}
```

when fires on a nonpositive-to-positive edge, not every tick the condition stays true. It also fires on the first evaluation if the condition starts positive. Recover above the threshold to re-arm. Values fluctuating around 25% can chatter; this fragment has no hysteresis. The release maps raw age 0→1 into brightness 1→0; without it, an idle trigger reads 1.

<a id="fragment-10"></a>

## 10. Hit heat with a palette

Accumulate hits into a cooling intensity and a coordinated color palette.

**Result:** `@rfHeatLevel` (scalar). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfHeatHit": {
      "trigger": {
        "event": "hit.received",
        "lifetime": 1,
        "max": 8
      }
    },
    "rfHeatCount": {
      "accumulate": {
        "trigger": "@rfHeatHit",
        "decay": 2
      }
    },
    "rfHeatLevel": {
      "expr": "saturate(@rfHeatCount / 4)"
    },
    "rfHeatColor": {
      "gradient": {
        "t": "@rfHeatLevel",
        "stops": [
          {
            "at": 0,
            "color": [
              0.05,
              0.15,
              0.4
            ]
          },
          {
            "at": 0.5,
            "color": [
              1,
              0.15,
              0.01
            ]
          },
          {
            "at": 1,
            "color": [
              1,
              0.85,
              0.45
            ]
          }
        ]
      }
    }
  }
}
```

Exports both scalar rfHeatLevel and RGB rfHeatColor. Each event adds one, regardless of damage; decay drains 2 counts per recipe-clock second. Four counts saturate the visible level, but the accumulator itself is not capped, so a sustained barrage can prolong cooling. max limits live trigger firings, not the accumulated total. Wire the color to a layer color and the level to opacity/intensity.

<a id="fragment-11"></a>

## 11. Three-hit meter with a combat reset

Keep a capped visual count until combat ends.

**Result:** `@rfChargeLevel` (scalar). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfCountCombat": {
      "actorState": "inCombat"
    },
    "rfCountOutside": {
      "expr": "1 - @rfCountCombat"
    },
    "rfCountReset": {
      "trigger": {
        "when": "@rfCountOutside",
        "lifetime": 0.1,
        "max": 1
      }
    },
    "rfCountHit": {
      "trigger": {
        "event": "hit.received",
        "lifetime": 0.5,
        "max": 8
      }
    },
    "rfCharges": {
      "counter": {
        "trigger": "@rfCountHit",
        "reset": "@rfCountReset",
        "cap": 3
      }
    },
    "rfChargeLevel": {
      "expr": "@rfCharges / 3"
    }
  }
}
```

cap stops the meter at three; it does not spend charges. Reset occurs on leaving combat (and initially if already outside), not continuously while outside. Hits received outside combat can therefore accumulate after the initial reset. If reset and hit arrive in the same evaluation, the current implementation resets first and then adds newly seen hits. State belongs to the live recipe instance, not a persistent gameplay statistic.

<a id="fragment-12"></a>

## 12. Two-pulse hit envelope

Produce an immediate flash followed by a smaller echo, without adding a second trigger.

**Result:** `@rfHitEcho` (scalar). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfEchoAge": {
      "trigger": {
        "event": "hit.received",
        "lifetime": 1.2,
        "max": 1
      }
    },
    "rfHitEcho": {
      "expr": "max(1 - smoothstep(0, 0.15, @rfEchoAge), 0.45 * (1 - smoothstep(0, 0.12, abs(@rfEchoAge - 0.45))))"
    }
  }
}
```

The raw trigger is normalized age: 0 at a firing, 1 when idle/expired. The first pulse ends at 0.18 seconds; the echo peaks at 0.54 seconds and has a 0.144-second half-width. The expression is zero at idle age 1. A new hit restarts this envelope; it does not independently sum echoes from older hits. Multiply any existing marking mask by this scalar.

<a id="fragment-13"></a>

## 13. Alternating footstep ripples

Combine separately anchored left and right ripples into one reusable field.

**Result:** `@rfStepMask` (mask). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfLeftStep": {
      "trigger": {
        "event": "anim.FootLeft",
        "anchor": {
          "node": "NPC L Foot [Lft ]"
        },
        "lifetime": 0.7,
        "max": 2
      },
      "curve": "@rfStepRelease"
    },
    "rfRightStep": {
      "trigger": {
        "event": "anim.FootRight",
        "anchor": {
          "node": "NPC R Foot [Rft ]"
        },
        "lifetime": 0.7,
        "max": 2
      },
      "curve": "@rfStepRelease"
    }
  },
  "curves": {
    "rfStepRelease": "pow(1 - x, 2)"
  },
  "sources": {
    "rfLeftRing": {
      "ripple": {
        "trigger": "@rfLeftStep",
        "speed": 70,
        "width": 6,
        "decay": 0.6
      }
    },
    "rfRightRing": {
      "ripple": {
        "trigger": "@rfRightStep",
        "speed": 70,
        "width": 6,
        "decay": 0.6
      }
    }
  },
  "masks": {
    "rfStepMask": "saturate(max(@rfLeftRing * @rfLeftStep, @rfRightRing * @rfRightStep))"
  }
}
```

The animation graph must emit these exact events; skeleton nodes must resolve. Animation mods can change events, and this is not physical ground-contact detection. The two sources add two spatial ripple workloads. Each envelope reads its newest firing even though the ripple retains up to two waves; rapid same-foot firings can brighten older retained waves through that shared envelope.

<a id="fragment-14"></a>

## 14. Facing the last attacker

Flash the bind-pose surfaces facing the last reported attacker position.

**Result:** `@rfAttackFacing` (mask). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfAttackEvent": {
      "trigger": {
        "event": "hit.received.position",
        "payload": "vec3",
        "lifetime": 0.8,
        "max": 1
      },
      "curve": "@rfAttackRelease"
    },
    "rfAttackWorld": {
      "payload": "@rfAttackEvent"
    },
    "rfWearerWorld": {
      "actorState": "position"
    },
    "rfAttackRoot": {
      "toRoot": "@rfAttackWorld"
    },
    "rfWearerRoot": {
      "toRoot": "@rfWearerWorld"
    },
    "rfAttackDirection": {
      "expr": "normalize(@rfAttackRoot - @rfWearerRoot)"
    }
  },
  "curves": {
    "rfAttackRelease": "pow(1 - x, 2)"
  },
  "sources": {
    "rfAttackNormal": {
      "bake": "normal"
    }
  },
  "masks": {
    "rfAttackFacing": "smoothstep(0.1, 0.7, dot(@rfAttackNormal * 2 - [1, 1, 1], @rfAttackDirection)) * @rfAttackEvent"
  }
}
```

The built-in .position event carries the attacker’s position, not damage, the weapon contact point, or a surface normal. payload holds the newest position after expiration; the release gates it back to darkness. Convert both points with toRoot before subtracting: converting a direction as a point would add translation. The captured world point stays fixed while the wearer/root can move, so this is not a frozen hit direction or an animation-correct impact decal.

<a id="fragment-15"></a>

## 15. Combat-target proximity

Raise an effect when the current combat target is nearby, with an explicit no-target gate.

**Result:** `@rfNearTarget` (scalar). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfHasTarget": {
      "actorState": "hasTarget"
    },
    "rfTargetWorld": {
      "actorState": "target"
    },
    "rfProximityWearer": {
      "actorState": "position"
    },
    "rfNearRaw": {
      "expr": "@rfHasTarget * (1 - smoothstep(128, 512, distance(@rfTargetWorld, @rfProximityWearer)))"
    },
    "rfNearTarget": {
      "smooth": {
        "of": "@rfNearRaw",
        "seconds": 0.2
      }
    }
  }
}
```

Full response inside 128 world units, none beyond 512. Target selection follows the game’s combat/targeting state; it is not a search for the nearest enemy. Without a target, the target vector is zero, so hasTarget is essential. Smoothing intentionally leaves a short fade after target loss.

<a id="fragment-16"></a>

## 16. Organic temporal flicker

Use the native smooth noise signal for a bounded, slowly varying modulation.

**Result:** `@rfFlicker` (scalar). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfNoise": {
      "noise": {
        "frequency": 3,
        "amplitude": 1,
        "seed": 17
      }
    },
    "rfFlicker": {
      "expr": "clamp(0.75 + 0.25 * @rfNoise, 0.5, 1)"
    }
  }
}
```

The underlying value noise is approximately -1..1; this maps it to 0.5..1. Frequency controls progression through the noise lattice, not a guaranteed number of flashes. All texels using this signal vary together. Equal seeds and recipe times produce the same sequence; change seeds or combine it with a spatial pattern for variation. This is a signal, not a per-texel noise() expression function.

<a id="fragment-17"></a>

## 17. Capture resource level on combat entry

Sample stamina once when combat begins, then hold that value as an effect parameter.

**Result:** `@rfEntryLevel` (scalar). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfEntryCombat": {
      "actorState": "inCombat"
    },
    "rfEntryStamina": {
      "av": "Stamina"
    },
    "rfEntryMaximum": {
      "av": {
        "of": "Stamina",
        "measure": "max"
      }
    },
    "rfEntryFraction": {
      "expr": "saturate(@rfEntryStamina / max(@rfEntryMaximum, 1))"
    },
    "rfEntryEvent": {
      "trigger": {
        "when": "@rfEntryCombat",
        "value": "@rfEntryFraction",
        "payload": "scalar",
        "lifetime": 0.2,
        "max": 1
      }
    },
    "rfEntryLevel": {
      "payload": "@rfEntryEvent"
    }
  }
}
```

payload reads the captured value, not the trigger’s age. It remains held after the firing expires and updates on the next combat entry; before any firing it is zero. Starting the recipe while already in combat samples on its first positive evaluation. This is runtime state, not saved progression.

<a id="fragment-18"></a>

## 18. Two-layer scrolling texture field

Combine two differently sampled copies of an existing image to make a less repetitive flowing field.

**Result:** `@rfFlowMask` (mask). No other cookbook fragment is required.

```json
{
  "signals": {
    "rfFlowA": {
      "expr": "time * [0.03, -0.02]"
    },
    "rfFlowB": {
      "expr": "time * [-0.01, 0.025]"
    }
  },
  "sources": {
    "rfSwirlA": {
      "image": {
        "path": "Effects\\DarkSwirls.dds",
        "channel": "luma",
        "space": "tiled",
        "tile": [
          3,
          3
        ],
        "scroll": "@rfFlowA"
      }
    },
    "rfSwirlB": {
      "image": {
        "path": "Effects\\DarkSwirls.dds",
        "channel": "luma",
        "space": "tiled",
        "tile": [
          5,
          5
        ],
        "scroll": "@rfFlowB",
        "transpose": true
      }
    }
  },
  "masks": {
    "rfFlowMask": "smoothstep(0.15, 0.55, @rfSwirlA * @rfSwirlB)"
  }
}
```

This uses the DarkSwirls path already used by the importer; the texture must resolve from the game’s assets and is not supplied by this document. Swap in an authored texture with suitable contrast and tiling. This is scrolling texture modulation, not fluid simulation. UV seams and mirrored charts still matter. Use a larger mip for softer broad detail, then tune the thresholds for the actual image.

## Combine placement, a pattern, and a response

For a bronze-colored, upward-facing marking that heats up when struck:

1. Start with one pattern from the procedural cookbook, keeping its `cbPattern`.
2. Merge fragments 1 (color), 3 (upward-facing) and 10 (hit heat) from this file.
3. Merge the masks below and replace the study recipe's outputs with this one
   output. In an established recipe, adapt its existing layer instead; changing
   emissive strength affects the material scalar shared by the composition.

```json
{
  "masks": {
    "cbEligible": "saturate(@rfColorMask * @rfUpMask)",
    "cbActivity": "@cbPattern * @rfHeatLevel"
  },
  "outputs": [
    {
      "target": "material",
      "slot": "emissive",
      "strength": 2,
      "resolution": "half",
      "stack": [
        {
          "source": [
            1,
            1,
            1
          ],
          "blend": "replace",
          "opacity": 0.08,
          "color": [
            0.1,
            0.2,
            0.35
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
          "opacity": 0.7,
          "color": "@rfHeatColor",
          "mask": "@cbActive"
        }
      ]
    }
  ]
}
```

`cbCoverage` and `cbActive` come from the pattern starter and already apply
`cbEligible`; do not multiply eligibility into `cbActivity` again. Otherwise a
soft edge is squared and becomes narrower. The dim marking remains when heat
is zero, while its activity and color react to hits.

Other combinations worth trying:

- A UV chart gate × chevrons, with the resource-spending signal driving a bright
  pulse and the held combat-entry resource selecting its initial color.
- A spatial root sweep × a color selection, with combat proximity controlling
  opacity. The wave travels across separate pieces while the material choice
  determines where it appears.
- A scrolling image × a material cluster, with hit heat driving diffuse color
  and roughness through separate amounts. Use a mask for coverage and a signal
  for each layer opacity; avoid equally strong changes in every material slot.
- The last-attacker-facing field × an authored plate mask, with the two-pulse
  envelope controlling an additional light. Facing is approximate bind-pose
  selection; it cannot supply an exact impact decal.

A `plugin` trigger can expose additional events from a cooperating SKSE plugin,
but merely naming an ID does not create a sender or guarantee its payload.
Likewise, do not assume a spell-cast animation event exists on every graph.
Use the input catalog and actual event observations before adding a dependency.

## Cost and verification

Start by previewing the result mask or signal alone, then compose it. Shared
names permit reuse within a recipe; copying a fragment under new names creates
separate rows and potentially more render work. Bakes, cluster analyses, ripple
fields, image sampling and mask intermediates have different costs. In
particular, a large graph of individually simple masks can retain many targets.
Keep visual validation separate from parser/schema validation and record the
armor, graph events and textures used by a working combination.

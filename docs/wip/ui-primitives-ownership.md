# UI-primitive layer `.cpp` ownership map

Wave-2 extension of the frozen studio shape. Every function declared in the
new UI-primitive headers maps to exactly one owning `.cpp`. Two headers are
pure spec data (`Widgets.h`) or header-only templates plus a thin accessor set
(`Fields.h` templates, `Page.h`); the one owning translation unit is
`studio/Fields.cpp`.

## Headers and the types they define

| Header | Key types |
| --- | --- |
| `Widgets.h` | `WidthMode`+`kWidthModeCount`, `Width`, `Column`, `TableBorders`+`kTableBordersCount`, `TableStyle`, `RuleAction`+`kRuleActions`, `RuleButton`, `RuleSpec`, `RuleClick`, `RowMove`, `ThumbnailSpec` |
| `Page.h` | `Page` |
| `Fields.h` | (functions only — `Bind*` binding factories, `BindSignalMember`/`BindSourceMember` templates, the widget-kind `FormField` builders) |

## Function → owning `.cpp`

### `Fields.cpp`

Binding factories (each returns a `FieldBinding`, captures by value):
`BindLayerSource`, `BindLayerCurve`, `BindLayerOpacity`, `BindLayerColor`,
`BindLayerMask`, `BindLayerChannels`, `BindLayerBlend`, `BindScalar`,
`BindLightParam`, `BindLightVector`, `BindLightShadow`, `BindLightReplace`,
`BindShellParam`, `BindShellVector`, `BindShellPoint`, `BindShellMaterial`,
`BindShellBlend`, `BindShellDepthBias`, `BindShellAlphaTest`, `BindPriority`,
`BindClockSpeed`, `BindOutputReplace`, `BindSignalKind`, `BindCurveText`,
`BindMaskText`.

Widget-kind `FormField` builders: `ValueField`, `ReferenceField`,
`ChoiceField`, `BlendField`, `ToggleField`, `TextedField`.

Page accessors: `ActorOf`, `BonesOf`, `ScaleOf`.

### No `.cpp` (owned by the header)

`Widgets.h` — all types are `constexpr`/aggregate spec data;
`RuleActionLabel` and `RuleButton::Label` are `constexpr` inline over
`kRuleActions`. `Fields.h` — `BindSignalMember` and `BindSourceMember` are
inline function templates (their bodies must be visible to the callers that
supply the member pointer). `Page.h` declares only the three accessors, all
homed in `Fields.cpp`.

## Reconciliation with `studio-ownership.md`

No overlap. The frozen map homes every whole-row form builder in `Panels.cpp`
(`InspectorForm`, `ScalarForm`, `SignalForm`, `SignalValueEdit`, `SourceForm`,
`LightForm`, `ShellForm`, `LiteralColor`, `LiteralColorText`,
`FieldDetailName`, `RowNameField`, `CurveTextField`, `MaskTextField`). This
layer adds the FIELD-LEVEL vocabulary those builders compose; it declares no
whole-row builder and redefines none of the frozen `Panels.cpp` functions.
`Fields.cpp` is a new translation unit that did not exist in the frozen map.

Two shared-name points, both overload-disambiguated and non-colliding:

- `BonesOf(const Page&)` (`Fields.cpp`, this layer) versus
  `BonesOf(const MeshData&)` (`mesh/MeshFacts.cpp`, wave 1). Distinct argument
  types, distinct namespaces (`Studio::` vs `BetterEnchantmentEffects::`);
  overload resolution selects by the argument.
- The field factories reuse recipe/'s published parsers (`ParseParam`,
  `ParseColorParam`, `ParseVec3Param`, `ParseLayerSource`, `ParseBlend`,
  `ChannelSet::Parse`, `DefaultSignalKind`, `ParseShellMaterial`,
  `ParseShellBlend`) rather than re-deriving parsing, per "call the core."

## Merge note: sigil stripping

`BindLayerMask` strips a leading `@` through a private `SigilName` helper in
`Fields.cpp` so this translation unit links against recipe/ alone while the
parallel studio fills are in flight. `studio/Names.h` declares
`ReferenceName` for the same job (homed in `Names.cpp`). At merge, route
`BindLayerMask` through `Names::ReferenceName` and drop `SigilName`, so one
function strips the sigil for the whole tree.

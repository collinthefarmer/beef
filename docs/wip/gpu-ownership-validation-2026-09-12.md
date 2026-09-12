# GPU ownership validation — 2026-09-12

This implements section 2 of the [engine-facing survey](engine-types-survey-2026-09-12.md).

## Implemented contract

`TextureLab::Init` constructs an owning `GpuResources` candidate. Required vertex
and pixel shaders, constant buffer, sampler, blend, depth and rasterizer states
must all succeed before the candidate is published and `Available()` becomes
true. Failure or an exception destroys the unpublished candidate. Shader bytecode
and compiler diagnostics also use `REX::W32::ComPtr`.

Interpreter, ripple and classification capabilities each contain a shader and its
constant buffer. Baking contains both shaders and its input layout. A failed
optional capability is absent and its partial resources are released; other
capabilities and the base pass remain usable. Ripple and classification bind null
in their unused constant-buffer slots, so they do not depend on another optional
capability being present.

Render targets privately own their texture and views. Curve lookups are created
through `CreateLookup` and forbid value copying and moving. Shared owners remain
the public lifetime mechanism. Temporary bake buffers and texture extent query
references are scoped COM owners. Engine device/context pointers remain explicitly
borrowed. Existing renderer state restoration and weak pool recycling are retained.

## Automated checks

The production header asserts the default-construction restriction for lookups and
copy/move restrictions for lookups and render targets. An isolated negative
compilation check confirms that external `Lookup({})` construction is rejected
because the construction-key constructor is private. The four changed render
translation units pass isolated Windows-target Clang syntax checks. The root
integration run supplies full Release build and repository checks.

These checks do not execute D3D resource creation or establish correct device
teardown ordering. There is no native fake D3D factory in the current adapter;
adding a test-only reproduction of its construction logic would not exercise the
production rollback paths.

## D3D integration cases still to execute

Use an instrumented D3D device/compiler boundary and live-object diagnostics to
fail one creation call at a time. Count acquired and released references, including
any non-null outputs returned with a failure result. Test exception unwinding after
partial construction separately from ordinary HRESULT failure.

| Case | Expected result |
| --- | --- |
| Required VS/PS compilation or shader object creation fails | `Init` fails; every acquired blob and object is released; all availability queries are false. |
| Required constant buffer, sampler, blend, depth or rasterizer creation fails | No resources are published; optional resources created earlier are released too. |
| Each interpreter/ripple/classification compile, shader or buffer creation fails | Only that capability is absent; required pipeline succeeds; partial objects are released. |
| Each bake compilation, VS, PS or layout creation fails | Baking is absent; earlier bake objects and blobs are released. |
| Interpreter absent with ripple available; interpreter/ripple absent with classify available | Remaining passes draw successfully with no null optional-record dereference or incorrect constant-buffer binding. |
| Target texture, SRV, RTV, presenter load or metadata allocation fails | No target escapes; acquired COM objects are released and presenter metadata is not partially installed. |
| Lookup texture or SRV creation fails | Factory returns no lookup and releases the texture/view acquired before failure. |
| Bake vertex-buffer success followed by index-buffer failure | Vertex buffer is released; render state restoration still runs. |
| Successful lab initialization, lookup/target use, then owner destruction | Created resources are released once per owned reference; presenter restoration precedes target resource destruction. |
| Target returned after its pool owner is gone | Weak recycling destroys the target without retaining or dereferencing the dead pool. |
| Repeated `Clear`, retained target, and later final owner release | Active targets remain usable; unused targets are released outside the pool mutex. |

Check normal copy/layer, expression, ripple, mesh-bake and material-cluster output
against the pre-change pixels. Confirm the game pipeline is restored on success,
early return and injected allocation failure. Renderer metadata synchronization,
backend UI consumption lifetime, UV alignment and cache identity are separate
survey work; this ownership change does not establish those contracts.

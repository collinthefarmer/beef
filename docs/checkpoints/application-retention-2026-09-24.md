# Application history retention

The ownership review found that recipe-scoped application records retained
distinct IDs indefinitely, including renamed/deleted IDs and canceled loads.
Retries also inherited every historical wearer, even after successful rendering
or an unmatched result. Terminal actor-scoped history already had a 256-record
allowance, but recipe history did not.

`ApplicationService` now retains at most 256 terminal recipe/whole-catalog
records, ordered by application revision. Records containing any queued or
prepared actor are protected, even when another actor makes the aggregate phase
failed. Pruning runs after request supersession, terminal recipe reports, and
cancellation. Old reports cannot recreate pruned records, and reused IDs still
receive newer revision tokens.

Replacement requests inherit pending and failed targets from retained records,
alongside the manager's currently loaded/applied actors. Completed successful or
unmatched historical wearers no longer accumulate over repeated edits. A retained
failure can still be retried without resupplying its actor ID. Once a terminal
record ages out, its retry-only targets are forgotten; normal manager discovery
continues to supply current actors.

Cancellation retains the application token and canceled phase but releases the
actor vector and prior problem text. Resume removes canceled recipe and actor
summaries. Previously published snapshots remain valid independent values.

These bounds apply to history, not the active workload or total bytes: current
requests retain all their actors, pending records can exceed the allowance, and
failed retry targets remain until superseded, pruned, or canceled. This does not
claim a measured crowd/resource budget.

Native coverage exercises ID churn beyond the history allowance, mixed
failed/pending aggregates, completion after pruning pressure, stale reports and
ID reuse, held snapshots, cancellation and repeated loads, 600 successive
successful wearers, and transitions between recipe and whole-catalog scope.
The tests call the production service; queued applicator and session suites
also protect the existing retirement/replacement behavior. They do not execute
the in-game rename UI or measure GPU resource retention.

Verification: `engine_applicationservice`, `engine_applicator`, and
`engine_sessionqueue` passed natively and under ASan/UBSan. Windows Release
compiled and linked. The initial header rebuild reported the existing four
missing-field initializer warnings in `menu/Menu.cpp`; the final storage-release
refinement rebuilt successfully. Fresh targeted clang-tidy on
`ApplicationService.cpp`, including its header, reported zero findings and passed
the existing baseline gate. Formatting, include layers, and `git diff --check`
passed. No staging, installation, or in-game run was performed.

Runtime acceptance remains in the active alpha plan: repeated editing, rename,
delete, reapply, eviction, and save loads should settle application status and
recover resources. Validate under the supported workload with candidate identity
and traces; native history checks alone do not establish those outcomes.

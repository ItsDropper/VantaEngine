# Vanta Reliability Engineering Standard

This document defines the minimum engineering bar for Vanta projects. It is a working standard, not a claim that the software is bug-free.

## Release gates

A change is releasable only when all applicable gates pass:

1. **Reproducible build:** document the toolchain and exact build/test commands.
2. **Automated tests:** add regression coverage for each fixed defect; tests must fail before the fix when practical.
3. **Debug and Release:** compile and run tests in both configurations in CI.
4. **Failure behavior:** validate external inputs, check resource/API failures, and fail with a useful diagnostic instead of silently continuing in an invalid state.
5. **No false performance claims:** compare equivalent workloads and record methodology; a successful build is not a performance benchmark.
6. **Safe rollout:** ship in small changes, keep a known-good revision, and provide a rollback path for updates that can change user data or runtime behavior.
7. **Honest status:** label features as implemented, experimental, or planned. Never describe an untested path as production-ready.

## Defect workflow

For every confirmed defect:

- Record reproduction steps, expected behavior, actual behavior, environment, and relevant logs.
- Identify the root cause rather than patching only the visible symptom.
- Add a regression test at the lowest practical layer.
- Keep the patch focused; avoid mixing unrelated refactors with the fix.
- Run the relevant test suite and review the diff for unintended changes.
- If a test cannot be run, state that limitation explicitly before release.

## Runtime resilience

- Reject or normalize invalid numeric inputs before they reach simulation, camera, or rendering math.
- Bound frame deltas and resource budgets.
- Check allocation, file, shader, graphics API, and device-loss results where the API exposes failure.
- Do not swallow errors; diagnostics should identify the subsystem and failed operation without leaking credentials or personal data.
- Keep recovery behavior deterministic where feasible.
- Treat corrupted or missing optional assets differently from corrupted required configuration; document fallback behavior.

## Security and privacy

- Collect no telemetry by default unless a project explicitly documents the data, purpose, retention, and opt-out.
- Never log access tokens, passwords, private keys, or full credential-bearing URLs.
- Keep secrets out of source control and test fixtures.
- Validate paths and untrusted asset metadata before use.
- Use least privilege and avoid requiring administrator rights without a specific technical need.

## VantaEngine validation commands

From the repository root on Windows with the documented MSVC/CMake/Ninja toolchain:

```powershell
.\scripts\build.ps1 -Test
.\scripts\build.ps1 -Configuration Release -Test
```

These commands compile and run the configured CTest suite. They do not prove absence of defects, guarantee identical results across GPUs/drivers, or replace manual rendering and performance checks.

## Definition of done

A task is done when the behavior is implemented, the relevant regression test exists, applicable tests pass, diagnostics are useful, documentation is accurate, and the change is reviewed. If any condition is unmet, record it as an explicit limitation rather than implying completion.

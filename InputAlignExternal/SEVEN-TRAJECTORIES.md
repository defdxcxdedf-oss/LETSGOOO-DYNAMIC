<!-- coderabbit-ai-generated:v1 -->

# InputAlign External 0.3 alpha

Generated implementation remains experimental.

- Shift+Tab toggles correction during an active level. Autorepeat is ignored; the captured release is consumed. This shortcut can conflict with Steam Overlay: change the overlay shortcut in Steam if needed.
- Normal and Wave limits support 0–400 ticks. **Todo a 400 ticks** overrides both limits while preserving their individual values. It does not increase the CPU budget.
- Native adapter v4 advances up to seven separate candidate states round-robin under one deadline, using adjacent input delays. It returns the earliest fully surviving candidate in the batch, never a partial candidate. Batches cover the configured horizon; a shorter remaining horizon uses fewer than seven candidates.
- Seven candidates share the same physics implementation, so they do not independently verify the game. More simultaneous candidates can increase cost before an early success; this is not a proven FPS optimization.
- A hypothetical candidate hitting unsupported behavior is discarded. Its notes do not contaminate another candidate. Actual replay still suspends the attempt on unsupported behavior. Prefix traversal stops at death/unsupported contact.
- Unknown/custom physics, active dual, CBF and unmodelled triggers remain rejected. No universal-object or full-state fidelity claim.
- The Python reference search retains sequential earliest-delay evaluation for test comparison; the runtime uses native adapter v4. Replace the entire worker directory when updating.
- Runtime packaging pins the Python 3.12.10 x64 embedded ZIP SHA256, rejects duplicate/colliding runtime members and unlisted vendor files.

## Build

The repository workflow builds the Windows bridge with Geode 5.11.2 and pinned GD bindings, runs native/Python tests and packages the worker. Locally use scripts/build-windows.ps1 on Windows with SDK, CLI, Python 3.12 and MSVC installed. A successful build is not in-game validation.

## Limits

The existing partial wire-state comparison, queued-input superseding policy, recording-clock behavior and game fidelity have not been proven by independent game traces. Do not call this a final stable release.

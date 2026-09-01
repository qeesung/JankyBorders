# Upstream issue and pull-request audit

Snapshot date: 2026-09-01  
Upstream baseline: `FelixKratz/JankyBorders@a7297ca` (`v1.9.0`)  
Scope: 59 open issues and 14 open pull requests at snapshot time.

This fork prioritizes reproducible correctness, memory safety, native-fullscreen
behavior, and mature low-maintenance features on macOS 26.5.2. An open state or
a clean GitHub merge status is not treated as evidence that a change is safe.

Decision meanings:

- **Implement**: included in this fork with regression coverage.
- **Reimplement**: the upstream idea is useful, but the submitted patch is not
  safe or does not compose with the other selected changes.
- **Conditional**: only retained if it can be reproduced or measured on the
  target machine within three focused attempts.
- **Defer**: outside the current compatibility or evidence boundary.
- **Resolved/configuration**: already addressed on the baseline, belongs to an
  external package, or has an existing configuration workaround.

## Issues

| Issue | Decision | Rationale / acceptance evidence |
|---|---|---|
| #207 How do I make it all square | Implement | Make `style=square` square at ordinary widths on macOS 26; cover with geometry and visual tests. |
| #202 Support `XDG_CONFIG_HOME` | Implement | Add absolute-path XDG lookup with documented fallback precedence. |
| #201 Border missing after fullscreen | Implement | Refresh cached Space IDs and migrate existing helper windows with bounded retries. |
| #200 v1.9.0 reliability regression | Implement | Restore normal `WINDOW_IGNORES_CYCLE` document windows without adding title-based AX scans. |
| #199 Unstable resize on macOS 26.5 | Resolved/configuration | Upstream marked fixed by v1.9.0; require a new reproduction against this baseline. |
| #197 Multiple configurations | Defer | Help/configuration request; no concrete correctness defect. |
| #194 Square border still round inside | Implement | Same square-geometry correction as #207. |
| #193 Display scaling | Resolved/configuration | `hidpi=on` is the existing explicit trade-off; no stable defect isolated. |
| #190 Homebrew build failure on macOS 26 | Defer | External distribution concern; fork CI builds on macOS 26, but this task does not update a tap. |
| #189 Cannot add borders to Accessibility | Resolved/configuration | Accessibility is optional for core rendering and no new AX dependency is added. |
| #188 High GPU usage on Tahoe | Conditional | Keep a change only with an objective equal-load resource comparison. |
| #186 Apple Pay button disabled | Defer | Apple security UI rejects overlays; document `order=below` or app exclusion rather than bypassing it. |
| #184 Homebrew 5 formula deprecation | Defer | Belongs to the external formula repository. |
| #182 Border lingers during close/minimize animation | Defer | No timely system event is known; reject continuous polling that regresses idle cost. |
| #181 AeroSpace hidden corner windows | Defer | External window-manager behavior without a reliable native visibility signal. |
| #180 Border ordering while dragging | Defer | Needs a fresh reproducible trace after the ordering/fullscreen changes. |
| #179 Animate the active border | Defer | Feature expansion with ongoing rendering cost. |
| #173 Ghost box with fractional widths | Conditional | Retain only if pixel-alignment tests reproduce and prove a correction. |
| #166 Borders in Zoom screen sharing | Conditional | Zoom is unavailable on the target host; do not guess at private sharing flags. |
| #165 Focus after minimizing | Defer | Depends on delayed system events; reassess only with a stable event trace. |
| #163 Window snapping interference | Defer | No isolated JankyBorders trigger or safe correction established. |
| #159 Support Ventura | Defer | Fork compatibility target is macOS 26.5.2 only. |
| #157 Border offset | Implement | Covered by `position=auto|inside|center|outside`. |
| #155 Combine gradient and glow | Implement | Adopt the composable color model with strict parsing and rendering tests. |
| #154 AeroSpace accordion focus | Defer | External focus/event semantics; no stable native-only reproduction yet. |
| #150 Non-uniform border color | Defer | Needs a reproducible rendering fixture before changing antialiasing. |
| #147 Mission Control transformed windows | Defer | WindowServer transform changes have no reliable event and polling is out of scope. |
| #146 Preserve border when maximized | Implement | `position=auto` moves only clipped sides inward. |
| #144 Hide borders for lower sublayers | Resolved/configuration | Existing ordering/application filters cover the supported behavior. |
| #142 Disable borders | Implement | Add and document `style=none`, including runtime transitions. |
| #139 Whitelist with Nix-installed apps | Resolved/configuration | Application-name/configuration issue; no core defect established. |
| #138 Blacklisted apps appear smaller | Resolved/configuration | Borders do not resize target windows; requires an external-manager reproduction. |
| #134 Color based on input language | Defer | Event-driven product feature outside the stability batch. |
| #131 Persistent borders | Defer | Broad state/persistence feature outside the selected mature set. |
| #130 Independent-project question | Defer | Discussion rather than an actionable defect. |
| #128 WindowServer/borders memory growth | Conditional | Require a two-hour equal-load RSS/WindowServer slope comparison. |
| #126 Render borders inside | Implement | Add explicit inside/center/outside modes and default auto clipping protection. |
| #123 Safari extension management blocked | Defer | Security UI overlay limitation; prefer app filtering and documentation. |
| #122 Single-edge highlight | Implement | Reimplement 1/2/4-color side rendering on the unified color model. |
| #118 Input loss after logout | Resolved/configuration | Marked available on current upstream baseline; require a new v1.9 trace. |
| #117 Shade inactive windows | Defer | Visual feature outside the selected set. |
| #115 Sequoia issues | Defer | Fork is intentionally tested only on macOS 26.5.2. |
| #113 Oversized Todoist quick-add border | Implement | Address through safe native window suitability rules; no title AX scan. |
| #112 Missing framework headers | Defer | Toolchain/distribution issue; macOS 26 CI provides the supported build proof. |
| #111 Ignore child/tool windows | Implement | Balance transient-window rejection with #200 main-window correctness. |
| #110 Homebrew clang build error | Defer | Do not recommend destructive LLVM removal; verify the supported compiler in CI. |
| #108 Accent from window color | Defer | New color-sampling feature outside the mature set. |
| #106 Stale/sliced border after close | Conditional | Require a repeatable lifecycle trace before changing cleanup behavior. |
| #104 Visualize yabai stacks | Defer | External integration feature outside the selected set. |
| #99 Dock-toggle glitches | Conditional | Retain only with a deterministic refresh/rescan regression. |
| #95 Inner/center/outer configuration | Implement | Public positioning API and geometry tests. |
| #93 Resize tearing | Resolved/configuration | Later upstream resize work supersedes the original report; require a v1.9 reproduction. |
| #83 Window-management bridge crash | Conditional | High impact, but accept only a trace-backed lifecycle fix. |
| #81 Border disappears after moving Space | Implement | Same SID/helper-window migration repair as #201. |
| #74 Toggle border | Implement | `style=none` is the explicit runtime toggle. |
| #73 App/window rules | Partial implement | Fix native suitability and retain app lists; title-regex/AX rule expansion is deferred. |
| #57 Per-window configuration | Resolved/configuration | `apply-to=<window-id>` is already present on the baseline. |
| #45 Double borders | Defer | Additional rendering model outside the selected mature features. |
| #29 Stage Manager transformed windows | Defer | Reliable handling would require continuous transform polling. |

## Pull requests

| PR | Decision | Rationale / integration rule |
|---|---|---|
| #208 CAContext nine-slice renderer | Defer / reimplement later | macOS 26.5.2 builds and symbol probes pass, and removing full-size backing stores may reduce memory, but the PR has no CI/review or long-run RSS/GPU evidence, does not fix fullscreen/SID migration, conflicts with the selected color/position models, and fails to recreate an existing per-window helper for `renderer=` changes. Preserve the idea for a later opt-in backend with CG fallback and a two-hour comparison. |
| #206 Per-side colors | Reimplement | Useful feature, but its color representation conflicts with #203 and needs shared strict parsing/tests. |
| #205 IPC/event hardening | Reimplement | The reported bounds issues are real; preserve global/per-window setting scope and validate the Mach envelope too. |
| #204 Adaptive redraw debounce | Reject | Delayed blocks capture raw `border *` and can run after destruction; the linked issue is already closed. |
| #203 Glow plus gradient | Adopt with review | Use as the base color model, retaining attribution and adding composition tests. |
| #198 Build binaries | Partial reimplement | Add build/test CI only; exclude release creation and external Homebrew writes. |
| #196 Active-only mode | Reimplement | Reuse the common helper-window Space migration path before enabling reuse across focus/Space changes. |
| #195 Undefined-behavior fallbacks | Reimplement | Keep the defensive intent and cover allocation failures and zero-element arrays omitted by the patch. |
| #191 Resize/orphan memory leak | Conditional reimplement | Conflicts with v1.9 resize code and proposes unsafe lifetime state; retain only measured, lifecycle-safe pieces. |
| #183 Window-title regex filters | Defer | Adds AX permission/scanning cost and fails unsafely when titles are unavailable. |
| #170 Document uniform style | Adopted | Cherry-picked with original authorship as `f5ed5ea`. |
| #156 LLVM troubleshooting | Reject | Recommends destructive package removal without establishing the root cause. |
| #143 `style=none` | Adopt with review | Add explicit parsing, documentation, tests, and focus recovery on re-enable. |
| #135 AeroSpace documentation typo | Adopted | Cherry-picked with original authorship as `061196a`. |

## Completion record

Implementation commits, exact verification commands, CI runs, dynamic test
counts, and conditional-item measurements are appended here before delivery.

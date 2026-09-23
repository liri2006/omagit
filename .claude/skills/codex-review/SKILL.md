---
name: codex-review
description: Adversarial code-review loop with Codex — the main session reviews the current diff first, debates findings with Codex (gpt-6-astra high) until they agree on a fix list, delegates the fixes to an Opus 5.5 agent (opus-implementer), then re-reviews. Repeats until both reviewers have no remaining findings. Use after an implementation lands (especially one produced by opus-implementer) or when the user asks for a joint/thorough review.
---

# Review loop with Codex

Roles: **You** (main session) review first, own architecture/taste/correctness judgments, and are the final authority on every disputed finding. **Codex** (gpt-6-astra, high reasoning, read-only sandbox) reviews independently, challenges your findings, and adds its own. Fixes are implemented by the `opus-implementer` subagent (an Opus 5.5 agent pinned to `claude-opus-5-5` in its frontmatter; do not pass a `model` override), never by Codex and never by the review session itself beyond trivial one-liners.

Conventions:

- `$SCRATCH` = this session's scratchpad directory. All codex commands run from the repo root with the maximum Bash timeout (600000 ms).
- One persistent Codex review session spans ALL cycles — start it once, `codex exec resume <SESSION_ID>` for every later exchange, so Codex remembers what was already discussed and fixed.
- Announce each phase to the user as it happens (e.g. "Review cycle 1: my findings → Codex", "Codex disputed 2 of 5 findings, added 1 of its own").

## Scope

Review the current working diff (`git diff` + untracked files). If the working tree is clean, review the current branch against `main`, or the last commit (`git diff HEAD~1`) when the branch is `main` itself. The user may narrow scope via arguments.

## Cycle N, phase 1 — your review

Review the diff YOURSELF first, before Codex sees anything. Check at minimum:

- Correctness, including Qt object lifetime and parenting, signal/slot connections, thread affinity (git work runs via `GitRepo::runAsync` / worker threads, UI only on the main thread), and settings round-trips.
- The repo's layering: the core (`OMAGIT_CORE_*` in `omagit.pri`: `GitRepo`, `RemoteSync`, `AskPass`, `DesktopExec.h`, …) must stay free of QtGui so `tests/gitrepo_test` keeps building with `QT -= gui`; `MainWindow` is the composition root, pages (`CommitPage`, `DiffPane`, `Footer`) are separate widgets; new QSettings keys go in `src/Settings.h`; shared widgets come from `ui::` in `src/UiHelpers.*`; colours and fonts come from `OmarchyTheme`, never hard-coded.
- Refresh behaviour: F5, the file watcher and fetches must preserve selection, scroll offsets and the diff position; unchanged data must not reset models.
- Test coverage of the changed behaviour in the right suite (`gitrepo`, `mergedialog`, `ui`), and that new sources are registered in `omagit.pri`.
- Taste for user-facing changes: wording, Nerd Font glyph choice, spacing, consistency with the Omarchy look (TickMenu ticks on the right, no checkboxes on the left, section labels, hairlines).

Write numbered findings to `$SCRATCH/review-findings-N.md`: each with file:line, severity (must-fix / should-fix / nit), what's wrong, and the proposed fix.

## Cycle N, phase 2 — debate with Codex

First cycle only — start the session (capture the session id from the run log with `grep -m1 "session id:"`):

```
codex exec -m gpt-6-astra -c model_reasoning_effort="high" -s read-only --output-last-message "$SCRATCH/review-codex-1.md" - < "$SCRATCH/review-task-1.md" > "$SCRATCH/review-run1.log" 2>&1
```

where `review-task-1.md` contains the scope (exact diff command to run), a short repo orientation (Qt 6 Widgets app, `omagit.pri` layering, `tests/run.sh` suites, the checklist above), your findings, and this instruction:

> You are co-reviewing this diff with another reviewing architect. Run the diff yourself and inspect the surrounding code. For each of the architect's numbered findings reply AGREE or DISPUTE with one-paragraph rationale. Then add any findings of your own, numbered continuing the list, each with file:line, severity, and proposed fix. Do not modify any files. End with a REMARKS section: either exactly AGREED (you accept the combined list as-is) or the numbered points still in dispute.

Later cycles / later turns: `codex exec resume <SESSION_ID> --output-last-message "$SCRATCH/review-codex-N.md" - < "$SCRATCH/review-task-N.md"`.

Negotiate to an agreed fix list, max 3 exchanges per cycle:

- Evaluate Codex's disputes and additions on the merits — verify claims against the actual code (Read/Grep), don't concede on assertion alone.
- You have final authority: after 3 exchanges, rule on anything still disputed and record the ruling.
- The output of this phase is `$SCRATCH/fixlist-N.md`: the agreed (or ruled) numbered fixes, each concrete enough to implement without further judgment.

If the agreed fix list is EMPTY and Codex says AGREED — the loop is done; go to Finalize.

## Cycle N, phase 3 — implement fixes

- Trivial taste/one-line fixes: apply directly yourself.
- Everything else: hand `fixlist-N.md` to `opus-implementer` (Agent tool, `subagent_type: "opus-implementer"`, no `model` override; announce it, BRIEF markers, include verification commands — at minimum `qmake6 omagit.pro && make -j$(nproc)` plus `tests/run.sh <suites covering the touched code>`).
- After the subagent reports, review the fix diff yourself and confirm verification passed. For visible changes, also take an offscreen screenshot (`XDG_CONFIG_HOME=<fresh scratch dir> QT_QPA_PLATFORM=offscreen ./omagit --no-fetch --screenshot "$SCRATCH/after.png" …`) and look at it; wipe the scratch `XDG_CONFIG_HOME` between batches so persisted toggles don't leak between runs.

## Cycle N+1

Re-enter phase 1 against the updated diff. Tell Codex in the next task file which findings were fixed and what changed, so it re-reviews rather than repeating itself.

Hard cap: 3 full cycles. If findings still remain after cycle 3, fix the remainder directly yourself (they should be small by then) or report honestly to the user what is still open and why.

## Finalize

- Save a review record to `docs/reviews/YYYY-MM-DD-<short-slug>.md` (create the folder if missing): scope, cycles run, findings by origin (yours / Codex's), disputes and how they were resolved, fixes applied, verification results, anything left open. Give the user the path.
- Summarize in chat: total findings, who caught what, notable disagreements, and the final state of verification.

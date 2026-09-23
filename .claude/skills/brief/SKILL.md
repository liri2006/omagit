---
name: brief
description: Co-create an implementation brief with Codex — Codex (gpt-6-astra high) drafts and revises, the main session critiques and owns architecture/taste, iterating until they agree. Use before delegating non-trivial features to opus-implementer (Opus 5.5), or when the user asks for a brief/plan co-authored with Codex.
---

# Brief co-creation with Codex

Roles: **Codex** (gpt-6-astra, high reasoning, read-only sandbox) explores the repo, drafts, and revises the brief. **You** (main session) critique each draft, own all architecture and taste decisions, and are the final authority on disagreements.

Conventions for every command below:

- `$SCRATCH` = this session's scratchpad directory.
- Run ALL codex commands from the repo root. (`codex exec resume` does not accept `-C` and requires a trusted git directory as cwd.)
- Give each codex call the maximum Bash timeout (600000 ms). If one times out, resume it with the same loop mechanics.

## Round 0 — task statement (you write this)

Write `$SCRATCH/brief-task.md` containing:

- The goal, in the user's terms, plus any context from the conversation Codex needs (it cannot see this conversation).
- Applicable repo conventions and constraints. For Omagit that means at least:
  - C++17 / Qt 6 Widgets, built with `qmake6` (`qmake6 omagit.pro && make -j$(nproc)`); source lists live in `omagit.pri` — `OMAGIT_CORE_*` (git wrapper, `RemoteSync`, `AskPass`, …) must stay free of QtGui because `tests/gitrepo_test` builds with `QT -= gui`; `OMAGIT_WIDGET_*` is the app minus `main.cpp`. New files are registered there.
  - `MainWindow` is the composition root; pages are separate widgets (`CommitPage`, `DiffPane`, `Footer`, `HistoryView`, `MergeDialog`, `KeybindingsPanel`, `LoginDialog`); shared widget helpers live in `src/UiHelpers.*` (namespace `ui`); every QSettings key is a constant in `src/Settings.h`; colours, fonts and light/dark mode come from `OmarchyTheme` and are never hard-coded; keyboard shortcuts are declared in `MainWindow::bindings()` so they show in the keybindings panel.
  - Refresh (F5, watcher, fetch) must preserve selection, scroll offsets and the diff position; unchanged data must not reset models.
  - Tests: `tests/run.sh [gitrepo|mergedialog|ui]`; git-wrapper behaviour is tested in `tests/gitrepo_test.cpp` against throw-away repos, widget logic in `tests/ui_test.cpp`, the merge view in `tests/mergedialog_test.cpp`. Screenshot checks run offscreen with `XDG_CONFIG_HOME` pointed at a scratch dir and `--no-fetch`.
  - Every feature is written from scratch; no code is copied from other git GUIs.
- All taste decisions if the work is user-facing: exact label and status strings, layout, which `ui::` helpers, Nerd Font glyphs, which keybinding (lazygit letter + Ctrl) — decide these yourself first, never leave them to Codex.
- This instruction, verbatim, at the end:

  > You are co-authoring an implementation brief with a reviewing architect. Explore the repository as needed. Produce a brief with sections: Goal, Files to change (exact paths), Implementation steps, Test plan, Acceptance criteria, Verification commands, Open questions. Do not write any code. End with a REMARKS section listing your assumptions and anything you are unsure about.

## Round 1 — initial draft (Codex)

Announce to the user first, e.g. "Asking Codex (gpt-6-astra high) to draft the brief."

```
codex exec -m gpt-6-astra -c model_reasoning_effort="high" -s read-only --output-last-message "$SCRATCH/brief-v1.md" - < "$SCRATCH/brief-task.md" > "$SCRATCH/brief-run1.log" 2>&1
```

Capture the session id for the loop: `grep -m1 "session id:" "$SCRATCH/brief-run1.log"`.

## Rounds 2..N — critique loop (max 4 Codex turns total)

1. Read the latest draft. Critique it YOURSELF — do not delegate the critique. Check at minimum:
   - Repo architecture conventions honored (core/widget split in `omagit.pri`, composition root, `Settings.h`, `ui::` helpers, `OmarchyTheme`).
   - File paths are real and right — spot-check with Glob/Read, don't trust them.
   - Scope: nothing missing, no scope creep, no future specs implemented early.
   - Test plan actually covers the acceptance criteria and names the right suite; verification commands are the real ones (`qmake6 omagit.pro && make -j$(nproc)`, `tests/run.sh …`, offscreen screenshot flags).
   - Taste decisions from Round 0 preserved verbatim, not "improved".
2. Write the critique to `$SCRATCH/brief-critique-N.md`, ending with:

   > Revise the full brief, incorporating or explicitly rebutting each numbered point. Reply with the complete revised brief, then a REMARKS section that is either exactly the word AGREED or a numbered list of pushbacks.

3. Send it:

   ```
   codex exec resume <SESSION_ID> --output-last-message "$SCRATCH/brief-vN.md" - < "$SCRATCH/brief-critique-N.md" > "$SCRATCH/brief-runN.log" 2>&1
   ```

4. Briefly tell the user how the round went — especially any genuine disagreement and who conceded.
5. Stop when REMARKS is AGREED and you have no remaining objections. If 4 Codex turns pass without agreement, you settle the contested points yourself and edit the brief directly — you are the final authority; note your rulings in the brief.

## Finalize

- Save the agreed brief (with any final rulings edited in) to `docs/briefs/YYYY-MM-DD-<short-slug>.md` in the repo (create `docs/briefs/` if missing). This is the user-visible artifact — working drafts and critiques stay in `$SCRATCH`, but the final brief always lands here. Give the user the file path in your summary.
- Summarize for the user: goal, files to change, test plan in one line, any contested points and how they resolved.
- Ask the user whether to proceed to implementation. If yes, hand the saved brief file's contents to `opus-implementer` (Agent tool, `subagent_type: "opus-implementer"`, no `model` override; announce the delegation, BRIEF markers, review the diff after).

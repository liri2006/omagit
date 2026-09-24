#pragma once

#include <QString>

// Every QSettings key the application uses, in one place so that the side
// writing a setting and the side reading it back cannot drift apart. The
// strings are the ones omagit.conf already holds: renaming one silently
// loses the user's setting, so they only ever grow. Which file they land in
// is decided by the organisation and application names main() sets.
namespace settings {

// ---- The window -------------------------------------------------------------

// Geometry of the main window as saveGeometry() left it (QByteArray).
constexpr auto kWindowGeometry = QLatin1StringView("window/geometry");
// Which pane layout is showing, spelled as paneLayoutKey() spells it.
constexpr auto kWindowLayout = QLatin1StringView("window/layout");
// Pre-0.3 spelling of "the left section fills the window"; read for migration
// only, and only while window/layout is still unset (bool).
constexpr auto kWindowLeftFull = QLatin1StringView("window/leftFull");
// Whether the diff pane sits beside the left section (bool, default true).
constexpr auto kWindowDiffPane = QLatin1StringView("window/diffPane");
// Width in pixels the left section is given the next time the splitter is
// laid out; unset until the user drags it (int).
constexpr auto kWindowLeftWidth = QLatin1StringView("window/leftWidth");
// State of the commit page's changes-over-message splitter (QByteArray).
constexpr auto kWindowCommitSplitter = QLatin1StringView("window/commitSplitter");
// The same splitter's state from when the message stood over the changes; its
// sizes are the wrong way round now, so it is only ever removed.
constexpr auto kWindowCommitMessageSplitter = QLatin1StringView("window/commitMessageSplitter");
// How the commit page lists its pending files: "compact", "tree" or "table"
// (the default, and what an unknown value falls back to). Which collapsed
// directories a tree run had is deliberately not remembered.
constexpr auto kWindowFilesView = QLatin1StringView("window/filesView");

// ---- The diff ---------------------------------------------------------------

// Side by side rather than one pane (bool, default true).
constexpr auto kDiffTwoPane = QLatin1StringView("diff/twoPane");
// Colour the diff by the file's syntax (bool, default true). Whitespace
// deliberately has no key: it stays off until the user asks for it again.
constexpr auto kDiffSyntaxHighlighting = QLatin1StringView("diff/syntaxHighlighting");

// ---- The commit message agent -----------------------------------------------

// The agent CLI last chosen ("claude", "codex"), and the model and reasoning
// effort picked for it. Both of those belong to that one agent, so they are
// cleared when the agent changes or is no longer installed.
constexpr auto kAgentName = QLatin1StringView("agent/name");
constexpr auto kAgentModel = QLatin1StringView("agent/model");
constexpr auto kAgentEffort = QLatin1StringView("agent/effort");

// ---- Merging ----------------------------------------------------------------

// Whether the merge dialog offers `--no-ff` ticked (bool, default false).
constexpr auto kMergeNoFastForward = QLatin1StringView("merge/noFastForward");

// ---- The remote -------------------------------------------------------------

// Seconds between automatic fetches; 0 turns them off. There is no UI for it,
// the user edits omagit.conf, which is why the default lives here too.
constexpr auto kAutoFetchSeconds = QLatin1StringView("remote/autoFetchSeconds");
constexpr int kAutoFetchSecondsDefault = 180; // the interval RemoteSync starts with

// ---- Repositories -----------------------------------------------------------

// The repositories opened before, the most recent first (QStringList).
constexpr auto kRecentRepositories = QLatin1StringView("repos/recent");

} // namespace settings

#include "AskPass.h"
#include "CommitPage.h"
#include "GitRepo.h"
#include "MainWindow.h"
#include "CloneDialog.h"
#include "OmarchyTheme.h"
#include "Settings.h"

#include <QAbstractScrollArea>
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QKeySequence>
#include <QMenu>
#include <qpa/qwindowsysteminterface.h>
#include <QPainter>
#include <QRegularExpression>
#include <QSettings>
#include <QScrollBar>
#include <QTimer>
#include <QWindow>

namespace {

// Is this process the askpass helper of a git or ssh run of ours, and what is
// it being asked? Both point their ASKPASS variable at this binary and run it
// as `omagit "<prompt>"` — git through a shell, so the explicit
// `omagit --askpass "<prompt>"` works there as well, while ssh execs the
// helper directly and can pass no flag of its own. What tells the two apart
// from someone opening a repository is OMAGIT_ASKPASS_SOCKET: only the app
// puts it in the environment of the git processes it starts.
bool askPassPrompt(int argc, char *argv[], QString *prompt)
{
    for (int i = 1; i < argc; ++i) {
        if (qstrcmp(argv[i], "--askpass") != 0)
            continue;
        *prompt = i + 1 < argc ? QString::fromLocal8Bit(argv[i + 1]) : QString();
        return true;
    }
    if (!qEnvironmentVariableIsSet("OMAGIT_ASKPASS_SOCKET") || argc != 2 || argv[1][0] == '-')
        return false;
    *prompt = QString::fromLocal8Bit(argv[1]);
    return true;
}

// A flag that was spelt wrong: say so where the shell will see it and stop.
int usageError(const QString &message)
{
    fprintf(stderr, "%s\n", qPrintable(message));
    return 2;
}

} // namespace

int main(int argc, char *argv[])
{
    // Before the QApplication: as a helper the program answers one question
    // on stdout and exits, with no window, no theme and no display.
    QString prompt;
    if (askPassPrompt(argc, argv, &prompt)) {
        QCoreApplication app(argc, argv);
        QFile out;
        if (!out.open(stdout, QIODevice::WriteOnly))
            return 1;
        return askPassClient(qEnvironmentVariable("OMAGIT_ASKPASS_SOCKET"), prompt, &out);
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("omagit"));
    QCoreApplication::setApplicationName(QStringLiteral("omagit"));
    QCoreApplication::setApplicationVersion(QStringLiteral(OMAGIT_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("omagit"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Pending changes, history and diff viewer for git, themed by Omarchy."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("path"), QStringLiteral("Directory or file inside a git repository (default: cwd)."));
    QCommandLineOption screenshotOpt(QStringLiteral("screenshot"), QStringLiteral("Render the window to <file> and exit (for testing)."), QStringLiteral("file"));
    QCommandLineOption selectOpt(QStringLiteral("select"), QStringLiteral("Pre-select the given repo-relative path."), QStringLiteral("path"));
    QCommandLineOption historyOpt(QStringLiteral("history"), QStringLiteral("Open the history view instead of the commit dialog."));
    QCommandLineOption fullOpt(QStringLiteral("full"), QStringLiteral("Start with the diff pane hidden: the left section fills the window."));
    QCommandLineOption miniOpt(QStringLiteral("mini"), QStringLiteral("Start in the Mini layout: a rail of file miniatures next to the diff pane (in a narrow window, the stacked Diff tab)."));
    QCommandLineOption amendOpt(QStringLiteral("amend"), QStringLiteral("Open the commit dialog with \"Amend last commit\" ticked."));
    QCommandLineOption screenshotAfterOpt(QStringLiteral("screenshot-after"), QStringLiteral("Milliseconds to wait before taking the --screenshot (default 800)."), QStringLiteral("ms"), QStringLiteral("800"));
    QCommandLineOption noFetchOpt(QStringLiteral("no-fetch"), QStringLiteral("Do not fetch by itself to keep the Pull count current."));
    QCommandLineOption screenshotMenuOpt(QStringLiteral("screenshot-menu"), QStringLiteral("Open the branch, repo, agent, keybindings, merge, login or clone panel, the Mini layout's commit popover, the New branch card, the stacked layout's sync, more or options menu, or the narrow diff pane's view options, before taking the --screenshot (for testing)."), QStringLiteral("branch|repo|agent|keybindings|merge|login|clone|commit|newbranch|sync|more|options|diff"));
    parser.addOption(screenshotOpt);
    parser.addOption(screenshotAfterOpt);
    parser.addOption(selectOpt);
    parser.addOption(historyOpt);
    parser.addOption(fullOpt);
    parser.addOption(miniOpt);
    parser.addOption(amendOpt);
    parser.addOption(noFetchOpt);
    parser.addOption(screenshotMenuOpt);
    // Handled before the QApplication above; here so --help mentions it.
    QCommandLineOption askPassOpt(QStringLiteral("askpass"), QStringLiteral("Ask the running Omagit for the given credential prompt and print the answer (what git and ssh run)."), QStringLiteral("prompt"));
    parser.addOption(askPassOpt);
    QCommandLineOption screenshotSizeOpt(QStringLiteral("screenshot-size"), QStringLiteral("Window size for the --screenshot, as WxH (for testing; the size is not remembered)."), QStringLiteral("WxH"));
    parser.addOption(screenshotSizeOpt);
    QCommandLineOption screenshotKeysOpt(QStringLiteral("screenshot-keys"), QStringLiteral("Comma-separated keys (m,a,Down,Return) sent to the focused widget once the --screenshot-menu dropdown is open, or to the window; @objectName[:vbar] or @ClassName[:vbar] focuses that (first visible) widget or its vertical scrollbar first (for testing)."), QStringLiteral("keys"));
    parser.addOption(screenshotKeysOpt);
    QCommandLineOption filesViewOpt(QStringLiteral("files-view"), QStringLiteral("How the commit dialog lists its files for this run: compact, tree or table (for testing; the choice is not remembered)."), QStringLiteral("compact|tree|table"));
    parser.addOption(filesViewOpt);
    parser.process(app);

    const QStringList args = parser.positionalArguments();
    const QString start = args.isEmpty() ? QDir::currentPath() : args.first();

    // A fixed window size for the screenshots, so a picture does not depend on
    // the desktop it was taken on. Checked before anything is opened: a
    // misspelt size is a usage error, not a window of some other size.
    QSize screenshotSize;
    if (parser.isSet(screenshotSizeOpt)) {
        if (!parser.isSet(screenshotOpt))
            return usageError(QStringLiteral("--screenshot-size only makes sense with --screenshot."));
        const QString value = parser.value(screenshotSizeOpt);
        static const QRegularExpression form(QStringLiteral("^(\\d+)x(\\d+)$"));
        const QRegularExpressionMatch parts = form.match(value);
        bool okWidth = false, okHeight = false;
        const int width = parts.hasMatch() ? parts.captured(1).toInt(&okWidth) : 0;
        const int height = parts.hasMatch() ? parts.captured(2).toInt(&okHeight) : 0;
        if (!okWidth || !okHeight || width <= 0 || height <= 0)
            return usageError(QStringLiteral("--screenshot-size takes two positive numbers, as in 945x612 — not \"%1\".").arg(value));
        screenshotSize = QSize(width, height);
    }

    // Likewise checked before a repository is opened: a files view nobody
    // recognises is a usage error, not a quiet fall back to the table.
    QString filesView;
    if (parser.isSet(filesViewOpt)) {
        filesView = parser.value(filesViewOpt);
        bool known = false;
        CommitPage::viewFromKey(filesView, &known);
        if (!known)
            return usageError(QStringLiteral("--files-view takes compact, tree or table — not \"%1\".").arg(filesView));
    }

    OmarchyTheme theme;
    theme.apply(app);

    QString error;
    QString root = GitRepo::findRoot(start, &error);
    // Started from somewhere outside a repository (no path given): show the
    // repository opened last instead, the way an editor reopens its files.
    if (root.isEmpty() && args.isEmpty()) {
        const QStringList recent = MainWindow::recentRepositories();
        for (const QString &path : recent) {
            root = GitRepo::findRoot(path);
            if (!root.isEmpty())
                break;
        }
    }
    if (root.isEmpty()) {
        CloneDialog dialog(CloneDialog::defaultFolder(), nullptr, true);
        if (dialog.exec() != QDialog::Accepted)
            return 0;
        root = dialog.repositoryPath();
    }

    GitRepo repo(root);
    MainWindow window(&repo);
    // A --screenshot run is a test: it must not leave its offscreen geometry behind.
    if (!parser.isSet(screenshotOpt)) {
        QObject::connect(&app, &QApplication::aboutToQuit, [&window] {
            QSettings().setValue(settings::kWindowGeometry, window.saveGeometry());
        });
    }
    if (parser.isSet(selectOpt))
        window.setInitialSelection(parser.value(selectOpt));
    if (parser.isSet(miniOpt))
        window.setPaneLayout(PaneLayout::Mini, false);
    if (parser.isSet(fullOpt))
        window.setDiffPaneVisible(false, false);
    if (parser.isSet(historyOpt))
        window.setMode(MainWindow::HistoryMode);
    if (parser.isSet(noFetchOpt))
        window.setAutoFetchEnabled(false);
    // Whatever page the flags above asked for: the override only changes how
    // the commit dialog lists its files, never which page is on.
    if (!filesView.isEmpty())
        window.setFilesView(filesView);
    // After the restored geometry and the layout flags, before the first show.
    if (screenshotSize.isValid())
        window.resize(screenshotSize);
    window.show();
    if (parser.isSet(amendOpt))
        QTimer::singleShot(0, &window, [&window] { window.setAmend(true); });
    if (parser.isSet(screenshotOpt)) {
        const QString file = parser.value(screenshotOpt);
        bool validAfter = false;
        const int given = parser.value(screenshotAfterOpt).toInt(&validAfter);
        // A misspelt --screenshot-after would otherwise grab at once.
        const int after = validAfter ? given : screenshotAfterOpt.defaultValues().first().toInt();
        const QString menu = parser.value(screenshotMenuOpt);
        if (!menu.isEmpty()) {
            // The dropdown runs its own event loop; the grab below happens inside it.
            QTimer::singleShot(after, &window, [&window, menu] {
                const char *slot = menu == QLatin1String("repo") ? "showRepoMenu"
                    : menu == QLatin1String("keybindings")       ? "showKeybindings"
                    : menu == QLatin1String("agent")             ? "showAgentMenu"
                    : menu == QLatin1String("merge")             ? "showMergeDialog"
                    : menu == QLatin1String("clone")             ? "showCloneDialog"
                    : menu == QLatin1String("login")             ? "showLoginDialog"
                    : menu == QLatin1String("commit")            ? "showCommitPopover"
                    : menu == QLatin1String("sync")              ? "showSyncMenu"
                    : menu == QLatin1String("more")              ? "showMoreMenu"
                    : menu == QLatin1String("options")           ? "showOptionsMenu"
                    : menu == QLatin1String("diff")              ? "showDiffOptionsMenu"
                    : menu == QLatin1String("newbranch")         ? "showNewBranchCard"
                                                                 : "showBranchMenu";
                QMetaObject::invokeMethod(&window, slot);
            });
        }
        const QStringList keys = parser.value(screenshotKeysOpt).split(QLatin1Char(','), Qt::SkipEmptyParts);
        // The merge view works out its verdict first; keys and the grab wait for it.
        const int settle = menu == QLatin1String("merge") ? 1200 : 0;
        if (!keys.isEmpty()) {
            // Inside a dropdown the keys go to its focused field; otherwise
            // to the window itself, where the shortcuts (Ctrl+G, …) live,
            // early enough for a long --screenshot-after to show their effect.
            QTimer::singleShot(menu.isEmpty() ? qMin(after, 800) : after + 200 + settle, &window, [&window, keys] {
                if (!window.windowHandle())
                    return;
                // The window of the widget an @ token focused: a dialog just
                // opened may not be the focus window yet when the keys go out.
                QWindow *target = nullptr;
                for (const QString &name : keys) {
                    if (name.startsWith(QLatin1Char('@'))) {
                        // Deliver the keys so far, then move the focus for the rest.
                        QWindowSystemInterface::flushWindowSystemEvents();
                        QCoreApplication::sendPostedEvents();
                        const QString wanted = name.mid(1).section(QLatin1Char(':'), 0, 0);
                        const bool vbar = name.endsWith(QLatin1String(":vbar"));
                        for (QWidget *w : window.findChildren<QWidget *>()) {
                            if (!w->isVisible() || (w->objectName() != wanted && QLatin1String(w->metaObject()->className()) != wanted))
                                continue;
                            auto *area = qobject_cast<QAbstractScrollArea *>(w);
                            (vbar && area ? static_cast<QWidget *>(area->verticalScrollBar()) : w)->setFocus();
                            target = w->window()->windowHandle();
                            break;
                        }
                        continue;
                    }
                    const QKeyCombination combo = QKeySequence::fromString(name)[0];
                    const QString text = name.size() == 1 ? name : QString();
                    // As the window system would report the key, so that the
                    // shortcuts see it first; a QKeyEvent posted by hand goes
                    // straight to the focused widget and never reaches them.
                    QWindow *win = target ? target
                        : QGuiApplication::focusWindow() ? QGuiApplication::focusWindow() : window.windowHandle();
                    QWindowSystemInterface::handleKeyEvent(win, QEvent::KeyPress, combo.key(), combo.keyboardModifiers(), text);
                    QWindowSystemInterface::handleKeyEvent(win, QEvent::KeyRelease, combo.key(), combo.keyboardModifiers(), text);
                }
                // Before the grab, which may be due in this same event loop turn.
                QWindowSystemInterface::flushWindowSystemEvents();
            });
        }
        QTimer::singleShot(after + (menu.isEmpty() ? 0 : 500 + settle), &window, [&window, file] {
            QPixmap shot = window.grab();
            // A dialog (the merge view) and a dropdown are windows of their own: paint them on top.
            QPainter p(&shot);
            // A submenu is the active popup over the menu it came from, which is still open.
            QList<QWidget *> popups;
            for (QWidget *w = QApplication::activePopupWidget(); w; w = qobject_cast<QMenu *>(w->parentWidget()))
                if (w->isVisible())
                    popups.prepend(w);
            popups.prepend(QApplication::activeModalWidget());
            for (QWidget *w : std::as_const(popups))
                if (w && w != &window)
                    p.drawPixmap(w->mapToGlobal(QPoint(0, 0)) - window.mapToGlobal(QPoint(0, 0)), w->grab());
            shot.save(file);
            QCoreApplication::exit(0);
        });
    }
    return app.exec();
}

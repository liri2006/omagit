// The definitions of the shared helpers fixtures.h declares; the fixtures
// themselves stay in the header.
#include "fixtures.h"

#include "../../src/DiffPane.h"
#include "../../src/LoginDialog.h"
#include "../../src/UiHelpers.h"

#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpSocket>

std::unique_ptr<OmarchyTheme> g_theme;

bool writeFixture(const QString &path, const QByteArray &data, bool executable)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        return false;
    file.close();
    return !executable || file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QString stamp(int hour)
{
    return QStringLiteral("2024-01-01T%1:00:00+00:00").arg(hour, 2, 10, QLatin1Char('0'));
}

bool git(const QString &dir, const QStringList &args, int hour)
{
    QProcess p;
    p.setWorkingDirectory(dir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_AUTHOR_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_AUTHOR_EMAIL"), QStringLiteral("test@example.com"));
    env.insert(QStringLiteral("GIT_COMMITTER_NAME"), QStringLiteral("Test"));
    env.insert(QStringLiteral("GIT_COMMITTER_EMAIL"), QStringLiteral("test@example.com"));
    if (hour > 0) {
        env.insert(QStringLiteral("GIT_AUTHOR_DATE"), stamp(hour));
        env.insert(QStringLiteral("GIT_COMMITTER_DATE"), stamp(hour));
    }
    p.setProcessEnvironment(env);
    p.start(QStringLiteral("git"), QStringList{QStringLiteral("-c"), QStringLiteral("commit.gpgsign=false")} + args);
    if (!p.waitForFinished(15000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0) {
        qWarning("git %s failed: %s", qPrintable(args.join(' ')), p.readAllStandardError().constData());
        return false;
    }
    return true;
}

bool commit(const QString &dir, const QString &message, int hour)
{
    return git(dir, {QStringLiteral("commit"), QStringLiteral("--allow-empty"), QStringLiteral("-q"),
                     QStringLiteral("-m"), message}, hour);
}

void settle()
{
    QTest::qWait(30);
}

QModelIndex treeRow(ChangesTreeModel *model, const QString &path, int column)
{
    const QModelIndex file = model->indexForPath(path);
    return (file.isValid() ? file : model->indexForDirectory(path)).siblingAtColumn(column);
}

std::unique_ptr<CommitPage> commitPage(GitRepo *repo)
{
    // The page asks the agent CLIs on PATH for their models as it is built;
    // a PATH with only the repository on it keeps those processes out of
    // these tests.
    const QByteArray env = qgetenv("PATH");
    qputenv("PATH", repo->root().toUtf8());
    auto page = std::make_unique<CommitPage>(repo);
    qputenv("PATH", env);
    page->resize(760, 600);
    page->show();
    page->reload();
    return page;
}

CommitFixture commitFixture()
{
    CommitFixture f;
    f.dir.reset(new QTemporaryDir);
    const QString path = f.dir->path();
    if (!f.dir->isValid() || !git(path, {"init", "-q", "-b", "main"}))
        return f;
    writeFixture(QDir(path).filePath(QStringLiteral("a.txt")), "a\n");
    writeFixture(QDir(path).filePath(QStringLiteral("b.txt")), "b\n");
    if (!git(path, {"add", "-A"}) || !git(path, {"commit", "-q", "-m", "first"}, 1))
        return f;
    writeFixture(QDir(path).filePath(QStringLiteral("a.txt")), "a changed\n");
    writeFixture(QDir(path).filePath(QStringLiteral("b.txt")), "b changed\n");
    writeFixture(QDir(path).filePath(QStringLiteral("u1.txt")), "u1\n");
    writeFixture(QDir(path).filePath(QStringLiteral("u2.txt")), "u2\n");
    f.repo.reset(new GitRepo(path));
    f.page = commitPage(f.repo.get());
    return f;
}

CommitFixture nestedFixture()
{
    CommitFixture f;
    f.dir.reset(new QTemporaryDir);
    const QString path = f.dir->path();
    const QDir root(path);
    if (!f.dir->isValid() || !git(path, {"init", "-q", "-b", "main"}))
        return f;
    for (const QString folder : {"Beta", "alpha", "src", "src/deep", "src/Deep", "tests", "single"})
        if (!root.mkpath(folder))
            return f;
    const QStringList tracked{"Beta/x.txt",  "alpha/y.txt",    "src/a.txt",    "src/deep/b.txt",
                              "src/Deep/c.txt", "tests/remove.txt", "single/one.txt", "root.txt"};
    for (const QString &file : tracked)
        writeFixture(root.filePath(file), "one\n");
    if (!git(path, {"add", "-A"}) || !git(path, {"commit", "-q", "-m", "first"}, 1))
        return f;
    for (const QString &file : tracked)
        if (file != QLatin1String("tests/remove.txt"))
            writeFixture(root.filePath(file), "two\n");
    QFile::remove(root.filePath(QStringLiteral("tests/remove.txt")));
    writeFixture(root.filePath(QStringLiteral("tests/new.txt")), "u\n");
    writeFixture(root.filePath(QStringLiteral("top.txt")), "u\n");
    f.repo.reset(new GitRepo(path));
    f.page = commitPage(f.repo.get());
    return f;
}

std::unique_ptr<QTcpServer> unauthorizedServer()
{
    auto server = std::make_unique<QTcpServer>();
    QObject::connect(server.get(), &QTcpServer::newConnection, server.get(), [listener = server.get()] {
        while (QTcpSocket *socket = listener->nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                // Answer once the request headers are in: closing on a socket
                // with unread data would reach curl as a reset, not a 401.
                const QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                if (!request.contains("\r\n\r\n")) {
                    socket->setProperty("request", request);
                    return;
                }
                socket->write("HTTP/1.1 401 Unauthorized\r\n"
                              "WWW-Authenticate: Basic realm=\"test\"\r\n"
                              "Content-Length: 0\r\n"
                              "Connection: close\r\n"
                              "\r\n");
                socket->disconnectFromHost();
            });
        }
    });
    if (!server->listen(QHostAddress::LocalHost))
        return {};
    return server;
}

BarFixture topBar(const QString &repository,
                  const QString &branch, int count)
{
    BarFixture f;
    f.host.reset(new QWidget);
    f.host->resize(1600, 200);
    f.bar = new TopBar(f.host.get());
    f.bar->setRepositoryName(repository);
    f.bar->setBranchLabel(branch);
    f.bar->setChangesCount(count);
    f.bar->move(0, 0);
    f.bar->resize(f.bar->sizeHint());
    f.host->show();
    return f;
}

WindowFixture mainWindow(int extra, bool mini,
                         const std::function<void(MainWindow *)> &beforeShow,
                         const QString &branch, const QString &folder)
{
    WindowFixture f;
    f.dir.reset(new QTemporaryDir);
    f.tools.reset(new QTemporaryDir);
    const QString path = folder.isEmpty() ? f.dir->path() : QDir(f.dir->path()).filePath(folder);
    const QString gitBinary = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (!f.dir->isValid() || !f.tools->isValid() || gitBinary.isEmpty() || !QDir().mkpath(path))
        return f;
    // A PATH with nothing on it but git: the window's commit page asks the
    // coding-agent CLIs for their models as it is built, and no such process
    // belongs in a test.
    if (!QFile::link(gitBinary, QDir(f.tools->path()).filePath(QStringLiteral("git"))))
        return f;
    if (!git(path, {"init", "-q", "-b", "main"}))
        return f;
    writeFixture(QDir(path).filePath(QStringLiteral("a.txt")), "a\n");
    if (!git(path, {"add", "-A"}) || !git(path, {"commit", "-q", "-m", "first"}, 1))
        return f;
    if (!branch.isEmpty() && !git(path, {QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("-b"), branch}))
        return f;
    writeFixture(QDir(path).filePath(QStringLiteral("a.txt")), "a changed\n");
    writeFixture(QDir(path).filePath(QStringLiteral("u1.txt")), "u1\n");
    for (int i = 1; i <= extra; ++i)
        writeFixture(QDir(path).filePath(QStringLiteral("f%1.txt").arg(i, 2, 10, QLatin1Char('0'))), "f\n");
    // The window commits with git's own configuration: an identity of the
    // repository's own, and no signing, whatever the desktop's git says.
    if (!git(path, {"config", "user.name", "Test"}) || !git(path, {"config", "user.email", "test@example.com"})
        || !git(path, {"config", "commit.gpgsign", "false"}))
        return f;
    f.repo.reset(new GitRepo(path));
    const QByteArray env = qgetenv("PATH");
    qputenv("PATH", f.tools->path().toUtf8());
    f.window.reset(new MainWindow(f.repo.get()));
    qputenv("PATH", env);
    f.window->setAutoFetchEnabled(false);
    if (mini)
        f.window->setPaneLayout(PaneLayout::Mini, false);
    f.window->resize(1200, 800);
    if (beforeShow)
        beforeShow(f.window.get());
    f.window->show();
    return f;
}

QStringList barNames(TopBar *bar)
{
    QStringList out;
    for (const QWidget *w : QList<const QWidget *>{bar->repoButton(), bar->branchButton(), bar->changesTab(),
                                                   bar->historyTab(), bar->pullButton(), bar->pushButton(),
                                                   bar->fetchButton(), bar->mergeButton(), bar->moreButton(),
                                                   bar->layoutButton(), bar->diffToggle()})
        out << w->accessibleName();
    return out;
}

const QStringList kBarNames{QStringLiteral("omagit-workspace"),
                            QStringLiteral("feature/askpass-login-dialog"),
                            QStringLiteral("Changes"),
                            QStringLiteral("History"),
                            QStringLiteral("Pull"),
                            QStringLiteral("Push"),
                            QStringLiteral("Fetch"),
                            QStringLiteral("Merge"),
                            QStringLiteral("More"),
                            QStringLiteral("Mini layout"),
                            QStringLiteral("Diff pane")};

int iconFormWidth()
{
    return ui::space(ui::pad::control + ui::box::icon + ui::pad::control);
}

QStringList barMetrics(TopBar *bar)
{
    const auto entry = [](const char *name, int value) { return QStringLiteral("%1=%2").arg(QLatin1String(name)).arg(value); };
    return {entry("hint", bar->sizeHint().width()),
            entry("hintHeight", bar->sizeHint().height()),
            entry("min", bar->minimumSizeHint().width()),
            entry("minHeight", bar->minimumSizeHint().height()),
            entry("changesTab", bar->changesTab()->sizeHint().width()),
            entry("historyTab", bar->historyTab()->sizeHint().width()),
            entry("tabs", bar->changesTab()->parentWidget()->sizeHint().width())};
}

QList<QAction *> filledMenu(QMenu *menu)
{
    emit menu->aboutToShow();
    return menu->actions();
}

QStringList menuTexts(const QList<QAction *> &actions)
{
    QStringList out;
    for (const QAction *a : actions)
        out << (a->isSeparator() ? QStringLiteral("-") : a->text());
    return out;
}

QSplitter *bodySplitter(const WindowFixture &f)
{
    return qobject_cast<QSplitter *>(f.window->findChild<DiffPane *>()->parentWidget());
}

int designLeftWidth(const QWidget *window)
{
    const int w = window->width();
    return w >= ui::space(1400) ? ui::space(560) : w >= ui::space(1000) ? ui::space(400) : ui::space(340);
}

QRect rectIn(const QWidget *w, const QWidget *ref)
{
    return QRect(w->mapTo(ref, QPoint(0, 0)), w->size());
}

QRect tileSquare(const QToolButton *tile)
{
    const int side = ui::space(40);
    return QRect((tile->width() - side) / 2, tile->height() - side, side, side);
}

bool activate(QWidget *w)
{
    w->activateWindow();
    return QTest::qWaitForWindowActive(w);
}

QString longParagraph()
{
    return QStringLiteral("Make the toolbar tiling aware so that the labels fold first, then the icons and then "
                          "the buttons themselves, and keep the branch name whole for as long as the row allows "
                          "it, eliding only at the very end when nothing else is left to fold away.");
}

int freshContentHeight(const MessageEdit *edit, const QString &text)
{
    MessageEdit fresh;
    fresh.setFont(edit->font());
    // The corner button's glyph decides how much of the width the text gets.
    fresh.cornerButton()->setText(edit->cornerButton()->text());
    fresh.applyTheme();
    fresh.resize(edit->size());
    fresh.setPlainText(text);
    fresh.show();
    if (!QTest::qWaitForWindowExposed(&fresh))
        return -1;
    QTest::qWait(30);
    return fresh.contentHeight();
}

bool closeTo(const QColor &a, const QColor &b)
{
    return qAbs(a.red() - b.red()) <= 3 && qAbs(a.green() - b.green()) <= 3 && qAbs(a.blue() - b.blue()) <= 3;
}

void clickAt(QWidget *window, const QPoint &pos, Qt::KeyboardModifiers modifiers)
{
    QWidget *target = window->childAt(pos);
    if (!target)
        target = window;
    QTest::mouseClick(target, Qt::LeftButton, modifiers, target->mapFrom(window, pos));
}

QWidget *tileRule(const WindowFixture &f)
{
    for (QWidget *w : f.tile()->parentWidget()->findChildren<QWidget *>())
        if (w != f.tile() && w->height() == 1)
            return w;
    return nullptr;
}

const char kClaudeHelp[] =
    "Options:\n"
    "  --effort <level>                      Effort level for the current session\n"
    "                                        (low, medium, high, xhigh, max)\n"
    "  --environment <environment_id>        Create a new cloud session\n"
    "  --model <model>                       Model for the current session. Provide\n"
    "                                        an alias for the latest model (e.g.\n"
    "                                        'fable', 'opus', or 'sonnet') or a\n"
    "                                        model's full name (e.g.\n"
    "                                        'claude-fable-5').\n"
    "  --no-chrome                           Disable Claude in Chrome integration\n";

bool writeFakeAgent(const QString &dir, const QString &name, const char *probe, const QByteArray &answer)
{
    const QByteArray script = QByteArray("#!/bin/sh\nif [ \"$1\" = ") + probe
        + " ]; then\nwhile IFS= read -r line; do printf '%s\\n' \"$line\"; done <<'EOF'\n" + answer
        + "\nEOF\nelse\nwhile IFS= read -r line; do :; done\necho 'Fake subject'\nfi\n";
    return writeFixture(QDir(dir).filePath(name), script, true);
}

bool writeFakeClaude(const QString &dir)
{
    return writeFakeAgent(dir, QStringLiteral("claude"), "--help", kClaudeHelp);
}

bool says(LoginDialog *dialog, const QString &text)
{
    for (const QLabel *label : dialog->findChildren<QLabel *>())
        if (label->isVisible() && QString(label->text()).remove(QChar(0x200B)).contains(text))
            return true;
    return false;
}

QString helperBinary()
{
    // ../build/tests/ui_test → the repository root next to it.
    QString binary = qEnvironmentVariable("OMAGIT_BINARY");
    if (binary.isEmpty())
        binary = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../omagit"));
    binary = QFileInfo(binary).absoluteFilePath();
    return QFileInfo::exists(binary) ? binary : QString();
}

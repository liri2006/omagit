// The ui suite's main(): one application for every test class, in settings
// and runtime directories of its own, the classes run in the order their
// files registered them in. `ui_test CloneTest -v2` runs that class alone.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/CredentialKeeper.h"
#include "../../src/OmarchyTheme.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

int main(int argc, char **argv)
{
    // Whatever the caller's environment says: the tests write settings and
    // AgentScope deletes <config>/omarchy/defaults/agent, so no run may
    // reach the real ~/.config; the askpass tests listen, and clear sockets
    // away, in the runtime directory. Both outlive the application.
    QTemporaryDir config, runtime;
    if (!config.isValid() || !runtime.isValid()) {
        qCritical("ui_test: no temporary directory for XDG_CONFIG_HOME and XDG_RUNTIME_DIR");
        return 1;
    }
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(config.path()));
    qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));

    QApplication app(argc, argv);
    app.setOrganizationName("omagit-tests");
    app.setApplicationName("ui-test");
    g_theme = std::make_unique<OmarchyTheme>();
    g_theme->apply(app);
    // A helper no machine has, so the sign-in's "Remember" offer does not come
    // and go with whether this one has libsecret; the tests of the offer name
    // a helper of their own.
    CredentialKeeper::setHelper(QStringLiteral("omagit-no-such-helper"));

    // A class named first: that class alone, with the arguments after its name.
    if (argc > 1) {
        for (const UiTestClass &test : uiTestClasses()) {
            if (qstrcmp(argv[1], test.name) != 0)
                continue;
            QList<char *> args{argv[0]};
            for (int i = 2; i < argc; ++i)
                args << argv[i];
            const std::unique_ptr<QObject> object = test.make();
            return QTest::qExec(object.get(), int(args.size()), args.data());
        }
    }

    bool failed = false;
    for (const UiTestClass &test : uiTestClasses()) {
        const std::unique_ptr<QObject> object = test.make();
        if (QTest::qExec(object.get(), argc, argv) != 0)
            failed = true;
    }
    return failed ? 1 : 0;
}

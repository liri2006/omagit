// UiTestCase's own slots, which QTest runs around every test of the suite,
// and the registry of the suite's test classes.
#include "UiTestCase.h"
#include "fixtures.h"

#include "../../src/OmarchyTheme.h"

#include <QApplication>
#include <QSettings>

QList<UiTestClass> &uiTestClasses()
{
    static QList<UiTestClass> classes;
    return classes;
}

// The test application's own settings file, emptied: no test sees what
// another one chose, saved or left behind.
void UiTestCase::init()
{
    QSettings().clear();
}

// A theme test that stopped half-way can leave its scratch theme directory
// named and OmarchyTheme::instance() on a theme of its own, or on none once
// that one is gone: the desktop's comes back before the next test.
void UiTestCase::cleanup()
{
    qunsetenv("OMAGIT_THEME_DIR");
    if (OmarchyTheme::instance() != g_theme.get()) {
        g_theme = std::make_unique<OmarchyTheme>();
        g_theme->apply(*qApp);
    }
}

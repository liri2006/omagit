// The base of every test class of the ui suite: each test starts from empty
// settings and ends with the shared theme current. And the registry main()
// runs the classes from, which each test file joins with UI_TEST(Class).
#pragma once

#include <QList>
#include <QObject>

#include <memory>

class UiTestCase : public QObject
{
    Q_OBJECT
private slots:
    // QTest finds these on the base class and runs them around every test
    // (every data row) of the class under test.
    void init();
    void cleanup();
};

// One test class of the suite: its name, as `ui_test <name>` picks it, and a
// way to make one.
struct UiTestClass
{
    const char *name;
    std::unique_ptr<QObject> (*make)();
};

// Every class, in the order the files registered them in: the order they
// are linked in, which is the order ui.pro lists them in.
QList<UiTestClass> &uiTestClasses();

template <typename Test>
struct UiTestRegistration
{
    explicit UiTestRegistration(const char *name)
    {
        uiTestClasses().append({name, []() -> std::unique_ptr<QObject> { return std::make_unique<Test>(); }});
    }
};

#define UI_TEST(Class) static const UiTestRegistration<Class> Class##Registration(#Class)

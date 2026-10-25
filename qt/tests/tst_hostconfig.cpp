#include "HostConfig.h"

#include <QTemporaryFile>
#include <QTest>

class TestHostConfig : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void defaultsMatchGhostty()
    {
        HostConfig config(QStringLiteral("/nonexistent/monstar-config"), {});
        QCOMPARE(config.tabsLocation(), QStringLiteral("top"));
        QCOMPARE(config.showTabBar(), QStringLiteral("auto"));
        QCOMPARE(config.unfocusedSplitOpacity(), 0.7);
        QVERIFY(config.inheritWorkingDirectory());
        QVERIFY(!config.focusFollowsMouse());
        QCOMPARE(config.unfocusedSplitFill(), config.background());
    }

    void fileThenOverridesApply()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        file.write("# comment\n"
                   "font-family = Iosevka\n"
                   "qt-tabs-location = left\n"
                   "qt-tabs-location = sideways\n"
                   "window-show-tab-bar = always\n"
                   "unfocused-split-opacity = 0.05\n"
                   "background = #102030\n"
                   "split-divider-color = 405060\n"
                   "focus-follows-mouse = true\n");
        file.close();
        HostConfig config(file.fileName(), {QStringLiteral("qt-tabs-location=bottom"),
                                            QStringLiteral("window-inherit-working-directory=false")});
        QCOMPARE(config.tabsLocation(), QStringLiteral("bottom"));
        QCOMPARE(config.showTabBar(), QStringLiteral("always"));
        QCOMPARE(config.unfocusedSplitOpacity(), 0.15);
        QCOMPARE(config.background(), QColor(0x10, 0x20, 0x30));
        QCOMPARE(config.unfocusedSplitFill(), QColor(0x10, 0x20, 0x30));
        QCOMPARE(config.splitDividerColor(), QColor(0x40, 0x50, 0x60));
        QVERIFY(config.focusFollowsMouse());
        QVERIFY(!config.inheritWorkingDirectory());
    }

    void invalidValuesAreRejected()
    {
        HostConfig config(QStringLiteral("/nonexistent/monstar-config"), {});
        QVERIFY(!config.set(QStringLiteral("window-show-tab-bar"), QStringLiteral("sometimes")));
        QVERIFY(!config.set(QStringLiteral("unfocused-split-fill"), QStringLiteral("#12345")));
        QVERIFY(!config.set(QStringLiteral("focus-follows-mouse"), QStringLiteral("yes")));
        QVERIFY(config.set(QStringLiteral("font-size"), QStringLiteral("12")));
        QCOMPARE(config.showTabBar(), QStringLiteral("auto"));
    }
};

QTEST_GUILESS_MAIN(TestHostConfig)
#include "tst_hostconfig.moc"

// monstar-qt settings, read from Monstar's own configuration file. Keys the
// host does not use are left to the terminals; invalid values warn and keep
// the previous value, like Monstar.
#pragma once

#include <QColor>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

class HostConfig : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by TerminalHost")

    Q_PROPERTY(QString tabsLocation READ tabsLocation NOTIFY changed)
    Q_PROPERTY(QString showTabBar READ showTabBar NOTIFY changed)
    Q_PROPERTY(qreal unfocusedSplitOpacity READ unfocusedSplitOpacity NOTIFY changed)
    Q_PROPERTY(QColor unfocusedSplitFill READ unfocusedSplitFill NOTIFY changed)
    Q_PROPERTY(QColor splitDividerColor READ splitDividerColor NOTIFY changed)
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(bool focusFollowsMouse READ focusFollowsMouse NOTIFY changed)
    Q_PROPERTY(QString windowDecoration READ windowDecoration NOTIFY changed)
    Q_PROPERTY(int windowCornerRadius READ windowCornerRadius NOTIFY changed)
    Q_PROPERTY(qreal backgroundOpacity READ backgroundOpacity NOTIFY changed)
    // The background color with backgroundOpacity applied.
    Q_PROPERTY(QColor translucentBackground READ translucentBackground NOTIFY changed)

public:
    // `path` overrides the default location; `overrides` are `key=value`
    // entries applied after the file, like `monstar -o`.
    HostConfig(const QString &path, const QStringList &overrides, QObject *parent = nullptr);

    // Reread the file and overrides, then emit `changed`.
    void reload();

    // Apply one entry. Returns false for invalid values of host keys;
    // unknown keys are accepted because they belong to the terminal.
    bool set(const QString &key, const QString &value);
    // Parse `key = value` lines.
    void parse(const QString &text);

    QString path() const { return m_path; }

    // top, bottom, left, or right.
    QString tabsLocation() const { return m_tabsLocation; }
    // always, auto, or never.
    QString showTabBar() const { return m_showTabBar; }
    qreal unfocusedSplitOpacity() const { return m_unfocusedSplitOpacity; }
    QColor unfocusedSplitFill() const { return m_unfocusedSplitFill.isValid() ? m_unfocusedSplitFill : background(); }
    QColor splitDividerColor() const { return m_splitDividerColor; }
    // The configured terminal background, or a dark default before the
    // first terminal frame arrives.
    QColor background() const { return m_background.isValid() ? m_background : QColor(0x1e, 0x1e, 0x1e); }
    bool focusFollowsMouse() const { return m_focusFollowsMouse; }
    // client draws a title bar holding the tabs, none draws no title bar,
    // and server (or auto) leaves the title bar to the desktop.
    QString windowDecoration() const { return m_windowDecoration; }
    bool frameless() const { return m_windowDecoration == u"client" || m_windowDecoration == u"none"; }
    // Corner radius of frameless windows in logical pixels; 0 is square.
    int windowCornerRadius() const { return m_windowCornerRadius; }
    // window-width and window-height set the first terminal's grid; the
    // window then fits it. The terminal applies both or neither.
    bool hasInitialGrid() const { return m_windowWidth > 0 && m_windowHeight > 0; }
    // Shared with the terminals, which draw their own translucent cells.
    qreal backgroundOpacity() const { return m_backgroundOpacity; }
    QColor translucentBackground() const
    {
        QColor color = background();
        color.setAlphaF(float(m_backgroundOpacity));
        return color;
    }
    // Blur behind a translucent window, as background-blur does for Monstar.
    bool backgroundBlur() const { return m_backgroundBlur; }
    bool inheritWorkingDirectory() const { return m_inheritWorkingDirectory; }
    // The terminal executable; empty selects `monstar` beside monstar-qt or
    // on PATH.
    QString terminalCommand() const { return m_terminalCommand; }

Q_SIGNALS:
    void changed();

private:
    void reset();

    QString m_path;
    QStringList m_overrides;
    QString m_tabsLocation;
    QString m_showTabBar;
    qreal m_unfocusedSplitOpacity;
    QColor m_unfocusedSplitFill;
    QColor m_splitDividerColor;
    QColor m_background;
    bool m_focusFollowsMouse;
    QString m_windowDecoration;
    int m_windowCornerRadius;
    int m_windowWidth;
    int m_windowHeight;
    qreal m_backgroundOpacity;
    bool m_backgroundBlur;
    bool m_inheritWorkingDirectory;
    QString m_terminalCommand;
};

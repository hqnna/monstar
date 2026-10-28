// Tabs and splits of Monstar terminals. Each pane is a `monstar` process
// connected to the nested compositor over its own socket; the host lays the
// panes out with one SplitTree per tab and performs the window actions the
// terminals forward through monstar_host_v1.
#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QPointer>
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

#include "HostConfig.h"
#include "SplitTree.h"

#include <QWaylandClient>
#include <QWaylandSurface>
#include <QWaylandXdgSurface>
#include <QWaylandXdgToplevel>

class MonstarCompositor;
class QProcess;
class QWindow;

struct Pane {
    int id = 0;
    int tab = 0;
    QProcess *process = nullptr;
    QPointer<QWaylandClient> client;
    QPointer<QWaylandXdgSurface> xdgSurface;
    QPointer<QWaylandXdgToplevel> toplevel;
    // Directory last reported by the terminal with an action.
    QString workingDirectory;
    QRectF rect;
    bool shown = false;
    bool focused = false;
    bool dimmed = false;
    // Last size sent in a configure event.
    QSize configured;
    // Whether the last configure marked the terminal activated.
    bool configuredActive = false;
};

// Panes in creation order; their geometry and state are QML roles.
class PaneModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by TerminalHost")
public:
    enum Role { PaneIdRole = Qt::UserRole + 1, ShellSurfaceRole, GeometryRole, ShownRole, FocusedRole, DimmedRole };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Pane *add(std::unique_ptr<Pane> pane);
    void remove(int id);
    Pane *find(int id) const;
    Pane *findClient(const QWaylandClient *client) const;
    Pane *findSurface(const QWaylandSurface *surface) const;
    const std::vector<std::unique_ptr<Pane>> &panes() const { return m_panes; }
    // Report every role of `pane` as changed.
    void refresh(const Pane *pane);

private:
    std::vector<std::unique_ptr<Pane>> m_panes;
};

// The dividers of the current tab. Rows keep their identity while the split
// set is unchanged, so a divider being dragged is not recreated.
class DividerModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by TerminalHost")
public:
    enum Role { DividerIdRole = Qt::UserRole + 1, GeometryRole, VerticalRole, AreaRole };

    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void sync(const QList<SplitTree::Divider> &dividers);

private:
    QList<SplitTree::Divider> m_dividers;
};

struct LaunchOptions {
    // Empty selects `monstar` beside this executable, then on PATH.
    QString terminal;
    // Passed to every terminal, such as --config and -o.
    QStringList commonArguments;
    // Passed to the first terminal only, such as -e and its command.
    QStringList firstArguments;
    QString workingDirectory;
};

class TerminalHost : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by main")

    Q_PROPERTY(HostConfig *config READ config CONSTANT)
    Q_PROPERTY(PaneModel *panes READ panes CONSTANT)
    Q_PROPERTY(DividerModel *dividers READ dividers CONSTANT)
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged)
    Q_PROPERTY(int currentTab READ currentTab NOTIFY tabsChanged)
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY tabsChanged)
    Q_PROPERTY(bool tabBarVisible READ tabBarVisible NOTIFY tabsChanged)
    Q_PROPERTY(qreal dividerWidth READ dividerWidth CONSTANT)
    // True until the first terminal has chosen its size from window-width
    // and window-height; the window stays hidden meanwhile.
    Q_PROPERTY(bool awaitingInitialSize READ awaitingInitialSize NOTIFY initialSizeChosen)

public:
    TerminalHost(HostConfig *config, LaunchOptions options, QObject *parent = nullptr);
    ~TerminalHost() override;

    HostConfig *config() const { return m_config; }
    PaneModel *panes() const { return m_panes; }
    DividerModel *dividers() const { return m_dividers; }
    QVariantList tabs() const;
    int currentTab() const { return m_currentTab; }
    QString windowTitle() const;
    bool tabBarVisible() const;
    bool awaitingInitialSize() const { return m_awaitingInitialSize; }
    qreal dividerWidth() const { return 1; }

    // Start the first tab once the compositor exists.
    Q_INVOKABLE void setCompositor(MonstarCompositor *compositor);
    // The size of the area panes share, in logical pixels.
    Q_INVOKABLE void setPaneArea(qreal width, qreal height);
    Q_INVOKABLE void attachToplevel(QWaylandXdgToplevel *toplevel, QWaylandXdgSurface *xdgSurface);
    Q_INVOKABLE void newTab();
    Q_INVOKABLE void activateTab(int index);
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void focusPane(int paneId);
    Q_INVOKABLE void setDividerRatio(int dividerId, qreal ratio);
    // Ask the desktop to blur behind a translucent window, following the
    // configuration from now on.
    Q_INVOKABLE void setWindow(QWindow *window);
    // The radius the window's corners are drawn with, so the blur follows
    // them; 0 for square corners.
    Q_INVOKABLE void setWindowCornerRadius(int radius);

    // Perform a Ghostty-syntax action for the terminal owning `surface`.
    void performAction(QWaylandSurface *surface, const QString &action, const QString &workingDirectory);

Q_SIGNALS:
    void tabsChanged();
    // Give keyboard focus to this pane's surface item.
    void focusRequested(int paneId);
    // The first terminal's chosen size in logical pixels, or an empty size
    // when it did not choose one in time.
    void initialSizeChosen(QSizeF paneSize);

private:
    struct Tab {
        int id;
        std::unique_ptr<SplitTree> tree;
        int focused;
    };

    Tab *findTab(int id) const;
    int tabIndex(int id) const;
    Tab *current() const;

    Pane *spawnPane(int tab, const QString &workingDirectory, const QStringList &extraArguments);
    void openTab(const QString &workingDirectory, const QStringList &extraArguments);
    void split(Pane *pane, SplitDirection direction, const QString &workingDirectory);
    void closePane(Pane *pane);
    void removePane(int paneId);
    void openWindow(const QString &workingDirectory);
    void moveTab(int tabId, int offset);
    void selectTab(int index);
    void setFocused(Tab *tab, int paneId);
    QString directoryFor(const Pane *pane, const QString &reported) const;

    void relayout();
    void configure(Pane *pane);
    void finishInitialSize(QSizeF paneSize);
    void applyWindowEffects();

    HostConfig *m_config;
    LaunchOptions m_options;
    MonstarCompositor *m_compositor = nullptr;
    QPointer<QWindow> m_window;
    bool m_awaitingInitialSize = false;
    int m_cornerRadius = 0;
    PaneModel *m_panes;
    DividerModel *m_dividers;
    std::vector<std::unique_ptr<Tab>> m_tabs;
    int m_currentTab = -1;
    QRectF m_area;
    int m_nextPaneId = 1;
    int m_nextTabId = 1;
    bool m_firstLaunched = false;
};

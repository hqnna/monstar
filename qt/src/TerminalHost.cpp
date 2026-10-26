#include "TerminalHost.h"

#include "KeyboardForwarder.h"
#include "MonstarCompositor.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>
#include <QStandardPaths>
#include <QWaylandClient>
#include <QWaylandSurface>
#include <QWaylandXdgSurface>
#include <QWaylandXdgToplevel>

#include <QWindow>

#if MONSTAR_HAVE_KWINDOWSYSTEM
#include <KWindowEffects>
#endif

#include <wayland-server-core.h>

#include <algorithm>
#include <optional>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcHost, "monstar.host")

namespace {

std::optional<SplitDirection> parseDirection(QStringView text)
{
    if (text == u"right")
        return SplitDirection::Right;
    if (text == u"down")
        return SplitDirection::Down;
    if (text == u"left")
        return SplitDirection::Left;
    if (text == u"up")
        return SplitDirection::Up;
    return std::nullopt;
}

// The shell's directory from /proc, for actions that do not report one.
QString childWorkingDirectory(qint64 pid)
{
    QFile children(QStringLiteral("/proc/%1/task/%1/children").arg(pid));
    if (!children.open(QIODevice::ReadOnly))
        return {};
    const QByteArray first = children.readAll().trimmed().split(' ').value(0);
    if (first.isEmpty())
        return {};
    return QFileInfo(QStringLiteral("/proc/%1/cwd").arg(QString::fromLatin1(first))).symLinkTarget();
}

} // namespace

// PaneModel

int PaneModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_panes.size());
}

QVariant PaneModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= int(m_panes.size()))
        return {};
    const Pane &pane = *m_panes[index.row()];
    switch (role) {
    case PaneIdRole:
        return pane.id;
    case ShellSurfaceRole:
        return QVariant::fromValue(static_cast<QObject *>(pane.xdgSurface.data()));
    case GeometryRole:
        return pane.rect;
    case ShownRole:
        return pane.shown;
    case FocusedRole:
        return pane.focused;
    case DimmedRole:
        return pane.dimmed;
    }
    return {};
}

QHash<int, QByteArray> PaneModel::roleNames() const
{
    return {
        {PaneIdRole, "paneId"},
        {ShellSurfaceRole, "shellSurface"},
        {GeometryRole, "geometry"},
        {ShownRole, "shown"},
        {FocusedRole, "focused"},
        {DimmedRole, "dimmed"},
    };
}

Pane *PaneModel::add(std::unique_ptr<Pane> pane)
{
    const int row = int(m_panes.size());
    beginInsertRows({}, row, row);
    m_panes.push_back(std::move(pane));
    endInsertRows();
    return m_panes.back().get();
}

void PaneModel::remove(int id)
{
    for (size_t row = 0; row < m_panes.size(); ++row) {
        if (m_panes[row]->id != id)
            continue;
        beginRemoveRows({}, int(row), int(row));
        m_panes.erase(m_panes.begin() + qsizetype(row));
        endRemoveRows();
        return;
    }
}

Pane *PaneModel::find(int id) const
{
    for (const auto &pane : m_panes) {
        if (pane->id == id)
            return pane.get();
    }
    return nullptr;
}

Pane *PaneModel::findClient(const QWaylandClient *client) const
{
    for (const auto &pane : m_panes) {
        if (pane->client && pane->client == client)
            return pane.get();
    }
    return nullptr;
}

Pane *PaneModel::findSurface(const QWaylandSurface *surface) const
{
    for (const auto &pane : m_panes) {
        if (pane->xdgSurface && pane->xdgSurface->surface() == surface)
            return pane.get();
    }
    return nullptr;
}

void PaneModel::refresh(const Pane *pane)
{
    for (size_t row = 0; row < m_panes.size(); ++row) {
        if (m_panes[row].get() == pane) {
            const QModelIndex index = this->index(int(row));
            Q_EMIT dataChanged(index, index);
            return;
        }
    }
}

// DividerModel

int DividerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_dividers.size());
}

QVariant DividerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_dividers.size())
        return {};
    const SplitTree::Divider &divider = m_dividers.at(index.row());
    switch (role) {
    case DividerIdRole:
        return divider.id;
    case GeometryRole:
        return divider.rect;
    case VerticalRole:
        return divider.vertical;
    case AreaRole:
        return divider.area;
    }
    return {};
}

QHash<int, QByteArray> DividerModel::roleNames() const
{
    return {
        {DividerIdRole, "dividerId"},
        {GeometryRole, "geometry"},
        {VerticalRole, "vertical"},
        {AreaRole, "area"},
    };
}

void DividerModel::sync(const QList<SplitTree::Divider> &dividers)
{
    bool sameSet = dividers.size() == m_dividers.size();
    for (qsizetype i = 0; sameSet && i < dividers.size(); ++i)
        sameSet = dividers.at(i).id == m_dividers.at(i).id;
    if (!sameSet) {
        beginResetModel();
        m_dividers = dividers;
        endResetModel();
        return;
    }
    m_dividers = dividers;
    if (!m_dividers.isEmpty())
        Q_EMIT dataChanged(index(0), index(int(m_dividers.size()) - 1));
}

// TerminalHost

TerminalHost::TerminalHost(HostConfig *config, LaunchOptions options, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_options(std::move(options))
    , m_panes(new PaneModel(this))
    , m_dividers(new DividerModel(this))
{
    connect(m_config, &HostConfig::changed, this, [this] {
        relayout();
        applyWindowEffects();
        Q_EMIT tabsChanged();
    });
    // Terminals hang up their shells when the compositor goes away. Detach
    // their processes so QProcess does not SIGKILL them first.
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
        for (const auto &pane : m_panes->panes()) {
            if (!pane->process)
                continue;
            pane->process->disconnect(this);
            pane->process->setParent(nullptr);
            pane->process = nullptr;
        }
    });
}

TerminalHost::~TerminalHost() = default;

QVariantList TerminalHost::tabs() const
{
    QVariantList result;
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const Tab &tab = *m_tabs[i];
        QString title;
        if (const Pane *pane = m_panes->find(tab.focused); pane && pane->toplevel)
            title = pane->toplevel->title();
        if (title.isEmpty())
            title = QStringLiteral("monstar");
        result.append(QVariantMap{
            {QStringLiteral("title"), title},
            {QStringLiteral("active"), int(i) == m_currentTab},
            {QStringLiteral("panes"), int(tab.tree->panes().size())},
        });
    }
    return result;
}

QString TerminalHost::windowTitle() const
{
    if (m_currentTab < 0)
        return QStringLiteral("monstar");
    return tabs().at(m_currentTab).toMap().value(QStringLiteral("title")).toString();
}

bool TerminalHost::tabBarVisible() const
{
    const QString mode = m_config->showTabBar();
    if (mode == u"always")
        return true;
    if (mode == u"never")
        return false;
    return m_tabs.size() > 1;
}

TerminalHost::Tab *TerminalHost::findTab(int id) const
{
    for (const auto &tab : m_tabs) {
        if (tab->id == id)
            return tab.get();
    }
    return nullptr;
}

int TerminalHost::tabIndex(int id) const
{
    for (size_t i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i]->id == id)
            return int(i);
    }
    return -1;
}

TerminalHost::Tab *TerminalHost::current() const
{
    if (m_currentTab < 0 || m_currentTab >= int(m_tabs.size()))
        return nullptr;
    return m_tabs[m_currentTab].get();
}

void TerminalHost::setCompositor(MonstarCompositor *compositor)
{
    Q_ASSERT(!m_compositor);
    m_compositor = compositor;
    connect(compositor, &MonstarCompositor::hostActionRequested, this, &TerminalHost::performAction);
    // Clients that disconnect, crash, or exit end their panes.
    connect(compositor, &QWaylandCompositor::surfaceAboutToBeDestroyed, this, [this](QWaylandSurface *surface) {
        if (Pane *pane = m_panes->findSurface(surface))
            removePane(pane->id);
    });
    if (!m_firstLaunched) {
        m_firstLaunched = true;
        openTab(m_options.workingDirectory, m_options.firstArguments);
    }
}

void TerminalHost::setWindow(QWindow *window)
{
    m_window = window;
    applyWindowEffects();
}

void TerminalHost::applyWindowEffects()
{
    if (!m_window)
        return;
#if MONSTAR_HAVE_KWINDOWSYSTEM
    // KWindowEffects speaks KWin's blur protocol; other desktops ignore it.
    const bool blur = m_config->backgroundBlur() && m_config->backgroundOpacity() < 1;
    KWindowEffects::enableBlurBehind(m_window, blur);
#endif
}

void TerminalHost::setPaneArea(qreal width, qreal height)
{
    const QRectF area(0, 0, std::max<qreal>(0, width), std::max<qreal>(0, height));
    if (area == m_area)
        return;
    m_area = area;
    relayout();
}

Pane *TerminalHost::spawnPane(int tab, const QString &workingDirectory, const QStringList &extraArguments)
{
    Q_ASSERT(m_compositor);
    int fds[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0) {
        qCWarning(lcHost) << "socketpair failed:" << qt_error_string(errno);
        return nullptr;
    }
    // The compositor owns fds[0] from here on, even on failure.
    wl_client *wlClient = wl_client_create(m_compositor->display(), fds[0]);
    if (!wlClient) {
        ::close(fds[0]);
        ::close(fds[1]);
        qCWarning(lcHost) << "wl_client_create failed";
        return nullptr;
    }

    if (KeyboardForwarder *keyboard = m_compositor->keyboardForwarder())
        keyboard->watchClient(wlClient);

    auto pane = std::make_unique<Pane>();
    pane->id = m_nextPaneId++;
    pane->tab = tab;
    pane->client = QWaylandClient::fromWlClient(m_compositor, wlClient);
    pane->workingDirectory = workingDirectory;

    QString program = m_options.terminal.isEmpty() ? m_config->terminalCommand() : m_options.terminal;
    if (program.isEmpty()) {
        const QString sibling = QCoreApplication::applicationDirPath() + QStringLiteral("/monstar");
        program = QFileInfo(sibling).isExecutable() ? sibling : QStringLiteral("monstar");
    }
    QStringList arguments = m_options.commonArguments;
    if (!workingDirectory.isEmpty())
        arguments << QStringLiteral("--working-directory") << workingDirectory;
    arguments << extraArguments;

    auto *process = new QProcess(this);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("WAYLAND_SOCKET"), QString::number(fds[1]));
    process->setProcessEnvironment(environment);
    process->setProcessChannelMode(QProcess::ForwardedChannels);
    process->setProgram(program);
    process->setArguments(arguments);
    // Only the child inherits its end of the connection.
    const int childFd = fds[1];
    process->setChildProcessModifier([childFd] { ::fcntl(childFd, F_SETFD, 0); });
    pane->process = process;

    const int id = pane->id;
    connect(process, &QProcess::finished, this, [this, id] { removePane(id); });
    connect(process, &QProcess::errorOccurred, this, [this, id, program](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            qCWarning(lcHost) << "failed to start" << program;
            removePane(id);
        }
    });
    connect(pane->client, &QObject::destroyed, this, [this, id] { removePane(id); });

    Pane *added = m_panes->add(std::move(pane));
    process->start();
    ::close(fds[1]);
    return added;
}

void TerminalHost::openTab(const QString &workingDirectory, const QStringList &extraArguments)
{
    auto tab = std::make_unique<Tab>();
    tab->id = m_nextTabId++;
    Pane *pane = spawnPane(tab->id, workingDirectory, extraArguments);
    if (!pane) {
        if (m_tabs.empty())
            QCoreApplication::exit(1);
        return;
    }
    tab->tree = std::make_unique<SplitTree>(pane->id);
    tab->focused = pane->id;
    // New tabs open after the current one, like Ghostty's default.
    const int index = m_currentTab < 0 ? 0 : m_currentTab + 1;
    m_tabs.insert(m_tabs.begin() + index, std::move(tab));
    selectTab(index);
}

void TerminalHost::newTab()
{
    const Tab *tab = current();
    const Pane *pane = tab ? m_panes->find(tab->focused) : nullptr;
    openTab(directoryFor(pane, {}), {});
}

void TerminalHost::split(Pane *pane, SplitDirection direction, const QString &workingDirectory)
{
    Tab *tab = findTab(pane->tab);
    Q_ASSERT(tab);
    Pane *added = spawnPane(tab->id, workingDirectory, {});
    if (!added)
        return;
    tab->tree->split(pane->id, direction, added->id);
    setFocused(tab, added->id);
    relayout();
    Q_EMIT tabsChanged();
}

void TerminalHost::closePane(Pane *pane)
{
    // Ask the terminal to close, as a window manager would. It hangs up its
    // shell, disconnects, and the pane is removed.
    if (pane->toplevel)
        pane->toplevel->sendClose();
    else if (pane->process)
        pane->process->terminate();
}

void TerminalHost::removePane(int paneId)
{
    Pane *pane = m_panes->find(paneId);
    if (!pane)
        return;
    const int tabId = pane->tab;
    if (pane->process) {
        // The process may still be exiting; let it finish on its own.
        pane->process->disconnect(this);
        connect(pane->process, &QProcess::finished, pane->process, &QObject::deleteLater);
        if (pane->process->state() == QProcess::NotRunning)
            pane->process->deleteLater();
    }
    m_panes->remove(paneId);

    Tab *tab = findTab(tabId);
    if (!tab)
        return;
    const QList<int> order = tab->tree->panes();
    if (order.size() <= 1) {
        const int index = tabIndex(tabId);
        m_tabs.erase(m_tabs.begin() + index);
        if (m_tabs.empty()) {
            m_currentTab = -1;
            Q_EMIT tabsChanged();
            QCoreApplication::quit();
            return;
        }
        if (index < m_currentTab || m_currentTab >= int(m_tabs.size()))
            --m_currentTab;
        selectTab(std::clamp(m_currentTab, 0, int(m_tabs.size()) - 1));
        return;
    }
    // Focus moves to the pane before the closed one, like Ghostty.
    const int previous = tab->tree->previous(paneId);
    tab->tree->remove(paneId);
    if (tab->focused == paneId)
        setFocused(tab, previous);
    relayout();
    Q_EMIT tabsChanged();
    if (tab == current())
        Q_EMIT focusRequested(tab->focused);
}

void TerminalHost::closeTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()))
        return;
    for (int id : m_tabs[index]->tree->panes()) {
        if (Pane *pane = m_panes->find(id))
            closePane(pane);
    }
}

void TerminalHost::activateTab(int index)
{
    if (index < 0 || index >= int(m_tabs.size()))
        return;
    selectTab(index);
}

void TerminalHost::selectTab(int index)
{
    Q_ASSERT(index >= 0 && index < int(m_tabs.size()));
    m_currentTab = index;
    relayout();
    Q_EMIT tabsChanged();
    Q_EMIT focusRequested(m_tabs[index]->focused);
}

void TerminalHost::moveTab(int tabId, int offset)
{
    const int from = tabIndex(tabId);
    if (from < 0 || m_tabs.size() < 2)
        return;
    // Ghostty wraps tabs moved past either end.
    const int count = int(m_tabs.size());
    const int to = ((from + offset) % count + count) % count;
    auto tab = std::move(m_tabs[from]);
    m_tabs.erase(m_tabs.begin() + from);
    m_tabs.insert(m_tabs.begin() + to, std::move(tab));
    if (m_currentTab == from)
        m_currentTab = to;
    Q_EMIT tabsChanged();
}

void TerminalHost::setFocused(Tab *tab, int paneId)
{
    Q_ASSERT(tab->tree->contains(paneId));
    tab->focused = paneId;
    // A zoomed split stays zoomed only while it has focus.
    if (tab->tree->zoomed() >= 0 && tab->tree->zoomed() != paneId)
        tab->tree->setZoomed(-1);
}

void TerminalHost::focusPane(int paneId)
{
    Pane *pane = m_panes->find(paneId);
    if (!pane)
        return;
    Tab *tab = findTab(pane->tab);
    if (!tab || tab->focused == paneId)
        return;
    setFocused(tab, paneId);
    relayout();
    Q_EMIT tabsChanged();
}

void TerminalHost::setDividerRatio(int dividerId, qreal ratio)
{
    Tab *tab = current();
    if (tab && tab->tree->setDividerRatio(dividerId, ratio))
        relayout();
}

QString TerminalHost::directoryFor(const Pane *pane, const QString &reported) const
{
    if (!m_config->inheritWorkingDirectory() || !pane)
        return {};
    // OSC 7 is the shell's own report; /proc covers shells without it.
    if (!reported.isEmpty())
        return reported;
    if (pane->process) {
        const QString live = childWorkingDirectory(pane->process->processId());
        if (!live.isEmpty())
            return live;
    }
    return pane->workingDirectory;
}

void TerminalHost::openWindow(const QString &workingDirectory)
{
    QStringList arguments = m_options.commonArguments;
    if (!workingDirectory.isEmpty())
        arguments << QStringLiteral("--working-directory") << workingDirectory;
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments))
        qCWarning(lcHost) << "failed to open a new window";
}

void TerminalHost::attachToplevel(QWaylandXdgToplevel *toplevel, QWaylandXdgSurface *xdgSurface)
{
    QWaylandClient *client = xdgSurface->surface()->client();
    Pane *pane = m_panes->findClient(client);
    if (!pane || pane->toplevel) {
        // Terminals open exactly one toplevel; anything else is not ours.
        qCWarning(lcHost) << "closing unexpected toplevel" << toplevel->title();
        toplevel->sendClose();
        return;
    }
    pane->xdgSurface = xdgSurface;
    pane->toplevel = toplevel;
    const int id = pane->id;
    connect(toplevel, &QWaylandXdgToplevel::titleChanged, this, &TerminalHost::tabsChanged);
    connect(xdgSurface, &QObject::destroyed, this, [this, id] { removePane(id); });
    pane->configured = {};
    configure(pane);
    m_panes->refresh(pane);
    Q_EMIT tabsChanged();
    const Tab *tab = findTab(pane->tab);
    if (tab && tab == current() && tab->focused == id)
        Q_EMIT focusRequested(id);
}

void TerminalHost::configure(Pane *pane)
{
    if (!pane->toplevel)
        return;
    const QSize size = pane->rect.size().toSize();
    if (size.isEmpty() || size == pane->configured)
        return;
    pane->configured = size;
    // Maximized: the terminal must use exactly this size and draw no
    // decorations or shadows.
    QList<QWaylandXdgToplevel::State> states{QWaylandXdgToplevel::MaximizedState};
    if (pane->focused)
        states.append(QWaylandXdgToplevel::ActivatedState);
    pane->toplevel->sendConfigure(size, states);
}

void TerminalHost::relayout()
{
    const qreal gap = dividerWidth();
    for (const auto &tab : m_tabs) {
        const bool visible = tab.get() == current();
        const QHash<int, QRectF> rects = tab->tree->layout(m_area, gap);
        const bool split = tab->tree->panes().size() > 1;
        for (int id : tab->tree->panes()) {
            Pane *pane = m_panes->find(id);
            if (!pane)
                continue;
            const auto rect = rects.constFind(id);
            const bool shown = visible && rect != rects.constEnd();
            // Hidden tabs keep their geometry so switching back is instant.
            if (rect != rects.constEnd())
                pane->rect = *rect;
            pane->shown = shown;
            pane->focused = tab->focused == id;
            pane->dimmed = split && !pane->focused && m_config->unfocusedSplitOpacity() < 1;
            configure(pane);
            m_panes->refresh(pane);
        }
    }
    const Tab *tab = current();
    m_dividers->sync(tab ? tab->tree->dividers(m_area, gap) : QList<SplitTree::Divider>{});
}

void TerminalHost::performAction(QWaylandSurface *surface, const QString &action, const QString &workingDirectory)
{
    Pane *pane = m_panes->findSurface(surface);
    if (!pane)
        return;
    Tab *tab = findTab(pane->tab);
    Q_ASSERT(tab);
    if (!workingDirectory.isEmpty())
        pane->workingDirectory = workingDirectory;

    const qsizetype colon = action.indexOf(u':');
    const QStringView name = QStringView(action).left(colon < 0 ? action.size() : colon);
    const QStringView param = colon < 0 ? QStringView() : QStringView(action).mid(colon + 1);
    const int count = int(m_tabs.size());
    const int index = tabIndex(tab->id);

    if (name == u"new_window") {
        openWindow(directoryFor(pane, workingDirectory));
    } else if (name == u"new_tab") {
        if (index != m_currentTab)
            selectTab(index);
        openTab(directoryFor(pane, workingDirectory), {});
    } else if (name == u"close_tab") {
        closeTab(index);
    } else if (name == u"close_surface") {
        closePane(pane);
    } else if (name == u"previous_tab") {
        selectTab((m_currentTab + count - 1) % count);
    } else if (name == u"next_tab") {
        selectTab((m_currentTab + 1) % count);
    } else if (name == u"last_tab") {
        selectTab(count - 1);
    } else if (name == u"goto_tab") {
        // Ghostty clamps indices past the end to the last tab.
        const int target = param.toInt();
        if (target >= 1)
            selectTab(std::min(target, count) - 1);
    } else if (name == u"move_tab") {
        moveTab(tab->id, param.toInt());
    } else if (name == u"new_split") {
        if (const auto direction = parseDirection(param))
            split(pane, *direction, directoryFor(pane, workingDirectory));
    } else if (name == u"goto_split") {
        int target = -1;
        if (param == u"next")
            target = tab->tree->next(pane->id);
        else if (param == u"previous")
            target = tab->tree->previous(pane->id);
        else if (const auto direction = parseDirection(param))
            target = tab->tree->neighbor(pane->id, *direction, m_area, dividerWidth());
        if (target >= 0 && target != pane->id) {
            setFocused(tab, target);
            relayout();
            Q_EMIT tabsChanged();
            Q_EMIT focusRequested(target);
        }
    } else if (name == u"toggle_split_zoom") {
        if (tab->tree->panes().size() > 1) {
            tab->tree->setZoomed(tab->tree->zoomed() == pane->id ? -1 : pane->id);
            relayout();
        }
    } else if (name == u"equalize_splits") {
        tab->tree->equalize();
        relayout();
    } else if (name == u"resize_split") {
        const qsizetype comma = param.indexOf(u',');
        const auto direction = parseDirection(param.left(comma));
        const int pixels = comma < 0 ? 0 : param.mid(comma + 1).toInt();
        if (direction && pixels > 0 && tab->tree->resize(pane->id, *direction, pixels, m_area, dividerWidth()))
            relayout();
    } else {
        qCDebug(lcHost) << "ignoring unsupported action" << action;
    }
}

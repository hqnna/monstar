#include "MonstarCompositor.h"

#include "KeyboardForwarder.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QWaylandKeymap>
#include <QWaylandSeat>
#include <QWaylandSurface>

#include <wayland-server-core.h>

MonstarHostExtension::MonstarHostExtension(QWaylandCompositor *compositor)
    : QWaylandCompositorExtensionTemplate(compositor)
{
}

void MonstarHostExtension::initialize()
{
    QWaylandCompositorExtensionTemplate::initialize();
    auto *compositor = static_cast<QWaylandCompositor *>(extensionContainer());
    init(compositor->display(), 1);
}

void MonstarHostExtension::monstar_host_v1_destroy(Resource *resource)
{
    wl_resource_destroy(resource->handle);
}

void MonstarHostExtension::monstar_host_v1_perform_action(Resource *, struct ::wl_resource *surface,
                                                          const QString &action,
                                                          const QString &working_directory)
{
    Q_EMIT actionRequested(QWaylandSurface::fromResource(surface), action, working_directory);
}

MonstarCompositor::MonstarCompositor(QObject *parent)
    : QWaylandQuickCompositor(parent)
{
    // Terminals connect over private sockets handed to each child, so the
    // listening socket is never advertised. It still needs a name; keep it
    // distinct from the desktop's.
    setSocketName(QByteArrayLiteral("monstar-qt-") + QByteArray::number(QCoreApplication::applicationPid()));
    setRetainedSelectionEnabled(true);
}

void MonstarCompositor::create()
{
    QWaylandQuickCompositor::create();

    m_host = new MonstarHostExtension(this);
    if (!m_host->isInitialized())
        m_host->initialize();
    connect(m_host, &MonstarHostExtension::actionRequested, this, &MonstarCompositor::hostActionRequested);

    applyKeymap();
    m_keyboard = new KeyboardForwarder(this, this);

    QClipboard *clipboard = QGuiApplication::clipboard();
    connect(clipboard, &QClipboard::dataChanged, this, [this, clipboard] {
        // Our own copies of a terminal selection need not be offered back.
        if (!clipboard->ownsClipboard())
            offerHostClipboard();
    });
    offerHostClipboard();
}

void MonstarCompositor::applyKeymap()
{
    // Fallback when the desktop keyboard cannot be forwarded: Qt translates
    // keys through the standard XKB environment, defaulting to "us".
    QWaylandSeat *seat = defaultSeat();
    if (!seat || !seat->keymap())
        return;
    QWaylandKeymap *keymap = seat->keymap();
    const auto apply = [](const char *name, auto setter) {
        const QString value = qEnvironmentVariable(name);
        if (!value.isEmpty())
            setter(value);
    };
    apply("XKB_DEFAULT_RULES", [keymap](const QString &v) { keymap->setRules(v); });
    apply("XKB_DEFAULT_MODEL", [keymap](const QString &v) { keymap->setModel(v); });
    apply("XKB_DEFAULT_LAYOUT", [keymap](const QString &v) { keymap->setLayout(v); });
    apply("XKB_DEFAULT_VARIANT", [keymap](const QString &v) { keymap->setVariant(v); });
    apply("XKB_DEFAULT_OPTIONS", [keymap](const QString &v) { keymap->setOptions(v); });
}

void MonstarCompositor::offerHostClipboard()
{
    const QMimeData *data = QGuiApplication::clipboard()->mimeData();
    if (data)
        overrideSelection(data);
}

void MonstarCompositor::retainedSelectionReceived(QMimeData *mimeData)
{
    // A terminal copied: publish it on the desktop clipboard. QClipboard
    // takes ownership of the copy.
    auto *copy = new QMimeData;
    for (const QString &format : mimeData->formats())
        copy->setData(format, mimeData->data(format));
    QGuiApplication::clipboard()->setMimeData(copy);
}

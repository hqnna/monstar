#include "KeyboardForwarder.h"

#include <QAbstractEventDispatcher>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>
#include <QWaylandClient>
#include <QWaylandCompositor>
#include <QWaylandSeat>
#include <QWaylandSurface>

// Client side of the desktop connection only. The server side uses
// wl_resource_post_event so the two protocol headers never meet.
#include <wayland-client-protocol.h>
#include <wayland-server-core.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcKeyboard, "monstar.keyboard")

namespace {

// wl_keyboard event opcodes, from wayland.xml.
enum : uint32_t {
    keymapEvent = 0,
    keyEvent = 3,
    modifiersEvent = 4,
    repeatInfoEvent = 5,
};
constexpr int repeatInfoSinceVersion = 4;

struct ResourceListener {
    wl_listener destroy;
    KeyboardForwarder *forwarder;
    wl_resource *resource;
};

struct ClientListener {
    wl_listener created;
    wl_listener destroyed;
    KeyboardForwarder *forwarder;
    wl_client *client;
};

template <typename T>
T *containerOf(wl_listener *listener, std::size_t offset)
{
    return reinterpret_cast<T *>(reinterpret_cast<char *>(listener) - offset);
}

void onResourceDestroyed(wl_listener *listener, void *)
{
    auto *state = containerOf<ResourceListener>(listener, offsetof(ResourceListener, destroy));
    state->forwarder->removeResource(state->resource);
    wl_list_remove(&state->destroy.link);
    delete state;
}

void onResourceCreated(wl_listener *listener, void *data)
{
    auto *state = containerOf<ClientListener>(listener, offsetof(ClientListener, created));
    auto *resource = static_cast<wl_resource *>(data);
    if (std::strcmp(wl_resource_get_class(resource), "wl_keyboard") != 0)
        return;
    auto *watch = new ResourceListener{};
    watch->forwarder = state->forwarder;
    watch->resource = resource;
    watch->destroy.notify = onResourceDestroyed;
    wl_resource_add_destroy_listener(resource, &watch->destroy);
    state->forwarder->addResource(resource);
}

void onClientDestroyed(wl_listener *listener, void *)
{
    auto *state = containerOf<ClientListener>(listener, offsetof(ClientListener, destroyed));
    wl_list_remove(&state->created.link);
    wl_list_remove(&state->destroyed.link);
    delete state;
}

const wl_keyboard_listener hostListener = {
    .keymap = [](void *data, wl_keyboard *, uint32_t format, int32_t fd, uint32_t size) {
        static_cast<KeyboardForwarder *>(data)->hostKeymap(format, fd, size);
    },
    .enter = [](void *data, wl_keyboard *, uint32_t, wl_surface *, wl_array *) {
        static_cast<KeyboardForwarder *>(data)->hostFocus(true);
    },
    .leave = [](void *data, wl_keyboard *, uint32_t, wl_surface *) {
        static_cast<KeyboardForwarder *>(data)->hostFocus(false);
    },
    .key = [](void *data, wl_keyboard *, uint32_t, uint32_t time, uint32_t key, uint32_t state) {
        static_cast<KeyboardForwarder *>(data)->hostKey(time, key, state);
    },
    .modifiers = [](void *data, wl_keyboard *, uint32_t, uint32_t depressed, uint32_t latched,
                    uint32_t locked, uint32_t group) {
        static_cast<KeyboardForwarder *>(data)->hostModifiers(depressed, latched, locked, group);
    },
    .repeat_info = [](void *data, wl_keyboard *, int32_t rate, int32_t delay) {
        static_cast<KeyboardForwarder *>(data)->hostRepeatInfo(rate, delay);
    },
};

const wl_seat_listener seatListener = {
    .capabilities = [](void *data, wl_seat *, uint32_t capabilities) {
        static_cast<KeyboardForwarder *>(data)->hostCapabilities(capabilities);
    },
    .name = [](void *, wl_seat *, const char *) {},
};

const wl_registry_listener registryListener = {
    .global = [](void *data, wl_registry *, uint32_t name, const char *interface, uint32_t version) {
        if (std::strcmp(interface, wl_seat_interface.name) == 0)
            static_cast<KeyboardForwarder *>(data)->hostSeat(name, version);
    },
    .global_remove = [](void *, wl_registry *, uint32_t) {},
};

// Drop Qt's translated key events for terminals; the forwarder sends the
// desktop's raw keys instead.
class KeyEventFilter : public QObject
{
public:
    KeyEventFilter(KeyboardForwarder *forwarder)
        : QObject(forwarder)
        , m_forwarder(forwarder)
    {
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        const QEvent::Type type = event->type();
        if ((type == QEvent::KeyPress || type == QEvent::KeyRelease || type == QEvent::ShortcutOverride)
            && m_forwarder->active() && watched->inherits("QWaylandQuickItem")) {
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    KeyboardForwarder *m_forwarder;
};

} // namespace

KeyboardForwarder::KeyboardForwarder(QWaylandCompositor *compositor, QObject *parent)
    : QObject(parent)
    , m_compositor(compositor)
{
    auto *wayland = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!wayland || !wayland->display()) {
        qCWarning(lcKeyboard) << "not running on Wayland; using Qt key translation";
        return;
    }
    // Our own registry and seat on a queue of our own: Qt neither dispatches
    // proxies it did not create nor lets us listen to its seat, whose
    // keyboard capability comes and goes with devices. The queue is drained
    // on every main-loop wakeup; the desktop sends our events alongside
    // Qt's, which wake the loop.
    m_display = wayland->display();
    m_queue = wl_display_create_queue(m_display);
    auto *displayWrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(m_display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(displayWrapper), m_queue);
    m_registry = wl_display_get_registry(displayWrapper);
    wl_proxy_wrapper_destroy(displayWrapper);
    wl_registry_add_listener(m_registry, &registryListener, this);
    // Bind the seat, then receive its capabilities and the keymap before
    // the first terminal connects.
    wl_display_roundtrip_queue(m_display, m_queue);
    wl_display_roundtrip_queue(m_display, m_queue);

    auto *dispatcher = QAbstractEventDispatcher::instance();
    connect(dispatcher, &QAbstractEventDispatcher::awake, this, &KeyboardForwarder::dispatchHostEvents);
    connect(dispatcher, &QAbstractEventDispatcher::aboutToBlock, this, &KeyboardForwarder::dispatchHostEvents);
    qGuiApp->installEventFilter(new KeyEventFilter(this));

    // Qt re-sends enter and its own modifiers when focus changes; follow
    // them with the desktop's.
    connect(compositor->defaultSeat(), &QWaylandSeat::keyboardFocusChanged, this, [this] {
        QTimer::singleShot(0, this, [this] {
            for (wl_resource *resource : focusedResources())
                sendModifiers(resource);
        });
    });
}

void KeyboardForwarder::hostSeat(uint32_t name, uint32_t version)
{
    if (m_seat)
        return;
    m_seat = static_cast<wl_seat *>(wl_registry_bind(m_registry, name, &wl_seat_interface, std::min<uint32_t>(version, 5)));
    wl_seat_add_listener(m_seat, &seatListener, this);
}

void KeyboardForwarder::hostCapabilities(uint32_t capabilities)
{
    const bool keyboard = capabilities & WL_SEAT_CAPABILITY_KEYBOARD;
    if (keyboard && !m_hostKeyboard) {
        m_hostKeyboard = wl_seat_get_keyboard(m_seat);
        wl_keyboard_add_listener(m_hostKeyboard, &hostListener, this);
    } else if (!keyboard && m_hostKeyboard) {
        if (wl_keyboard_get_version(m_hostKeyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
            wl_keyboard_release(m_hostKeyboard);
        else
            wl_keyboard_destroy(m_hostKeyboard);
        m_hostKeyboard = nullptr;
    }
}

KeyboardForwarder::~KeyboardForwarder()
{
    if (m_hostKeyboard)
        wl_keyboard_destroy(m_hostKeyboard);
    if (m_seat)
        wl_seat_destroy(m_seat);
    if (m_registry)
        wl_registry_destroy(m_registry);
    if (m_queue)
        wl_event_queue_destroy(m_queue);
}

void KeyboardForwarder::dispatchHostEvents()
{
    if (m_queue)
        wl_display_dispatch_queue_pending(m_display, m_queue);
}

void KeyboardForwarder::watchClient(wl_client *client)
{
    auto *state = new ClientListener{};
    state->forwarder = this;
    state->client = client;
    state->created.notify = onResourceCreated;
    state->destroyed.notify = onClientDestroyed;
    wl_client_add_resource_created_listener(client, &state->created);
    wl_client_add_destroy_listener(client, &state->destroyed);
}

void KeyboardForwarder::addResource(wl_resource *resource)
{
    m_resources.append(resource);
    // Qt sends its own keymap while handling get_keyboard, after this
    // creation callback. Replace it once that request has been handled.
    QTimer::singleShot(0, this, [this, resource] {
        if (m_resources.contains(resource))
            sendState(resource);
    });
}

void KeyboardForwarder::removeResource(wl_resource *resource)
{
    m_resources.removeAll(resource);
}

void KeyboardForwarder::sendState(wl_resource *resource)
{
    if (m_keymap.isEmpty())
        return;
    // Each keymap event needs its own sealed copy the client can map.
    const int fd = memfd_create("monstar-qt-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0)
        return;
    if (::write(fd, m_keymap.constData(), size_t(m_keymap.size())) != m_keymap.size()) {
        ::close(fd);
        return;
    }
    wl_resource_post_event(resource, keymapEvent, m_keymapFormat, fd, uint32_t(m_keymap.size()));
    ::close(fd);
    if (wl_resource_get_version(resource) >= repeatInfoSinceVersion)
        wl_resource_post_event(resource, repeatInfoEvent, m_repeatRate, m_repeatDelay);
    sendModifiers(resource);
}

void KeyboardForwarder::sendModifiers(wl_resource *resource)
{
    wl_resource_post_event(resource, modifiersEvent, nextSerial(), m_mods[0], m_mods[1], m_mods[2], m_mods[3]);
}

uint32_t KeyboardForwarder::nextSerial() const
{
    return wl_display_next_serial(m_compositor->display());
}

QList<wl_resource *> KeyboardForwarder::focusedResources() const
{
    QList<wl_resource *> result;
    QWaylandSurface *focus = m_compositor->defaultSeat()->keyboardFocus();
    if (!focus || !focus->client())
        return result;
    wl_client *client = focus->client()->client();
    for (wl_resource *resource : m_resources) {
        if (wl_resource_get_client(resource) == client)
            result.append(resource);
    }
    return result;
}

void KeyboardForwarder::hostFocus(bool focused)
{
    QWaylandSeat *seat = m_compositor->defaultSeat();
    if (!focused) {
        // Keys held while focus leaves are never released to us. Take
        // keyboard focus from the terminal too, as a desktop compositor
        // would: it stops key repeat, shows an unfocused cursor, and sends
        // focus-out reports to programs that asked for them.
        m_restoreFocus = seat->keyboardFocus();
        if (m_restoreFocus)
            seat->setKeyboardFocus(nullptr);
        return;
    }
    // Focus may have moved to another pane while the window was inactive.
    if (m_restoreFocus && !seat->keyboardFocus())
        seat->setKeyboardFocus(m_restoreFocus);
    m_restoreFocus = nullptr;
}

void KeyboardForwarder::hostKeymap(uint32_t format, int fd, uint32_t size)
{
    void *map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
    ::close(fd);
    if (map == MAP_FAILED) {
        qCWarning(lcKeyboard) << "cannot map the desktop keymap";
        return;
    }
    // `size` includes the terminating NUL of the text format.
    m_keymap = QByteArray(static_cast<const char *>(map), qsizetype(size));
    munmap(map, size);
    m_keymapFormat = format;
    qCDebug(lcKeyboard) << "desktop keymap" << size << "bytes," << m_resources.size() << "terminal keyboards";
    for (wl_resource *resource : std::as_const(m_resources))
        sendState(resource);
}

void KeyboardForwarder::hostKey(uint32_t time, uint32_t key, uint32_t state)
{
    const uint32_t serial = nextSerial();
    for (wl_resource *resource : focusedResources())
        wl_resource_post_event(resource, keyEvent, serial, time, key, state);
    wl_display_flush_clients(m_compositor->display());
}

void KeyboardForwarder::hostModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
    m_mods[0] = depressed;
    m_mods[1] = latched;
    m_mods[2] = locked;
    m_mods[3] = group;
    for (wl_resource *resource : focusedResources())
        sendModifiers(resource);
    wl_display_flush_clients(m_compositor->display());
}

void KeyboardForwarder::hostRepeatInfo(int32_t rate, int32_t delay)
{
    m_repeatRate = rate;
    m_repeatDelay = delay;
    for (wl_resource *resource : std::as_const(m_resources)) {
        if (wl_resource_get_version(resource) >= repeatInfoSinceVersion)
            wl_resource_post_event(resource, repeatInfoEvent, rate, delay);
    }
}

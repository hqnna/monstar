// Forwards the desktop's keyboard to terminals verbatim. Qt's compositor
// would translate Qt key events through a keymap compiled from XKB names,
// which loses the desktop layout, groups, and custom or virtual keymaps.
// Instead this binds its own wl_keyboard on monstar-qt's desktop seat and
// replays its keymap, keys, modifiers, and repeat settings to the terminal
// that has keyboard focus. Qt still decides focus and sends enter/leave.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QWaylandSurface>

struct wl_client;
struct wl_display;
struct wl_event_queue;
struct wl_keyboard;
struct wl_registry;
struct wl_seat;
struct wl_surface;
struct wl_listener;
struct wl_resource;
class QWaylandCompositor;

class KeyboardForwarder : public QObject
{
    Q_OBJECT
public:
    explicit KeyboardForwarder(QWaylandCompositor *compositor, QObject *parent = nullptr);
    ~KeyboardForwarder() override;

    // True once the desktop keymap arrived; Qt key events must then be
    // dropped so keys are not delivered twice.
    bool active() const { return m_hostKeyboard && !m_keymap.isEmpty(); }

    // Track the wl_keyboard objects `client` creates.
    void watchClient(wl_client *client);

    // Callbacks from the desktop's registry, seat, and keyboard.
    void hostSeat(uint32_t name, uint32_t version);
    void hostCapabilities(uint32_t capabilities);
    void hostEnter(wl_surface *surface);
    void hostLeave(wl_surface *surface);

    // Whether the desktop's keyboard focus is in this window. Keyboard
    // focus inside the window is unaffected; this only reports activation.
    bool windowActive() const { return m_windowActive; }
    void hostKeymap(uint32_t format, int fd, uint32_t size);
    void hostKey(uint32_t time, uint32_t key, uint32_t state);
    void hostModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    void hostRepeatInfo(int32_t rate, int32_t delay);

    // Called for each new client wl_keyboard resource.
    void addResource(wl_resource *resource);
    void removeResource(wl_resource *resource);

Q_SIGNALS:
    // A key arrived while no terminal had keyboard focus.
    void focusNeeded();
    void windowActiveChanged();

private:
    void setWindowActive(bool active);
    void dispatchHostEvents();
    QList<wl_resource *> focusedResources() const;
    void sendState(wl_resource *resource);
    void sendModifiers(wl_resource *resource);
    uint32_t nextSerial() const;

    QWaylandCompositor *m_compositor;
    wl_display *m_display = nullptr;
    wl_event_queue *m_queue = nullptr;
    wl_registry *m_registry = nullptr;
    wl_seat *m_seat = nullptr;
    wl_keyboard *m_hostKeyboard = nullptr;
    // The keymap text, including its terminating NUL.
    QByteArray m_keymap;
    uint32_t m_keymapFormat = 0;
    uint32_t m_mods[4] = {};
    int32_t m_repeatRate = 25;
    int32_t m_repeatDelay = 600;
    QList<wl_resource *> m_resources;
    // Keys pressed in the window and not yet released.
    QSet<uint32_t> m_heldKeys;
    wl_surface *m_hostFocus = nullptr;
    bool m_windowActive = true;
    QHash<wl_client *, wl_listener *> m_clientListeners;
};

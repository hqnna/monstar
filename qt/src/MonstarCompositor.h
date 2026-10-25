// The nested compositor terminals connect to. Beyond Qt's Quick compositor
// it advertises monstar_host_v1 for tab and split actions, follows the host
// keyboard layout, and bridges the clipboard with the host desktop.
#pragma once

#include <QWaylandQuickCompositor>
#include <QWaylandSurface>
#include <QtQml/qqmllist.h>
#include <QtQml/qqmlregistration.h>

#include "qwayland-server-monstar-host-v1.h"

#include <QWaylandCompositorExtensionTemplate>

class KeyboardForwarder;


class MonstarHostExtension : public QWaylandCompositorExtensionTemplate<MonstarHostExtension>,
                             public QtWaylandServer::monstar_host_v1
{
    Q_OBJECT
public:
    explicit MonstarHostExtension(QWaylandCompositor *compositor);
    void initialize() override;

Q_SIGNALS:
    void actionRequested(QWaylandSurface *surface, const QString &action, const QString &workingDirectory);

protected:
    void monstar_host_v1_destroy(Resource *resource) override;
    void monstar_host_v1_perform_action(Resource *resource, struct ::wl_resource *surface,
                                        const QString &action, const QString &working_directory) override;
};

class MonstarCompositor : public QWaylandQuickCompositor
{
    Q_OBJECT
    QML_ELEMENT
    // Like WaylandCompositor, children such as WaylandOutput and XdgShell
    // are declared inside the compositor.
    Q_PROPERTY(QQmlListProperty<QObject> data READ data DESIGNABLE false)
    Q_CLASSINFO("DefaultProperty", "data")
public:
    explicit MonstarCompositor(QObject *parent = nullptr);
    void create() override;
    QQmlListProperty<QObject> data() { return QQmlListProperty<QObject>(this, &m_data); }
    // Null until create().
    KeyboardForwarder *keyboardForwarder() const { return m_keyboard; }

Q_SIGNALS:
    // A terminal asked for a window action; see monstar-host-v1.xml.
    void hostActionRequested(QWaylandSurface *surface, const QString &action, const QString &workingDirectory);

protected:
    void retainedSelectionReceived(QMimeData *mimeData) override;

private:
    void applyKeymap();
    void offerHostClipboard();

    MonstarHostExtension *m_host = nullptr;
    KeyboardForwarder *m_keyboard = nullptr;
    QList<QObject *> m_data;
};

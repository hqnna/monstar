#include "HostConfig.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>
#include <optional>

Q_LOGGING_CATEGORY(lcConfig, "monstar.config")

namespace {

QString defaultPath()
{
    // Match Monstar: XDG_CONFIG_HOME must be absolute to be used.
    const QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (QDir::isAbsolutePath(base))
        return base + QStringLiteral("/monstar/config");
    return QDir::homePath() + QStringLiteral("/.config/monstar/config");
}

// Monstar colors are six hex digits with an optional leading '#'.
QColor parseColor(const QString &value)
{
    QString hex = value.startsWith(u'#') ? value.mid(1) : value;
    if (hex.size() != 6)
        return {};
    bool ok = false;
    const uint rgb = hex.toUInt(&ok, 16);
    return ok ? QColor::fromRgb(rgb) : QColor();
}

std::optional<bool> parseBool(const QString &value)
{
    if (value == u"true")
        return true;
    if (value == u"false")
        return false;
    return std::nullopt;
}

} // namespace

HostConfig::HostConfig(const QString &path, const QStringList &overrides, QObject *parent)
    : QObject(parent)
    , m_path(path.isEmpty() ? defaultPath() : path)
    , m_overrides(overrides)
{
    reset();
    reload();
}

void HostConfig::reset()
{
    m_tabsLocation = QStringLiteral("top");
    m_showTabBar = QStringLiteral("auto");
    // Ghostty's defaults.
    m_unfocusedSplitOpacity = 0.7;
    m_unfocusedSplitFill = QColor();
    m_splitDividerColor = QColor(0x3a, 0x3a, 0x3a);
    m_background = QColor();
    m_focusFollowsMouse = false;
    m_inheritWorkingDirectory = true;
    m_terminalCommand.clear();
}

void HostConfig::reload()
{
    reset();
    QFile file(m_path);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text))
        parse(QString::fromUtf8(file.readAll()));
    for (const QString &entry : std::as_const(m_overrides)) {
        const qsizetype eq = entry.indexOf(u'=');
        if (eq <= 0 || !set(entry.left(eq).trimmed(), entry.mid(eq + 1).trimmed()))
            qCWarning(lcConfig) << "invalid override" << entry;
    }
    Q_EMIT changed();
}

void HostConfig::parse(const QString &text)
{
    const QStringList lines = text.split(u'\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;
        const qsizetype eq = line.indexOf(u'=');
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (value.isEmpty())
            continue;
        if (!set(key, value))
            qCWarning(lcConfig).nospace() << "line " << i + 1 << ": invalid value for '" << key << "': " << value;
    }
}

bool HostConfig::set(const QString &key, const QString &value)
{
    if (key == u"qt-tabs-location") {
        static const QStringList locations{QStringLiteral("top"), QStringLiteral("bottom"),
                                           QStringLiteral("left"), QStringLiteral("right")};
        if (!locations.contains(value))
            return false;
        m_tabsLocation = value;
    } else if (key == u"window-show-tab-bar") {
        static const QStringList modes{QStringLiteral("always"), QStringLiteral("auto"), QStringLiteral("never")};
        if (!modes.contains(value))
            return false;
        m_showTabBar = value;
    } else if (key == u"unfocused-split-opacity") {
        bool ok = false;
        const qreal opacity = value.toDouble(&ok);
        if (!ok || !std::isfinite(opacity))
            return false;
        // Ghostty clamps to this range so unfocused splits stay visible.
        m_unfocusedSplitOpacity = std::clamp(opacity, 0.15, 1.0);
    } else if (key == u"unfocused-split-fill") {
        const QColor color = parseColor(value);
        if (!color.isValid())
            return false;
        m_unfocusedSplitFill = color;
    } else if (key == u"split-divider-color") {
        const QColor color = parseColor(value);
        if (!color.isValid())
            return false;
        m_splitDividerColor = color;
    } else if (key == u"background") {
        const QColor color = parseColor(value);
        if (!color.isValid())
            return false;
        m_background = color;
    } else if (key == u"focus-follows-mouse") {
        const auto enabled = parseBool(value);
        if (!enabled)
            return false;
        m_focusFollowsMouse = *enabled;
    } else if (key == u"window-inherit-working-directory") {
        const auto enabled = parseBool(value);
        if (!enabled)
            return false;
        m_inheritWorkingDirectory = *enabled;
    } else if (key == u"qt-terminal-command") {
        m_terminalCommand = value;
    }
    return true;
}

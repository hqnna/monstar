// monstar-qt: Monstar terminals in a Qt window with tabs and splits.
//
// usage: monstar-qt [--config PATH] [-o key=value]... [--working-directory DIR]
//                   [-e command [args...] | -- command...]

#include "HostConfig.h"
#include "TerminalHost.h"

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSocketNotifier>

#include <csignal>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int signalPipe[2] = {-1, -1};

void onReloadSignal(int)
{
    const char byte = 1;
    [[maybe_unused]] const ssize_t written = ::write(signalPipe[1], &byte, 1);
}

void printUsage()
{
    std::fputs("usage: monstar-qt [options]\n"
               "       monstar-qt [options] -e command [args...]\n"
               "       monstar-qt [options] -- command...\n"
               "\n"
               "Options:\n"
               "  -h, --help                   Show this help\n"
               "      --working-directory DIR  Start the first terminal in DIR\n"
               "      --config PATH            Load an alternate config file\n"
               "      --terminal PATH          Run this monstar executable\n"
               "  -o key=value                 Override a config key\n"
               "  -e command [args...]         Run command directly in the first terminal\n"
               "  -- command...                Run command through /bin/sh -c\n",
               stdout);
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("monstar-qt"));
    app.setDesktopFileName(QStringLiteral("dev.rockorager.monstar"));
    app.setWindowIcon(QIcon(QStringLiteral(":/monstar/dev.rockorager.monstar.svg")));

    QString configPath;
    QStringList overrides;
    LaunchOptions options;
    const QStringList args = app.arguments();
    for (qsizetype i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        const auto value = [&]() -> QString {
            if (i + 1 >= args.size()) {
                std::fprintf(stderr, "monstar-qt: %s needs a value\n", qPrintable(arg));
                std::exit(2);
            }
            return args.at(++i);
        };
        if (arg == u"-h" || arg == u"--help") {
            printUsage();
            return 0;
        } else if (arg == u"--config") {
            configPath = value();
            options.commonArguments << QStringLiteral("--config") << configPath;
        } else if (arg == u"-o") {
            const QString entry = value();
            overrides << entry;
            options.commonArguments << QStringLiteral("-o") << entry;
        } else if (arg == u"--working-directory") {
            options.workingDirectory = QDir(value()).absolutePath();
        } else if (arg == u"--terminal") {
            options.terminal = value();
        } else if (arg == u"-e" || arg == u"--") {
            options.firstArguments = args.mid(i);
            break;
        } else {
            std::fprintf(stderr, "monstar-qt: invalid command line\nTry 'monstar-qt --help' for usage.\n");
            return 2;
        }
    }

    HostConfig config(configPath, overrides);
    TerminalHost host(&config, options);
    // Translucency needs an alpha channel from the start; changing
    // background-opacity from 1 takes effect in new windows.
    if (config.backgroundOpacity() < 1)
        QQuickWindow::setDefaultAlphaBuffer(true);

    // `pkill -USR1 monstar` reloads Monstar's configuration, and matches
    // this process too: reload the host settings instead of terminating.
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, signalPipe) == 0) {
        auto *notifier = new QSocketNotifier(signalPipe[0], QSocketNotifier::Read, &app);
        QObject::connect(notifier, &QSocketNotifier::activated, &config, [&config] {
            char buffer[16];
            [[maybe_unused]] const ssize_t ignored = ::read(signalPipe[0], buffer, sizeof buffer);
            config.reload();
        });
        struct sigaction action = {};
        action.sa_handler = onReloadSignal;
        action.sa_flags = SA_RESTART;
        sigemptyset(&action.sa_mask);
        sigaction(SIGUSR1, &action, nullptr);
    } else {
        std::signal(SIGUSR1, SIG_IGN);
    }

    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("host"), QVariant::fromValue(&host)}});
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("Monstar.Host", "Main");
    return app.exec();
}

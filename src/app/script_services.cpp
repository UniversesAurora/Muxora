// SPDX-License-Identifier: MIT
#include "script_services.hpp"
#include <window_system/window_manager.hpp>
#include <QApplication>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cmath>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <mmsystem.h>
#endif

namespace muxora {
namespace {
using Reply = lua_runtime::ScriptResult;
Reply failure(QString message) { return {false, {}, std::move(message)}; }
Reply success(QVariant value = true) { return {true, {std::move(value)}, {}}; }

constexpr int maximumNotifications = 16;
constexpr int maximumLaunches = 8;
constexpr int maximumText = 8192;

class DetachedLauncher final : public QObject {
    Q_OBJECT
  public:
    explicit DetachedLauncher(std::shared_ptr<std::atomic<quint64>> generation)
        : generation_(std::move(generation)) {}
  public slots:
    void launch(quint64 id, quint64 generation, QString program, QStringList arguments, QString directory) {
        if (generation_->load() != generation)
            return;
        // QProcess is only a launch descriptor here. startDetached transfers
        // process ownership to the OS, and output cannot fill an unread pipe.
        QProcess process;
        process.setProgram(std::move(program));
        process.setArguments(std::move(arguments));
        process.setWorkingDirectory(std::move(directory));
        process.setStandardInputFile(QProcess::nullDevice());
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        qint64 pid = 0;
        const bool ok = process.startDetached(&pid);
        emit completed(id, generation, ok ? pid : 0, ok ? QString() : process.errorString());
    }
  signals:
    void completed(quint64 id, quint64 generation, qint64 pid, QString error);

  private:
    std::shared_ptr<std::atomic<quint64>> generation_;
};

} // namespace

struct AppScriptServices::Private {
    QPointer<window_system::WindowManager> manager;
    QSet<QMessageBox *> notifications;
    QSet<quint64> pending;
    quint64 nextLaunch = 0;
    std::shared_ptr<std::atomic<quint64>> generation = std::make_shared<std::atomic<quint64>>(1);
    QThread *thread = nullptr;
    DetachedLauncher *launcher = nullptr;
};

AppScriptServices::AppScriptServices(window_system::WindowManager *manager, QObject *parent)
    : QObject(parent), d(std::make_unique<Private>()) {
    d->manager = manager;
}

AppScriptServices::~AppScriptServices() {
    reset();
    if (d->thread) {
        disconnect(d->launcher, nullptr, this, nullptr);
        d->thread->quit();
        // Wait only for an in-flight CreateProcess/posix_spawn call, never for
        // the launched application's lifetime. No user program is killed.
        d->thread->wait();
    }
}

void AppScriptServices::reset() {
    ++*d->generation;
    d->pending.clear();
    const auto notifications = d->notifications;
    for (auto *notification : notifications)
        delete notification;
    d->notifications.clear();
#ifdef Q_OS_WIN
    PlaySoundW(nullptr, nullptr, 0);
#endif
}
int AppScriptServices::notificationCount() const { return d->notifications.size(); }
int AppScriptServices::pendingLaunchCount() const { return d->pending.size(); }

lua_runtime::ScriptResult AppScriptServices::invoke(QString method, QVariantList args, QVariantMap context) {
    if (!d->manager)
        return failure(QCoreApplication::translate("Muxora", "脚本服务已关闭。"));
    if (method == QStringLiteral("ui.notify")) {
        const bool descriptor = args.value(0).metaType().id() == QMetaType::QVariantMap;
        const auto options = descriptor ? args[0].toMap() : QVariantMap{};
        const auto message = descriptor ? options.value(QStringLiteral("text")) : args.value(0);
        const auto title = options.value(QStringLiteral("title"), QStringLiteral("Muxora"));
        if (message.metaType().id() != QMetaType::QString || title.metaType().id() != QMetaType::QString ||
            message.toString().size() > maximumText || title.toString().size() > 256)
            return failure(QCoreApplication::translate(
                "Muxora", "通知需要文本；正文最多 8192 个字符，标题最多 256 个字符。"));
        const auto duration = options.value(QStringLiteral("timeout_ms"), 5000);
        bool numeric = false;
        const double milliseconds = duration.toDouble(&numeric);
        if (!numeric || !std::isfinite(milliseconds) || milliseconds < 0 || milliseconds > 600000 ||
            std::floor(milliseconds) != milliseconds)
            return failure(
                QCoreApplication::translate("Muxora", "通知时长必须是 0 到 600000 之间的毫秒数。"));
        if (d->notifications.size() >= maximumNotifications)
            return failure(QCoreApplication::translate("Muxora", "最多同时显示 16 条脚本通知。"));
        const auto source = QUuid(context.value(QStringLiteral("window")).toString());
        auto *parent = d->manager->windowWidget(source);
        auto *box = new QMessageBox(QMessageBox::Information, title.toString(), message.toString(),
                                    QMessageBox::Ok, parent);
        box->setObjectName(QStringLiteral("LuaNotification"));
        box->setTextFormat(Qt::PlainText);
        box->setWindowModality(Qt::NonModal);
        box->setAttribute(Qt::WA_DeleteOnClose);
        window_system::applyTheme(box, d->manager->theme());
        d->notifications.insert(box);
        connect(box, &QObject::destroyed, this, [this, box] { d->notifications.remove(box); });
        if (milliseconds > 0)
            QTimer::singleShot(int(milliseconds), box, &QMessageBox::close);
        box->show();
        return success();
    }
    if (method == QStringLiteral("app.launch")) {
        const auto options = args.value(0).toMap();
        const auto programValue =
            options.isEmpty() ? args.value(0) : options.value(QStringLiteral("program"));
        auto argumentsValue = options.isEmpty() ? args.value(1, QVariantList{})
                                                : options.value(QStringLiteral("args"), QVariantList{});
        const auto directoryValue = options.value(QStringLiteral("cwd"), QString());
        if (programValue.metaType().id() != QMetaType::QString ||
            programValue.toString().trimmed().isEmpty() || programValue.toString().size() > 32768 ||
            programValue.toString().contains(QChar::Null) ||
            directoryValue.metaType().id() != QMetaType::QString ||
            directoryValue.toString().contains(QChar::Null))
            return failure(
                QCoreApplication::translate("Muxora", "启动程序需要有效的 program 和 cwd 字符串。"));
        if (argumentsValue.metaType().id() == QMetaType::QVariantMap && argumentsValue.toMap().isEmpty())
            argumentsValue = QVariantList{};
        if (argumentsValue.metaType().id() != QMetaType::QVariantList &&
            argumentsValue.metaType().id() != QMetaType::QStringList)
            return failure(QCoreApplication::translate("Muxora", "程序参数 args 必须是字符串数组。"));
        QStringList arguments;
        qsizetype bytes = 0;
        for (const auto &argument : argumentsValue.toList()) {
            if (argument.metaType().id() != QMetaType::QString || argument.toString().contains(QChar::Null))
                return failure(QCoreApplication::translate("Muxora", "程序参数 args 必须是字符串数组。"));
            arguments.append(argument.toString());
            bytes += argument.toString().size();
        }
        if (arguments.size() > 128 || bytes > 32768)
            return failure(QCoreApplication::translate("Muxora", "程序参数超过数量或长度限制。"));
        if (d->pending.size() >= maximumLaunches)
            return failure(QCoreApplication::translate("Muxora", "最多同时处理 8 个程序启动请求。"));
        if (!d->thread) {
            d->thread = new QThread(this);
            d->thread->setObjectName(QStringLiteral("DetachedProgramLauncher"));
            d->launcher = new DetachedLauncher(d->generation);
            d->launcher->moveToThread(d->thread);
            connect(d->thread, &QThread::finished, d->launcher, &QObject::deleteLater);
            connect(this, &AppScriptServices::launchRequested, d->launcher, &DetachedLauncher::launch);
            connect(d->launcher, &DetachedLauncher::completed, this,
                    [this](quint64 id, quint64 generation, qint64 pid, const QString &error) {
                        if (generation != d->generation->load() || !d->pending.remove(id))
                            return;
                        const auto request = QStringLiteral("launch.%1").arg(id);
                        const QPointer<AppScriptServices> retained(this);
                        emit launchCompleted(request, pid, error);
                        if (!retained)
                            return;
                        emit eventPublished(QStringLiteral("app.launch.completed"),
                                            QVariantMap{{QStringLiteral("request"), request},
                                                        {QStringLiteral("pid"), pid},
                                                        {QStringLiteral("ok"), error.isEmpty()},
                                                        {QStringLiteral("error"), error}});
                    });
            d->thread->start();
        }
        const auto id = ++d->nextLaunch;
        d->pending.insert(id);
        emit launchRequested(id, d->generation->load(), programValue.toString(), arguments,
                             directoryValue.toString());
        return success(QStringLiteral("launch.%1").arg(id));
    }
    if (method == QStringLiteral("app.play_sound")) {
        if (args.isEmpty() || args[0].toString() == QStringLiteral("beep")) {
            QApplication::beep();
            return success();
        }
        if (args[0].metaType().id() != QMetaType::QString)
            return failure(QCoreApplication::translate("Muxora", "声音参数需要 beep 或本地 WAV 文件路径。"));
        const auto file = QFileInfo(args[0].toString());
        if (!file.isFile() || file.suffix().compare(QStringLiteral("wav"), Qt::CaseInsensitive) ||
            file.size() > 16 * 1024 * 1024)
            return failure(
                QCoreApplication::translate("Muxora", "声音文件必须是最多 16 MiB 的本地 WAV 文件。"));
#ifdef Q_OS_WIN
        const auto path = file.absoluteFilePath().toStdWString();
        return PlaySoundW(path.c_str(), nullptr, SND_FILENAME | SND_ASYNC | SND_NODEFAULT)
                   ? success()
                   : failure(QCoreApplication::translate("Muxora", "无法播放声音文件。"));
#else
        return failure(QCoreApplication::translate("Muxora", "当前平台仅支持 beep 提示音。"));
#endif
    }
    return failure(QCoreApplication::translate("Muxora", "未知脚本服务：%1").arg(method));
}
} // namespace muxora
#include "script_services.moc"

#include <QApplication>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QWidget>
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
static qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
static QByteArray json(const QJsonObject &o) { return QJsonDocument(o).toJson(QJsonDocument::Compact); }
static QString read(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error(("Cannot read " + path).toStdString());
    return QString::fromUtf8(f.readAll());
}
static void write(QFile &f, const QByteArray &data) {
    if (f.write(data) != data.size() || !f.flush()) qFatal("Output write failed: %s", qPrintable(f.fileName()));
}
static void openNew(QFile &f, const QString &path) {
    f.setFileName(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::NewOnly)) throw std::runtime_error(("Refusing to overwrite / cannot create " + path).toStdString());
}
static quint64 ticks(FILETIME t) { return (quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime; }
static quint64 wallTicks() { FILETIME t; GetSystemTimeAsFileTime(&t); return ticks(t); }
static std::atomic_bool interrupted = false;
static BOOL WINAPI consoleControl(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) { interrupted = true; return TRUE; }
    return FALSE;
}
struct Proc { DWORD pid, parent; };
static std::vector<Proc> processes() {
    std::vector<Proc> result;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32 e{}; e.dwSize = sizeof(e);
    if (Process32First(snap, &e)) do { result.push_back({e.th32ProcessID, e.th32ParentProcessID}); } while (Process32Next(snap, &e));
    CloseHandle(snap); return result;
}
struct Usage { double cpu = 0, memory = 0; int count = 0, errors = 0; };
struct ProcessMeter {
    struct Previous { quint64 born, cpu; };
    std::map<DWORD, Previous> previous;
    quint64 lastWall = wallTicks();
    QElapsedTimer clock;
    ProcessMeter() { clock.start(); }
    Usage sample(DWORD root, const std::vector<Proc> &all, bool tree = true) {
        Usage u;
        std::set<DWORD> ids;
        if (root) ids.insert(root);
        if (tree) for (bool changed = true; changed;) {
            changed = false;
            for (auto p : all) if (ids.contains(p.parent) && !ids.contains(p.pid)) { ids.insert(p.pid); changed = true; }
        }
        const double seconds = clock.restart() / 1000.0;
        std::map<DWORD, Previous> next;
        SYSTEM_INFO system{}; GetSystemInfo(&system);
        for (DWORD pid : ids) {
            HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
            if (!h) { u.errors++; continue; }
            u.count++;
            FILETIME born{}, exit{}, kernel{}, user{};
            if (GetProcessTimes(h, &born, &exit, &kernel, &user)) {
                const quint64 total = ticks(kernel) + ticks(user), created = ticks(born);
                const auto old = previous.find(pid);
                quint64 delta = 0;
                if (old != previous.end() && old->second.born == created) delta = total - old->second.cpu;
                else if (created >= lastWall) delta = total;
                next[pid] = {created, total};
                if (seconds > 0) u.cpu += double(delta) / 1e7 / seconds * 100.0 / system.dwNumberOfProcessors;
            } else u.errors++;
            // Private *resident* pages. PrivateUsage is commit, NOT working set.
            std::vector<ULONG_PTR> buffer(131072);
            bool measured = false;
            for (int attempt = 0; attempt < 8; ++attempt) {
                if (QueryWorkingSet(h, buffer.data(), DWORD(buffer.size() * sizeof(ULONG_PTR)))) {
                    auto ws = reinterpret_cast<PSAPI_WORKING_SET_INFORMATION *>(buffer.data());
                    quint64 privatePages = 0;
                    for (ULONG_PTR i = 0; i < ws->NumberOfEntries; ++i) if (!ws->WorkingSetInfo[i].Shared) privatePages++;
                    u.memory += double(privatePages * system.dwPageSize) / 1048576.0;
                    measured = true; break;
                }
                if (GetLastError() != ERROR_BAD_LENGTH) break;
                buffer.resize(buffer.size() * 2);
            }
            if (!measured) u.errors++;
            CloseHandle(h);
        }
        previous = std::move(next); lastWall = wallTicks(); return u;
    }
};

struct Run;
struct Source : QWidget {
    Run &run;
    QString id, spec, url, mode, profile;
    QFile messages;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    UINT32 browserPid = 0;
    qint64 messageCount = 0, lastMessage = 0, probeAt = 0;
    QJsonObject probe, oldProbe;
    ProcessMeter meter;
    Source(Run &r, QString source, QString key, QString page, QString m, QString folder);
    ~Source() override { if (controller) controller->Close(); }
    void start();
    void configured();
    void receive(ICoreWebView2WebMessageReceivedEventArgs *args);
    void event(QString name, QJsonObject detail = {});
    bool check(HRESULT hr, QString operation);
    void resizeEvent(QResizeEvent *e) override {
        QWidget::resizeEvent(e);
        if (controller) { RECT b{0, 0, width(), height()}; controller->put_Bounds(b); }
    }
};
struct Run {
    QString root, out, mode, bundle, browserArgs;
    QFile events, metrics;
    QJsonObject metadata;
    QJsonArray inventory;
    std::vector<std::unique_ptr<Source>> sources;
    ProcessMeter hostMeter;
    int exitCode = 0;
    qint64 started = now();
    void event(QString source, QString name, QJsonObject detail = {}) {
        detail["receivedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        detail["receivedMs"] = now(); detail["source"] = source; detail["event"] = name;
        write(events, json(detail) + '\n');
    }
    void saveMetadata() {
        QFile f(QDir(out).filePath("run.json"));
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) qFatal("Cannot write run.json");
        write(f, QJsonDocument(metadata).toJson(QJsonDocument::Indented));
    }
    void buildBundle() {
        QJsonParseError error;
        const auto manifestBytes = read(QDir(root).filePath("manifest.json")).toUtf8();
        const auto manifest = QJsonDocument::fromJson(manifestBytes, &error).object();
        if (error.error != QJsonParseError::NoError || !manifest["content_scripts"].isArray()) throw std::runtime_error("Invalid manifest content_scripts");
        metadata["ssnVersion"] = manifest["version"];
        metadata["manifestSha256"] = QString::fromLatin1(QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256).toHex());
        bundle = read(":/chrome-shim.js") + "\n" + read(":/bootstrap.js") + "\n";
        for (const auto entryValue : manifest["content_scripts"].toArray()) {
            const auto entry = entryValue.toObject();
            if (entry["js"].toArray().isEmpty()) continue;
            bundle += "\nwindow.__chatSpike.register(" + QString::fromUtf8(json(entry)) + ", function () {\n";
            for (const auto fileValue : entry["js"].toArray()) {
                const auto rel = fileValue.toString();
                const auto path = QFileInfo(QDir(root).filePath(rel)).canonicalFilePath();
                if (path.isEmpty() || !path.startsWith(root + '/', Qt::CaseInsensitive)) throw std::runtime_error(("Manifest path outside ssn-root: " + rel).toStdString());
                const auto contents = read(path);
                inventory.append(QJsonObject{{"file", rel}, {"sha256", QString::fromLatin1(QCryptographicHash::hash(contents.toUtf8(), QCryptographicHash::Sha256).toHex())}});
                // Raw upstream text, never regex-patched/eval'd; file order is manifest order.
                bundle += "\n" + contents + "\n;\n";
            }
            bundle += "\n});\n";
        }
        metadata["scripts"] = inventory;
        metadata["bundleBytes"] = bundle.toUtf8().size();
    }
    void sample() {
        const auto all = processes();
        const auto timestamp = QString::number(now());
        Usage total;
        qint64 totalMessages = 0, totalLast = 0;
        for (auto &s : sources) {
            auto usage = s->meter.sample(s->browserPid, all);
            if (all.empty()) usage.errors++; // failed snapshot must not look like a browser-only tree
            total.cpu += usage.cpu; total.memory += usage.memory; total.count += usage.count; total.errors += usage.errors;
            totalMessages += s->messageCount; totalLast = std::max(totalLast, s->lastMessage);
            QString raf, timer;
            if (s->probeAt && now() - s->probeAt < 15000) {
                const bool same = s->probe["epoch"] == s->oldProbe["epoch"];
                const double dt = (s->probe["elapsedMs"].toDouble() - (same ? s->oldProbe["elapsedMs"].toDouble() : 0)) / 1000.0;
                if (dt > 0) {
                    raf = QString::number((s->probe["rafCount"].toDouble() - (same ? s->oldProbe["rafCount"].toDouble() : 0)) / dt, 'f', 3);
                    timer = QString::number((s->probe["timerCount"].toDouble() - (same ? s->oldProbe["timerCount"].toDouble() : 0)) / dt, 'f', 3);
                }
            }
            s->oldProbe = s->probe;
            QStringList row{timestamp, s->id, mode, QString::number(s->messageCount), s->lastMessage ? QString::number((now()-s->lastMessage)/1000.0, 'f', 3) : "",
                raf, timer, s->probe["visibility"].toString(), s->probe["reportedVisibility"].toString(),
                s->probeAt ? QString::number((now()-s->probeAt)/1000.0, 'f', 3) : "", QString::number(s->probe["blockedInput"].toInt()),
                QString::number(usage.cpu, 'f', 4), QString::number(usage.memory, 'f', 3), QString::number(usage.count), QString::number(usage.errors)};
            write(metrics, row.join(',').toUtf8() + '\n');
        }
        auto usageRow = [&](QString id, Usage u, qint64 count, qint64 last) {
            QStringList row{timestamp, id, mode, QString::number(count), last ? QString::number((now()-last)/1000.0, 'f', 3) : "",
                "", "", "", "", "", "", QString::number(u.cpu, 'f', 4), QString::number(u.memory, 'f', 3), QString::number(u.count), QString::number(u.errors)};
            write(metrics, row.join(',').toUtf8() + '\n');
        };
        usageRow("TOTAL", total, totalMessages, totalLast);
        usageRow("HOST", hostMeter.sample(GetCurrentProcessId(), all, false), 0, 0);
    }
};

Source::Source(Run &r, QString source, QString key, QString page, QString m, QString folder)
    : run(r), id(key), spec(source), url(page), mode(m), profile(folder) {
    setWindowTitle("chat-spike " + id + " [" + mode + "]");
    resize(500, 720); setAttribute(Qt::WA_ShowWithoutActivating);
    openNew(messages, QDir(run.out).filePath("messages-" + id + ".jsonl"));
    QDir().mkpath(profile);
    // Four real HWND/controller states. Hidden never calls show(), including setup.
    // Offscreen remains shown outside the union of ALL monitors, controller visible.
    // Minimized has an iconic HWND, controller visible. Visible is a normal window.
    if (mode == "offscreen") {
        QRect desktop;
        for (auto screen : QGuiApplication::screens()) desktop = desktop.united(screen->geometry());
        move(desktop.left() - width() - 1000, desktop.top() - height() - 1000); show();
    } else if (mode == "minimized") showMinimized();
    else if (mode == "visible") { move(50 + int(run.sources.size()) * 530, 50); show(); }
    winId(); // materialize HWND, including the never-shown hidden case
}
void Source::event(QString name, QJsonObject detail) { run.event(id, name, detail); }
bool Source::check(HRESULT hr, QString operation) {
    if (SUCCEEDED(hr)) return true;
    event("api-error", {{"operation", operation}, {"hresult", QString::number(quint32(hr), 16)}});
    run.exitCode = 2; return false;
}
void Source::start() {
    event("create-environment", {{"url", url}, {"profile", profile}, {"mode", mode}});
    auto options = Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
    // No Chromium flags or frame pump unless --browser-args asks for them.
    if (!run.browserArgs.isEmpty()) check(options->put_AdditionalBrowserArguments(run.browserArgs.toStdWString().c_str()), "browser arguments");
    check(CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.toStdWString().c_str(), options.Get(),
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([this](HRESULT hr, ICoreWebView2Environment *env) -> HRESULT {
            if (!check(hr, "environment") || !env) return S_OK;
            environment = env;
            LPWSTR version = nullptr;
            env->get_BrowserVersionString(&version);
            event("runtime", {{"version", QString::fromWCharArray(version ? version : L"")}}); CoTaskMemFree(version);
            check(env->CreateCoreWebView2Controller(reinterpret_cast<HWND>(winId()),
                Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>([this](HRESULT result, ICoreWebView2Controller *c) -> HRESULT {
                    if (!check(result, "controller") || !c) return S_OK;
                    controller = c; c->get_CoreWebView2(&web);
                    if (!web) { event("missing-webview"); run.exitCode = 2; return S_OK; }
                    configured(); return S_OK;
                }).Get()), "create-controller");
            return S_OK;
        }).Get()), "create-environment");
}
void Source::configured() {
    RECT bounds{0, 0, width(), height()};
    check(controller->put_Bounds(bounds), "bounds");
    check(controller->put_IsVisible(mode != "hidden"), "visibility");
    web->get_BrowserProcessId(&browserPid);
    const auto hwnd = reinterpret_cast<HWND>(winId());
    // Controller creation can restore a parent initially shown minimized. Apply
    // the native iconic state after creation, once, without activating it.
    if (mode == "minimized") ShowWindow(hwnd, SW_SHOWMINNOACTIVE);
    BOOL controllerVisible = FALSE; controller->get_IsVisible(&controllerVisible);
    RECT windowBounds{}; GetWindowRect(hwnd, &windowBounds);
    if (mode == "offscreen") {
        // Use physical desktop coordinates too, so mixed-DPI displays cannot
        // turn a logical Qt offscreen placement into an on-monitor HWND.
        SetWindowPos(hwnd, nullptr, GetSystemMetrics(SM_XVIRTUALSCREEN) - (windowBounds.right-windowBounds.left) - 1000,
            GetSystemMetrics(SM_YVIRTUALSCREEN) - (windowBounds.bottom-windowBounds.top) - 1000, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        GetWindowRect(hwnd, &windowBounds);
        controller->NotifyParentWindowPositionChanged();
    }
    event("controller-ready", {{"browserPid", double(browserPid)}, {"hwndVisible", bool(IsWindowVisible(reinterpret_cast<HWND>(winId())))},
        {"hwndMinimized", bool(IsIconic(hwnd))}, {"controllerVisible", bool(controllerVisible)},
        {"intersectsMonitor", MonitorFromRect(&windowBounds, MONITOR_DEFAULTTONULL) != nullptr},
        {"x", int(windowBounds.left)}, {"y", int(windowBounds.top)}});
    ComPtr<ICoreWebView2Settings> settings;
    web->get_Settings(&settings);
    settings->put_AreHostObjectsAllowed(FALSE);
    settings->put_AreDefaultScriptDialogsEnabled(FALSE);
    settings->put_AreDevToolsEnabled(FALSE);
    ComPtr<ICoreWebView2_3> web3;
    if (SUCCEEDED(web.As(&web3))) check(web3->SetVirtualHostNameToFolderMapping(L"ssn-resources.invalid", run.root.toStdWString().c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW), "resource-mapping");
    else event("resource-mapping-unavailable");
    EventRegistrationToken token{};
    check(web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>([](ICoreWebView2*, ICoreWebView2PermissionRequestedEventArgs *a) -> HRESULT {
        a->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK;
    }).Get(), &token), "permission-handler");
    check(web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>([this](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs *a) -> HRESULT {
        a->put_Handled(TRUE); event("popup-blocked"); return S_OK;
    }).Get(), &token), "popup-handler");
    ComPtr<ICoreWebView2_4> web4;
    if (SUCCEEDED(web.As(&web4))) web4->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>([this](ICoreWebView2*, ICoreWebView2DownloadStartingEventArgs *a) -> HRESULT {
        a->put_Cancel(TRUE); event("download-blocked"); return S_OK;
    }).Get(), &token);
    check(web->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs *a) -> HRESULT {
        receive(a); return S_OK;
    }).Get(), &token), "message-handler");
    check(web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([this](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs *a) -> HRESULT {
        LPWSTR uri = nullptr; a->get_Uri(&uri);
        event("navigation-start", {{"url", QString::fromWCharArray(uri ? uri : L"")}}); CoTaskMemFree(uri); return S_OK;
    }).Get(), &token), "navigation-start-handler");
    check(web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>([this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs *a) -> HRESULT {
        BOOL ok = FALSE; COREWEBVIEW2_WEB_ERROR_STATUS status{}; a->get_IsSuccess(&ok); a->get_WebErrorStatus(&status);
        event("navigation-complete", {{"success", bool(ok)}, {"webErrorStatus", int(status)}});
        if (!ok) run.exitCode = 2;
        // One diagnostic at navigation, never a recurring ExecuteScript keep-alive.
        web->ExecuteScript(L"JSON.stringify({url:location.href,title:document.title,ready:document.readyState})",
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>([this](HRESULT hr, LPCWSTR result) -> HRESULT {
                event("page-diagnostic", {{"hresult", QString::number(quint32(hr), 16)}, {"result", QString::fromWCharArray(result ? result : L"")}}); return S_OK;
            }).Get());
        return S_OK;
    }).Get(), &token), "navigation-complete-handler");
    check(web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([this](ICoreWebView2*, ICoreWebView2ProcessFailedEventArgs *a) -> HRESULT {
        COREWEBVIEW2_PROCESS_FAILED_KIND kind{}; a->get_ProcessFailedKind(&kind);
        QJsonObject detail{{"kind", int(kind)}};
        ComPtr<ICoreWebView2ProcessFailedEventArgs2> a2;
        if (SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&a2)))) {
            COREWEBVIEW2_PROCESS_FAILED_REASON reason{}; INT32 exitCode = 0; LPWSTR description = nullptr;
            a2->get_Reason(&reason); a2->get_ExitCode(&exitCode); a2->get_ProcessDescription(&description);
            detail["reason"] = int(reason); detail["exitCode"] = exitCode;
            detail["exitCodeHex"] = QString::number(quint32(exitCode), 16);
            detail["description"] = QString::fromWCharArray(description ? description : L""); CoTaskMemFree(description);
        }
        event("process-failed", detail);
        if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED || kind == COREWEBVIEW2_PROCESS_FAILED_KIND_FRAME_RENDER_PROCESS_EXITED) {
            QTimer::singleShot(0, this, [this] { event("renderer-crash-reload"); check(web->Reload(), "reload"); });
        } else if (kind == COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED) {
            run.exitCode = 2; event("browser-exit-unrecovered");
        }
        return S_OK;
    }).Get(), &token), "process-failed-handler");
    const auto script = run.bundle.toStdWString();
    check(web->AddScriptToExecuteOnDocumentCreated(script.c_str(), Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
        [this](HRESULT hr, LPCWSTR scriptId) -> HRESULT {
            if (!check(hr, "document-created-script")) return S_OK;
            event("bundle-registered", {{"scriptId", QString::fromWCharArray(scriptId)}, {"bytes", run.bundle.toUtf8().size()}});
            check(web->Navigate(url.toStdWString().c_str()), "navigate"); return S_OK;
        }).Get()), "register-bundle");
}
void Source::receive(ICoreWebView2WebMessageReceivedEventArgs *args) {
    LPWSTR raw = nullptr;
    if (FAILED(args->get_WebMessageAsJson(&raw))) return;
    const auto packet = QJsonDocument::fromJson(QString::fromWCharArray(raw).toUtf8()).object(); CoTaskMemFree(raw);
    const auto kind = packet["kind"].toString();
    if (kind == "probe") { probe = packet; probeAt = now(); return; }
    if (kind == "event") { event(packet["name"].toString(), packet); return; }
    if (kind != "capture") return;
    const auto payload = packet["payloadJson"].toString().toUtf8();
    QJsonParseError error;
    // Requests are normally objects; raw payload bytes remain intact in JSONL.
    const auto request = QJsonDocument::fromJson(payload, &error).object();
    QJsonObject envelope{{"receivedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}, {"receivedMs", now()},
        {"source", id}, {"frameUrl", packet["frameUrl"]}};
    auto line = json(envelope); line.chop(1);
    write(messages, line + ",\"payload\":" + payload + "}\n");
    auto count = [&](const QJsonObject &m) {
        if ((m["chatname"].isString() || m["username"].isString()) && m["chatmessage"].isString() && !m["event"].toBool() && !m["event"].isString()) { messageCount++; lastMessage = now(); }
    };
    count(request["message"].toObject());
    for (const auto m : request["messages"].toArray()) count(m.toObject());
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QCoreApplication::setApplicationName("chat-spike");
    QCommandLineParser parser; parser.setApplicationDescription("Unassisted WebView2 Social Stream measurement harness"); parser.addHelpOption();
    parser.addOptions({{QStringList{"ssn-root"}, "Read-only Social Stream repository", "dir"}, {QStringList{"source"}, "Repeat: twitch:channel, youtube:videoId, url:https://...", "source"},
        {QStringList{"mode"}, "hidden, offscreen, minimized, visible", "mode", "hidden"}, {QStringList{"out"}, "New run directory", "dir"}, {QStringList{"duration-min"}, "Stop after N minutes (otherwise until Ctrl+C/termination)", "N"},
        {QStringList{"browser-args"}, "Extra Chromium arguments for every source", "args"}, {QStringList{"shared-browser"}, "Use one profile and browser process for all sources"}});
    parser.process(app);
    try {
        Run run;
        run.root = QFileInfo(parser.value("ssn-root")).canonicalFilePath();
        run.mode = parser.value("mode");
    run.browserArgs = parser.value("browser-args");
        if (run.root.isEmpty() || !parser.isSet("out") || parser.values("source").isEmpty() || !QStringList{"hidden", "offscreen", "minimized", "visible"}.contains(run.mode)) throw std::runtime_error("Require --ssn-root, --source, --out and a valid --mode; see --help");
        bool validDuration = true;
        const double duration = parser.isSet("duration-min") ? parser.value("duration-min").toDouble(&validDuration) : 0;
        if (!validDuration || (parser.isSet("duration-min") && (duration <= 0 || duration > 30000))) throw std::runtime_error("duration-min must be > 0 and <= 30000");
        run.out = QDir::cleanPath(QFileInfo(parser.value("out")).absoluteFilePath());
        if (QFileInfo::exists(QDir(run.out).filePath("run.json")) || QFileInfo::exists(QDir(run.out).filePath("profiles"))) throw std::runtime_error("Use a fresh --out directory; profiles/logs are never reused");
        if (!QDir().mkpath(run.out)) throw std::runtime_error("Cannot create out directory");
        openNew(run.events, QDir(run.out).filePath("events.log"));
        openNew(run.metrics, QDir(run.out).filePath("metrics.csv"));
        write(run.metrics, "timestamp_ms,source,mode,message_count,last_message_age_s,raf_per_s,timer_per_s,visibility,reported_visibility,probe_age_s,blocked_input,cpu_percent,private_working_set_mib,process_count,process_errors\n");
        run.metadata = {{"startMs", run.started}, {"mode", run.mode}, {"ssnRoot", run.root}, {"durationMin", duration}, {"keepalive", false}, {"browserArgs", run.browserArgs}, {"sharedBrowser", parser.isSet("shared-browser")},
            {"settings", QJsonObject{{"textonlymode", true}}}, {"cpuConvention", "100% = all logical processors; TOTAL excludes HOST"}};
        SYSTEM_INFO system{}; GetSystemInfo(&system);
        run.metadata["logicalProcessors"] = int(system.dwNumberOfProcessors);
        run.metadata["qtVersion"] = qVersion();
        run.buildBundle();
        QJsonArray sourceMetadata;
        std::set<QString> keys;
        for (const auto spec : parser.values("source")) {
            const auto type = spec.section(':', 0, 0), value = spec.mid(type.size()+1);
            QString url, key;
            if (type == "twitch" && QRegularExpression("^[A-Za-z0-9_]+$").match(value).hasMatch()) { key = "twitch-" + value.toLower(); url = "https://www.twitch.tv/popout/" + value.toLower() + "/chat"; }
            else if (type == "youtube" && QRegularExpression("^[A-Za-z0-9_-]+$").match(value).hasMatch()) { key = "youtube-" + value; url = "https://www.youtube.com/live_chat?v=" + value; }
            else if (type == "url" && QUrl(value).isValid() && QStringList{"http", "https"}.contains(QUrl(value).scheme())) { key = "url-" + QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex().left(12)); url = value; }
            else throw std::runtime_error(("Invalid source: " + spec).toStdString());
            if (!keys.insert(key).second) throw std::runtime_error("Duplicate source");
            const auto profile = QDir(run.out).filePath("profiles/" + (parser.isSet("shared-browser") ? QStringLiteral("shared") : key));
            run.sources.push_back(std::make_unique<Source>(run, spec, key, url, run.mode, profile));
            sourceMetadata.append(QJsonObject{{"id", key}, {"spec", spec}, {"url", url}});
        }
        run.metadata["sources"] = sourceMetadata; run.saveMetadata();
        run.event("HOST", "run-start", {{"mode", run.mode}, {"sourceCount", int(run.sources.size())}});
        for (auto &source : run.sources) source->start();
        QTimer timer; timer.setTimerType(Qt::PreciseTimer);
        QObject::connect(&timer, &QTimer::timeout, &app, [&] { run.sample(); }); timer.start(10000);
        SetConsoleCtrlHandler(consoleControl, TRUE);
        QTimer stopTimer; QObject::connect(&stopTimer, &QTimer::timeout, &app, [&] { if (interrupted) app.quit(); }); stopTimer.start(250);
        if (duration > 0) QTimer::singleShot(qint64(duration * 60000), &app, &QApplication::quit);
        app.exec();
        SetConsoleCtrlHandler(consoleControl, FALSE);
        for (auto &source : run.sources) if (!source->probeAt) {
            source->event("probe-never-received"); run.exitCode = 2;
        }
        run.sample(); run.metadata["endMs"] = now(); run.metadata["exitCode"] = run.exitCode; run.saveMetadata();
        run.event("HOST", "run-end", {{"exitCode", run.exitCode}});
        std::printf("Run complete: %s (exit %d)\n", qPrintable(run.out), run.exitCode);
        return run.exitCode;
    } catch (const std::exception &e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}

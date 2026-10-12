#pragma once
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QKeySequence>
#include <QTimer>
#include <windows.h>
#include <functional>
#include <map>

namespace versus::mixer {
// GUI-thread registrations; no keyboard hook, injection, or input logging.
class GlobalHotkeys : public QAbstractNativeEventFilter {
    struct Binding { UINT key, modifiers; std::function<void(bool)> action; bool held; };
    std::map<int, Binding> bindings_;
    QTimer releases_;
public:
    GlobalHotkeys() {
        qApp->installNativeEventFilter(this);
        releases_.setInterval(20);
        QObject::connect(&releases_, &QTimer::timeout, [this] {
            bool pending = false;
            for (auto &[id, b] : bindings_) if (b.held) {
                const bool down = (GetAsyncKeyState(b.key)&0x8000) &&
                    (!(b.modifiers&MOD_CONTROL) || (GetAsyncKeyState(VK_CONTROL)&0x8000)) &&
                    (!(b.modifiers&MOD_ALT) || (GetAsyncKeyState(VK_MENU)&0x8000)) &&
                    (!(b.modifiers&MOD_SHIFT) || (GetAsyncKeyState(VK_SHIFT)&0x8000));
                if (!down) { b.held = false; b.action(false); } else pending = true;
            }
            if (!pending) releases_.stop();
        });
    }
    ~GlobalHotkeys() override { clear(); }
    void clear() {
        releases_.stop();
        for (auto &[id, b] : bindings_) { UnregisterHotKey(nullptr, id); if (b.held) b.action(false); }
        bindings_.clear();
    }
    QString add(int index, const QKeySequence &sequence, std::function<void(bool)> action) {
        if (sequence.isEmpty()) return {};
        if (sequence.count() != 1) return "Use a single key combination.";
        const auto combination = sequence[0];
        const int key = combination.key(); const auto modifiers = combination.keyboardModifiers();
        UINT vk = 0, mods = MOD_NOREPEAT;
        if (modifiers.testFlag(Qt::MetaModifier)) return "Windows-key shortcuts are reserved by Windows.";
        if (modifiers.testFlag(Qt::ControlModifier)) mods |= MOD_CONTROL;
        if (modifiers.testFlag(Qt::AltModifier)) mods |= MOD_ALT;
        if (modifiers.testFlag(Qt::ShiftModifier)) mods |= MOD_SHIFT;
        if (key >= Qt::Key_F1 && key <= Qt::Key_F24 && key != Qt::Key_F12) vk = VK_F1+key-Qt::Key_F1;
        else if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9)) vk = key;
        else if (key == Qt::Key_Space) vk = VK_SPACE;
        else if (key == Qt::Key_Return || key == Qt::Key_Enter) vk = VK_RETURN;
        if (!vk || (!(mods&(MOD_CONTROL|MOD_ALT)) && !(key >= Qt::Key_F1 && key <= Qt::Key_F24)))
            return "Use Ctrl/Alt with a letter, number, Space or Enter, or a function key except F12.";
        const int id = 0x5300+index;
        if (!RegisterHotKey(nullptr, id, mods, vk)) return "Already in use by Windows or another application.";
        bindings_.emplace(id, Binding{vk, mods, std::move(action), false}); return {};
    }
    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override {
        if (type != "windows_generic_MSG" && type != "windows_dispatcher_MSG") return false;
        auto *msg = static_cast<MSG*>(message);
        if (msg->message != WM_HOTKEY) return false;
        const auto found = bindings_.find(int(msg->wParam)); if (found == bindings_.end()) return false;
        auto &b = found->second;
        if (!b.held) { b.held = true; b.action(true); releases_.start(); }
        // Qt's thread-message dispatcher can supply no LRESULT storage for
        // WM_HOTKEY, unlike messages addressed to a native window.
        if (result) *result = 0;
        return true;
    }
};
}

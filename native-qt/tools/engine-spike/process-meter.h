#pragma once
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

// Stream watch: logs every stream the StreamManager holds, its state changes and the
// free address space, once a second. Armed by a level editor F9 entry or by
// [DeadSpace2008Randomizer] StreamWatch=1; off otherwise. Lines are capped.

struct StreamWatchEntry
{
    uint32_t id;
    uintptr_t object;
    int state;
    ULONGLONG since;
    bool seen;
};

StreamWatchEntry gStreamWatch[1024]{};
unsigned gStreamWatchCount = 0;
unsigned gStreamWatchLines = 0;
ULONGLONG gStreamWatchStarted = 0;
ULONGLONG gStreamWatchLastSummary = 0;
constexpr unsigned kStreamWatchMaxLines = 4000;

bool StreamWatchEnabled()
{
    static int configured = -1;
    if (configured < 0)
        configured = GetPrivateProfileIntA("DeadSpace2008Randomizer", "StreamWatch", 0, gIniPath) != 0;
    return configured || InterlockedCompareExchange(&gStreamWatchArmed, 0, 0) != 0;
}

bool StreamWatchLog()
{
    return gStreamWatchLines++ < kStreamWatchMaxLines;
}

// A printable string at ptr, or empty.
void StreamWatchText(uintptr_t ptr, char* out, size_t size)
{
    out[0] = 0;
    char buffer[80]{};
    if (!ptr || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(ptr), buffer, sizeof(buffer) - 1, nullptr))
        return;
    size_t n = 0;
    while (n < sizeof(buffer) - 1 && buffer[n] >= 0x20 && buffer[n] < 0x7F)
        ++n;
    if (n < 4 || buffer[n] != 0)
        return;
    std::snprintf(out, size, "%s", buffer);
}

void StreamWatchDescribe(const StreamWatchEntry& e)
{
    uint32_t words[24]{};
    ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(e.object), words, sizeof(words), nullptr);
    char line[512]{};
    int used = std::snprintf(line, sizeof(line), "stream watch: new stream id=%08X object=%08X state=%d words",
                             e.id, static_cast<unsigned>(e.object), e.state);
    for (unsigned i = 0; i < 24 && used > 0 && used < static_cast<int>(sizeof(line)) - 10; ++i)
        used += std::snprintf(line + used, sizeof(line) - used, " %08X", words[i]);
    if (StreamWatchLog())
        Log("%s", line);
    // name the stream: the first word that points at readable text
    for (unsigned i = 0; i < 24; ++i)
    {
        char text[80]{};
        StreamWatchText(words[i], text, sizeof(text));
        if (text[0])
        {
            if (StreamWatchLog())
                Log("stream watch:   id=%08X +%02X -> \"%s\"", e.id, i * 4, text);
            break;
        }
    }
}

void StreamWatchMemory(const char* why)
{
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) && StreamWatchLog())
        Log("stream watch: memory (%s): address space free %llu MB of %llu MB, physical free %llu MB",
            why, status.ullAvailVirtual >> 20, status.ullTotalVirtual >> 20, status.ullAvailPhys >> 20);
}

void StreamWatchTick()
{
    if (!StreamWatchEnabled())
        return;
    const ULONGLONG now = GetTickCount64();
    if (!gStreamWatchStarted)
    {
        gStreamWatchStarted = now;
        Log("stream watch: on (armed=%ld), polling the StreamManager once a second, at most %u lines",
            InterlockedCompareExchange(&gStreamWatchArmed, 0, 0), kStreamWatchMaxLines);
        StreamWatchMemory("start");
    }
    void* managerObject = nullptr;
    if (!ResolveStreamManager(&managerObject))
    {
        static ULONGLONG lastMissing = 0;
        if (now - lastMissing > 10000 && StreamWatchLog())
        {
            lastMissing = now;
            Log("stream watch: no StreamManager yet");
        }
        return;
    }
    const uintptr_t manager = reinterpret_cast<uintptr_t>(managerObject);
    uintptr_t buckets = 0;
    unsigned bucketCount = 0;
    if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(manager + 0x54), &buckets, sizeof(buckets), nullptr) ||
        !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(manager + 0x58), &bucketCount, sizeof(bucketCount), nullptr) ||
        !buckets || !bucketCount || bucketCount > 65536)
        return;
    for (unsigned i = 0; i < gStreamWatchCount; ++i)
        gStreamWatch[i].seen = false;
    unsigned total = 0, notReady = 0;
    for (unsigned b = 0; b < bucketCount; ++b)
    {
        uintptr_t node = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(buckets + b * sizeof(uintptr_t)), &node, sizeof(node), nullptr))
            continue;
        for (unsigned step = 0; node && step < 4096; ++step)
        {
            uint32_t id = 0;
            uintptr_t stream = 0, next = 0;
            if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(node), &id, sizeof(id), nullptr) ||
                !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(node + 4), &stream, sizeof(stream), nullptr) ||
                !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(node + 8), &next, sizeof(next), nullptr))
                break;
            int state = -1;
            if (stream)
                ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(stream + 0x1C), &state, sizeof(state), nullptr);
            ++total;
            if (state != 3)
                ++notReady;
            StreamWatchEntry* entry = nullptr;
            for (unsigned i = 0; i < gStreamWatchCount; ++i)
                if (gStreamWatch[i].id == id && gStreamWatch[i].object == stream)
                {
                    entry = &gStreamWatch[i];
                    break;
                }
            if (!entry && gStreamWatchCount < 1024)
            {
                entry = &gStreamWatch[gStreamWatchCount++];
                *entry = {id, stream, state, now, true};
                StreamWatchDescribe(*entry);
            }
            else if (entry)
            {
                entry->seen = true;
                if (entry->state != state)
                {
                    if (StreamWatchLog())
                        Log("stream watch: id=%08X object=%08X state %d -> %d after %llu ms", id, static_cast<unsigned>(stream),
                            entry->state, state, now - entry->since);
                    entry->state = state;
                    entry->since = now;
                }
            }
            if (next == node)
                break;
            node = next;
        }
    }
    for (unsigned i = 0; i < gStreamWatchCount;)
    {
        if (!gStreamWatch[i].seen)
        {
            if (StreamWatchLog())
                Log("stream watch: id=%08X object=%08X gone (last state %d)", gStreamWatch[i].id,
                    static_cast<unsigned>(gStreamWatch[i].object), gStreamWatch[i].state);
            gStreamWatch[i] = gStreamWatch[--gStreamWatchCount];
            continue;
        }
        ++i;
    }
    if (notReady && now - gStreamWatchLastSummary >= 5000)
    {
        gStreamWatchLastSummary = now;
        char line[400]{};
        int used = std::snprintf(line, sizeof(line), "stream watch: %u streams, %u not ready:", total, notReady);
        for (unsigned i = 0; i < gStreamWatchCount && used > 0 && used < static_cast<int>(sizeof(line)) - 40; ++i)
            if (gStreamWatch[i].state != 3)
                used += std::snprintf(line + used, sizeof(line) - used, " %08X(state %d, %llu s)", gStreamWatch[i].id,
                                      gStreamWatch[i].state, (now - gStreamWatch[i].since) / 1000);
        if (StreamWatchLog())
            Log("%s", line);
        StreamWatchMemory("pending");
    }
}

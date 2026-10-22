// Dead Space 1 (2008) spawn scaling, first stage.
//
// The 2008 game runs on the same EARS framework as Dead Space 3 and keeps the
// same authored spawner: the strings FuncSpawn and FuncSpawnRetryMessage are
// both in the executable. What it does not have is an AI Director, so the one
// mechanism that carries a whole campaign is also the only one there is.
//
// Two things had to be established before any of that can be used, and both
// are settled:
//
//   * the retail .text is encrypted on disk behind a SecuROM .bind section.
//     Eight megabytes of it hold exactly one `push ebp; mov ebp,esp`, so every
//     signature has to be taken from live memory, never from the file.
//   * injection works through a proxy DLL; the game loads winmm from its own
//     directory before the system one.
//
// This stage does nothing to the game. It reports where it landed, so the
// scaffolding can be proven before a single byte is patched.
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include "../include/safetyhook/safetyhook.hpp"

namespace
{
    HMODULE gPlugin = nullptr;
    HMODULE gGame = nullptr;
    char gLogPath[MAX_PATH]{};
    char gSpawnCatalogPath[MAX_PATH]{};
    char gGameRoot[MAX_PATH]{};

    // Three threads log (the game thread, the poll thread, the overlay); one lock keeps lines whole.
    SRWLOCK gLogLock = SRWLOCK_INIT;
    struct LogGuard
    {
        LogGuard() { AcquireSRWLockExclusive(&gLogLock); }
        ~LogGuard() { ReleaseSRWLockExclusive(&gLogLock); }
    };

    // A bad %s argument faults inside the formatting and callers catch that
    // with __try, so it has to happen before the lock is taken.
    bool FormatLogLine(char* out, size_t size, const char* format, va_list args)
    {
        __try
        {
            _vsnprintf_s(out, size, _TRUNCATE, format, args);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void Log(const char* format, ...)
    {
        static thread_local char text[4096];
        va_list args;
        va_start(args, format);
        const bool formatted = FormatLogLine(text, sizeof(text), format, args);
        va_end(args);
        if (!formatted)
            _snprintf_s(text, sizeof(text), _TRUNCATE, "(a log line faulted while formatting: %.200s)", format);
        LogGuard guard;
        FILE* file = nullptr;
        if (fopen_s(&file, gLogPath, "a") || !file)
            return;
        SYSTEMTIME now{};
        GetLocalTime(&now);
        std::fprintf(file, "[%02u:%02u:%02u.%03u] %s\n", now.wHour, now.wMinute,
                     now.wSecond, now.wMilliseconds, text);
        std::fclose(file);
    }

#include "hook_signatures_generated.inc"
    // Every fixed hook site is compared with the retail bytes before a hook goes in
    // (hook_signatures_generated.inc from trainer/build_ds1_hook_signatures.py). Another exe, a
    // site that already carries a jump (a second copy of the patch, another mod) or changed bytes
    // refuse that hook, and the log says which. Sites computed at run time are not listed.
    volatile LONG gHooksVerified = 0, gHooksRefused = 0, gHooksUnlisted = 0;
    int gExeIsRetail = -1;

    bool ExeIsRetail()
    {
        if (gExeIsRetail < 0)
        {
            const auto* module = reinterpret_cast<const BYTE*>(GetModuleHandleA(nullptr));
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(module + dos->e_lfanew);
            const DWORD stamp = nt->FileHeader.TimeDateStamp;
            gExeIsRetail = stamp == kRetailTimeDateStamp ? 1 : 0;
            Log("exe identity: PE timestamp %08X, %s", static_cast<unsigned>(stamp),
                gExeIsRetail ? "the retail 1.0.0.222 build every hook was written for"
                             : "NOT the retail build: no listed hook is installed");
        }
        return gExeIsRetail == 1;
    }

    void HexBytes(const unsigned char* bytes, unsigned length, char* out, size_t size)
    {
        out[0] = 0;
        for (unsigned i = 0; i < length && (i + 1) * 3 < size; ++i)
            std::snprintf(out + i * 3, size - i * 3, "%02X ", bytes[i]);
    }

    bool HookSiteOk(void* target, const char* kind)
    {
        const uintptr_t address = reinterpret_cast<uintptr_t>(target);
        const auto* code = reinterpret_cast<const unsigned char*>(address);
        for (const HookSignature& site : kHookSignatures)
        {
            if (site.address != address)
                continue;
            if (!ExeIsRetail())
            {
                InterlockedIncrement(&gHooksRefused);
                return false;
            }
            const bool retail = site.length && std::memcmp(code, site.bytes, site.length) == 0;
            if (retail || (!site.length && code[0] != 0xE9))
            {
                InterlockedIncrement(&gHooksVerified);
                return true;
            }
            char seen[32], wanted[32];
            HexBytes(code, site.length ? site.length : 5, seen, sizeof(seen));
            HexBytes(site.bytes, site.length, wanted, sizeof(wanted));
            InterlockedIncrement(&gHooksRefused);
            Log("hook %s (%s) at %08X REFUSED: %s %s(expected %s)", site.name, kind,
                static_cast<unsigned>(address),
                code[0] == 0xE9 ? "a jump is already there, another copy of the patch or another mod;"
                                : "the bytes differ:",
                seen, site.length ? wanted : "no retail bytes known, only no jump");
            return false;
        }
        if (InterlockedIncrement(&gHooksUnlisted) <= 8)
            Log("hook (%s) at %08X is computed at run time and not in the signature table",
                kind, static_cast<unsigned>(address));
        return true;
    }

    safetyhook::InlineHook VerifiedInline(void* target, void* destination)
    {
        if (!HookSiteOk(target, "inline"))
            return {};
        return safetyhook::create_inline(target, destination);
    }

    safetyhook::MidHook VerifiedMid(void* target, safetyhook::MidHookFn destination)
    {
        if (!HookSiteOk(target, "mid"))
            return {};
        return safetyhook::create_mid(target, destination);
    }

    // The image base is not the interesting part; whether the code section has
    // been decrypted by the time we run is. If it still looks like the file on
    // disk, we are too early and any scan would find nothing.
    bool CodeLooksDecrypted(HMODULE module)
    {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const BYTE*>(module) + dos->e_lfanew);
        const auto* section = IMAGE_FIRST_SECTION(nt);
        const BYTE* text = reinterpret_cast<const BYTE*>(module) +
                           section->VirtualAddress;
        const size_t size = section->Misc.VirtualSize;

        size_t prologues = 0;
        for (size_t i = 0; i + 3 < size && prologues < 64; ++i)
            if (text[i] == 0x55 && text[i + 1] == 0x8B && text[i + 2] == 0xEC)
                ++prologues;
        Log("code section %08X, %u bytes, %u function prologues in the first "
            "run", reinterpret_cast<unsigned>(text),
            static_cast<unsigned>(size), static_cast<unsigned>(prologues));
        return prologues >= 64;
    }

    // The class table sits on the heap: sixteen bytes per entry, holding a
    // factory, a name, the object size and a hash of the name, sorted by that
    // hash. FuncSpawn's factory allocates its own 0x120 bytes on top of the
    // base class and then calls the constructor below.
    // Right after the allocator call inside the factory, where EAX holds the
    // fresh object. The constructor at 0x005B53F0 is the wrong place: the
    // factory hands it ECX pointing at a local descriptor on its own stack,
    // not at the object, which showed up as "objects" 0x3C apart.
    constexpr uintptr_t kFuncSpawnAllocated = 0x00408ABA;
    constexpr unsigned kFuncSpawnSize = 0x120;

    safetyhook::MidHook gFuncSpawnCtor;
    long gSpawners = 0;
    volatile DWORD gGameThreadId = 0;
    // Two independent pieces of evidence that the loading screen is gone and
    // the game is actually being played.  A time window is not evidence: the
    // native character pin deadlocks the level loader, and "sidecars settled
    // plus a few seconds" only ever avoided that window by luck.
    uint32_t gLoadingScreenHandle = 0;
    volatile LONG gSpawnHandlerEntered = 0;
    // A better "the world is built" signal than waiting for the first trigger.
    // Waiting for a trigger costs one authored encounter after every level
    // start and after every checkpoint reload.  What we actually need is the
    // moment the level archive has finished loading and FuncSpawn construction
    // has stopped: that is still inside the loading screen, but past the point
    // where a native request deadlocked the loader.
    bool gPinAtWorldBuilt = true;
    uint32_t gLevelArchiveHandle = 0;
    volatile LONG gLastSpawnerTick = 0;
    constexpr ULONGLONG kSpawnerQuietMs = 400;

    // FuncSpawn's own vtable. Objects are rebuilt constantly as areas stream
    // in and out, so a remembered address is only good while this still
    // matches - the same lesson a stale pointer taught in Dead Space 3, where
    // writing to a freed object crashed the game on every checkpoint restart.
    constexpr uintptr_t kFuncSpawnVtable = 0x00C761A8;

    // Two bits matter, and setting only one does nothing at all.
    //
    // The caller at 0x005B57EF tests `byte [ebx+0x98], 2` before it will even
    // consider a retry, and ebx is the object plus 0x3C, so that byte is
    // +0xD4 and the bit is 2. Inside 0x005B6410 the first instruction is
    // `test byte [esi+0xD4], 8`, which gates the send itself. Bit 1 opens the
    // road, bit 3 permits the journey.
    //
    // Setting only bit 3 left the queued counter at zero on all 101 spawners
    // of a level, which is what proved the road was never taken.
    constexpr unsigned kOptions = 0xD4;
    constexpr unsigned kRetryOption = 0x08;     // send the retry message
    constexpr unsigned kRetryPath = 0x02;       // let the caller reach it
    constexpr unsigned kQueuedCount = 0xD0;

    // Read off the same caller: +0xBC is the authored spawn cap and +0xC0 the
    // running count, with -1 meaning unlimited. Dead Space 3 uses -1 for the
    // same thing. Most spawners in the level already carry -1, so the cap is
    // not what holds their numbers down.
    constexpr unsigned kMaxSpawn = 0xBC;
    constexpr unsigned kNumSpawned = 0xC0;

    // The message handler. `ret 4` with the object in ECX makes it a thiscall
    // taking one argument, and that argument's first dword is the message id.
    // At 0x005B57CD the handler compares that id against +0x60, and only then
    // does it spawn. So sending the same message again is a spawn again, and
    // it passes every check the game makes on the way, including the cap at
    // +0xBC which stands at -1 on most spawners.
    constexpr uintptr_t kSpawnHandler = 0x005B57A0;
    // Immediately after SimManager::Resolve returns. EAX is the resolved
    // runtime object and EBX is FuncSpawn + 0x3C.
    constexpr uintptr_t kSpawnResolverCall = 0x005B58A1;
    constexpr uintptr_t kSpawnResolverReturn = 0x005B58A6;
    // Branch probes inside SimManager_ResolveSpawnTarget.  These are installed
    // only with SpawnResolverDiagnostics and remain observational: they tell us
    // whether a foreign target is rejected by the key gate, a SimManager
    // filter, the callback path, or the class factory.
    constexpr uintptr_t kResolverPreflightPassed = 0x006F5841;
    // Hook the TEST itself, not the following JE.  A narrowly-scoped global
    // replacement may turn AL from false to true here and then let the retail
    // TEST/branch execute normally.
    constexpr uintptr_t kResolverFilterResult = 0x006F585F;
    constexpr uintptr_t kResolverFiltersPassed = 0x006F5875;
    constexpr uintptr_t kResolverCallbackResult = 0x006F5892;
    constexpr uintptr_t kResolverFactoryEnter = 0x006F58B5;
    constexpr uintptr_t kResolverFactoryResult = 0x006F58C0;
    // SimManager filter 0 lives at 0x00403B80 and is the retail resource
    // residency gate.  0x006EF250 fetches the value field whose name hashes to
    // 0xAE986323 ("Animated") from the packet currently being resolved, the
    // four words of that field are hashed as a GUID, 0x006CA7A0 asks the
    // StreamManager which stream publishes that resource, and 0x004028E0 reads
    // that stream's state, which must be 3.  These three probes report the
    // reference, the stream id and the state, so a rejection can be attributed
    // to the resource lookup or to the stream state instead of being guessed.
    // iMsgRestore handler.  A checkpoint reload rebuilds enemies that were
    // alive when the checkpoint was set, and it does so during world
    // construction.  Our synthetic targets are republished at the same time,
    // so the question is only which of the two happens first: with global
    // templates on, restored randomized enemies vanish, with them off they
    // survive.  This probe timestamps every restore so the gap is measurable
    // instead of guessed.
    constexpr uintptr_t kRestoreHandler = 0x005108A0;
    // SimManager::FindOrCreateTargetKey.  Everything that turns a target GUID
    // into a key goes through here, including - if the plan holds - the
    // checkpoint restore of an enemy that was alive.  EDI carries the GUID.
    // Publishing early cannot win that race: the sidecar streams need about
    // 420 ms to reach state 3 and the restore does not wait.  Substituting the
    // GUID here is the remaining option, so first measure whether the restore
    // actually passes through.
    constexpr uintptr_t kFindTargetKey = 0x006F7DC0;
    constexpr uintptr_t kFilterAnimatedReference = 0x00403B8D;
    constexpr uintptr_t kFilterAnimatedStream = 0x00403BC8;
    constexpr uintptr_t kFilterAnimatedState = 0x00403BD1;
    constexpr unsigned kTriggerMessage = 0x60;
    constexpr unsigned kHandlerBaseOffset = 0x3C;   // ECX points here, not at the object
    using SpawnHandlerFn = void(SAFETYHOOK_THISCALL*)(void*, void*);

    // The enemy classes, taken from the game's own class table. Sizes are no
    // use for telling them apart: the table stores what each class adds to its
    // base, not the size of the finished object, so a spawned necromorph shows
    // up as an allocation that matches no entry at all. The factories are
    // exact, so those are hooked instead.
    // Filled from the game's own class table rather than from a guessed list:
    // every class whose objects are actor sized gets a slot, a hook and an ini
    // key. Guessing produced fourteen names and missed whatever the level
    // designers actually used.
    constexpr unsigned kMaxActorClasses = 32;

    // Taken from the game's own class table, not invented: these are the
    // classes that turned up there with necromorph and NPC names. Selecting
    // by object size instead filled the list with UI widgets and weapons,
    // because plenty of harmless things are the same size as a slasher.
    const char* const kWanted[] = {
        "Zombie", "Lurker", "Brute", "Leaper", "Exploder", "Infector",
        "Pregnant", "Guardian", "GuardianPod", "GuardianTentacle",
        "DividerCore", "DividerPart", "SwarmEnemy", "StaticEnemy",
        "GenericNPC", "NeutralNPC", "Slug", "SlugTentacle", "HiveMind",
        "HiveMindEye", "Leviathan", "LeviathanTentacle", "DragTentacle",
        "SlamDragTentacle", "Hunter",
    };
    constexpr unsigned kWantedCount = sizeof(kWanted) / sizeof(kWanted[0]);
    char gActorNames[kMaxActorClasses][40]{};
    unsigned gActorPercent[kMaxActorClasses]{};   // total per trigger, not a percentage
    bool gDumped[kMaxActorClasses]{};             // one full dump per class
    unsigned gActorCount = 0;
    constexpr unsigned kEnemyClassCount = kMaxActorClasses;

    // Discovery mode. Hooking every factory in the class table and reporting
    // whatever a spawn actually builds beats guessing at a list of enemy
    // names, which is how the first fourteen were picked. Off unless asked
    // for: it puts a hook on 277 functions.
    bool gDiscover = false;
    struct FactoryEntry { uintptr_t address; unsigned nameIndex; };
    FactoryEntry gAllFactories[320]{};
    unsigned gAllFactoryCount = 0;
    safetyhook::MidHook gDiscoverHooks[320];
    thread_local unsigned gSeenClasses[8]{};
    thread_local unsigned gSeenClassCount = 0;
    // Distinct classes do not prove that only one actor was created: two
    // Zombies still leave gSeenClassCount at one. Count every factory entry.
    thread_local unsigned gFactoryInvocationCount = 0;

    safetyhook::MidHook gEnemyFactories[kEnemyClassCount];
    unsigned gEnemyPercent[kEnemyClassCount];       // from the ini, 100 = as authored
    thread_local unsigned gCreatedIndex = 0xFFFF;   // which enemy class appeared

    // Every class factory allocates through this one function, and the class
    // table records each class's object size. Watching the sizes requested
    // while a spawner is doing its work therefore says what it produced,
    // which is the only way to tell a necromorph from a pile of corpses: the
    // spawner itself carries no type, both of its guids are unique per
    // instance.
    constexpr uintptr_t kAllocator = 0x006359E0;
    safetyhook::MidHook gAllocator;
    thread_local bool gWatching = false;

    struct SeenSize { unsigned size; unsigned count; };
    SeenSize gSizes[48]{};
    unsigned gSizeCount = 0;

    // name -> size, read out of the game's own class table at startup.
    struct ClassEntry { unsigned size; const char* name; };
    ClassEntry gClasses[400]{};
    unsigned gClassCount = 0;
    char gClassNames[400][40]{};

    const char* ClassForSize(unsigned size)
    {
        for (unsigned i = 0; i < gClassCount; ++i)
            if (gClasses[i].size == size)
                return gClasses[i].name;
        return nullptr;
    }

    unsigned gExtraSpawns = 1;   // vanilla until the INI says otherwise (startup, live reload and menu agree)
    char gIniPath[MAX_PATH]{};

    // A DS1 FuncSpawn is intentionally split in two identities.  +0x20 is
    // the source entity UID (useful for locating the object in a SimG file),
    // whereas +0x50 is the target entity UID consumed by the stock resolver.
    // The target is not a class name: it identifies an authored template that
    // can carry its own model, animation banks, scripts and vent behaviour.
    // Replacing that UID for exactly one stock handler call therefore retains
    // the original trigger and placement, unlike creating an actor ourselves.
    constexpr unsigned kSourceGuid = 0x20;
    constexpr unsigned kTargetGuid = 0x50;
    // FuncSpawn passes this group-local owner token as argument three to
    // SimManager_ResolveSpawnTarget.  A foreign target UID paired with the
    // source chapter's token is rejected before its class factory runs.
    constexpr unsigned kOwnerContext = 0x1C;

    struct Guid128
    {
        uint32_t value[4]{};
    };

    // Retail DS1 exposes the same value through its hidden
    // `-checkpoint <NeoUID>` argument.  That parser converts the text to four
    // dwords and stores them at 0x00F48194..0x00F481A0.  Writing those dwords
    // while the main menu is idle changes the checkpoint used by the next
    // New Game; it does not teleport or touch an existing save.
    constexpr uintptr_t kCheckpointGlobalsRva = 0x00F48194 - 0x00400000;
    constexpr uintptr_t kNewGameCheckpointTextRva = 0x00C7B13C - 0x00400000;
    constexpr char kCheckpointChapter1HangarFinaleText[] =
        "XCENTKOWSK_C853BA9_BCD388F_v3";
    constexpr char kCheckpointChapter1TramRepairCompleteText[] =
        "XENTKOWSKI_C89EA49_DED4C5E_v3";
    constexpr char kCheckpointChapter1InitialText[] =
        "XCENTKOWSK_C8A99CD_622DBBB_v3";
    static_assert(sizeof(kCheckpointChapter1HangarFinaleText) ==
                  sizeof(kCheckpointChapter1InitialText));
    static_assert(sizeof(kCheckpointChapter1TramRepairCompleteText) ==
                  sizeof(kCheckpointChapter1InitialText));
    constexpr Guid128 kCheckpointChapter1HangarFinale{{
        0x4BC853BA, 0x9BCD388F, 0x544E4543, 0x53574F4B
    }};
    constexpr Guid128 kCheckpointChapter1TramRepairComplete{{
        0x49C89EA4, 0x9DED4C5E, 0x4B544E45, 0x4B53574F
    }};
    constexpr Guid128 kCheckpointChapter1Initial{{
        0x4BC8A99C, 0xD622DBBB, 0x544E4543, 0x53574F4B
    }};
    bool gCheckpointChanger = false;
    unsigned gCheckpointChangerHotkey = VK_F9;

    bool SameGuid(const Guid128& a, const Guid128& b)
    {
        return std::memcmp(&a, &b, sizeof(a)) == 0;
    }

    Guid128* CheckpointGlobals()
    {
        if (!gGame)
            return nullptr;
        auto* target = reinterpret_cast<Guid128*>(
            reinterpret_cast<uintptr_t>(gGame) + kCheckpointGlobalsRva);
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(target, &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return nullptr;
        const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        if (!(memory.Protect & writable))
            return nullptr;
        return target;
    }

    // New Game builds its starting level out of two writable buffers in the
    // image: a folder ("CH01_flt\\") and an archive name ("CH01_flt.str").
    // The checkpoint search then looks for the UID inside whatever level that
    // is, which is why setting only the UID moved nothing across chapters -
    // a chapter 4 checkpoint is simply not in chapter 1's list.  Every chapter
    // folder is eight characters, so both buffers keep their exact length.
    constexpr uintptr_t kNewGameLevelFolderRva = 0x00C7B120 - 0x00400000;
    constexpr uintptr_t kNewGameLevelArchiveRva = 0x00C7B12C - 0x00400000;
    const char* const kChapterFolders[] = {
        "CH01_flt", "CH02_med", "CH03_eng", "CH04_brg", "CH05_med", "CH06_hyd",
        "CH07_min", "CH08_brg", "CH09_mil", "CH10_crw", "CH11_flt", "CH12_col",
    };

    bool WriteImageText(uintptr_t rva, const char* value, size_t length)
    {
        if (!gGame)
            return false;
        char* target = reinterpret_cast<char*>(
            reinterpret_cast<uintptr_t>(gGame) + rva);
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(target, &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT ||
            (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
            return false;
        DWORD oldProtection = 0;
        if (!VirtualProtect(target, length, PAGE_READWRITE, &oldProtection))
            return false;
        std::memcpy(target, value, length);
        DWORD ignored = 0;
        VirtualProtect(target, length, oldProtection, &ignored);
        return true;
    }

    bool ApplyNewGameChapter(unsigned chapter)
    {
        if (chapter < 1 || chapter > 12)
            return false;
        const char* folder = kChapterFolders[chapter - 1];
        char withSlash[16]{};
        std::snprintf(withSlash, sizeof(withSlash), "%s\\", folder);
        char archive[20]{};
        std::snprintf(archive, sizeof(archive), "%s.str", folder);
        const bool ok =
            WriteImageText(kNewGameLevelFolderRva, withSlash,
                           std::strlen(withSlash) + 1) &&
            WriteImageText(kNewGameLevelArchiveRva, archive,
                           std::strlen(archive) + 1);
        Log("checkpoint changer: New Game level %s to %s",
            ok ? "set" : "NOT set", archive);
        return ok;
    }

    bool ApplyMainMenuCheckpoint(const Guid128& checkpoint, const char* name,
                                 const char* text, unsigned chapter)
    {
        Guid128* target = CheckpointGlobals();
        if (!target)
        {
            Log("checkpoint changer: globals are not writable");
            return false;
        }
        char* newGameText = reinterpret_cast<char*>(
            reinterpret_cast<uintptr_t>(gGame) + kNewGameCheckpointTextRva);
        MEMORY_BASIC_INFORMATION textMemory{};
        if (!VirtualQuery(newGameText, &textMemory, sizeof(textMemory)) ||
            textMemory.State != MEM_COMMIT ||
            (textMemory.Protect & (PAGE_NOACCESS | PAGE_GUARD)))
        {
            Log("checkpoint changer: New Game checkpoint text is unavailable");
            return false;
        }
        // New Game reparses this 29-character text and may overwrite the four
        // globals. Keep both representations in agreement, mirroring the DS2
        // table's proven New Game Checkpoint path. The literal lives in the
        // retail image's read-only data, so temporarily make only its bytes
        // writable and restore the original page protection immediately.
        DWORD oldProtection = 0;
        if (!VirtualProtect(newGameText, sizeof(kCheckpointChapter1InitialText),
                            PAGE_READWRITE, &oldProtection))
        {
            Log("checkpoint changer: VirtualProtect failed for New Game text "
                "(error %lu)", GetLastError());
            return false;
        }
        std::memcpy(newGameText, text, sizeof(kCheckpointChapter1InitialText));
        DWORD ignoredProtection = 0;
        if (!VirtualProtect(newGameText, sizeof(kCheckpointChapter1InitialText),
                            oldProtection, &ignoredProtection))
        {
            Log("checkpoint changer: warning: could not restore New Game text "
                "protection (error %lu)", GetLastError());
        }
        std::memcpy(target, &checkpoint, sizeof(checkpoint));
        ApplyNewGameChapter(chapter);
        Log("checkpoint changer: next New Game set to %s "
            "(%08X-%08X-%08X-%08X), source text='%s'", name,
            checkpoint.value[0], checkpoint.value[1], checkpoint.value[2],
            checkpoint.value[3], newGameText);
        return true;
    }

    struct CheckpointMenuEntry
    {
        const char* name;
        Guid128 guid;
        const char* neoUid;
        const char* note;
        unsigned chapter;
    };
#include "checkpoint_catalog_generated.h"

    LONG gCheckpointMenuVisible = FALSE;
    unsigned gCheckpointMenuSelection = kCheckpointMenuDefaultSelection;
    unsigned gCheckpointMenuScroll =
        kCheckpointMenuDefaultSelection > 5 ?
        kCheckpointMenuDefaultSelection - 5 : 0;
    constexpr unsigned kCheckpointMenuVisibleRows = 11;
    char gCheckpointMenuFeedback[160] =
        "UP/DOWN selects, ENTER/F8 applies, F9 closes";
    HFONT gCheckpointMenuFont = nullptr;
    HWND gCheckpointGameWindow = nullptr;
    LONG gCheckpointMenuKeyDown[256]{};
    LONG gCheckpointMenuKeyEdges[256]{};

    enum DebugMenuPage : unsigned
    {
        kDebugMenuCheckpoints = 0,
        kDebugMenuRandomizer = 1,
        kDebugMenuCheats = 2,
    };

    struct RandomizerMenuDraft
    {
        bool enemyRandomizer = false;
        uint32_t seed = 0x00D5A11u;
        unsigned chance = 100;
        bool crossFamily = false;
        bool globalResources = false;
        unsigned spawnsPerTrigger = 1;
        bool progressionRandomizer = false;
        unsigned preset = 0; // custom
    };

    DebugMenuPage gDebugMenuPage = kDebugMenuCheckpoints;

    // Set from the exploder settings, which are read in a file
    // included further down; the streaming policy above needs to
    // know before then whether our second exploder asset is wanted.
    bool gSuperExploderWanted = false;
    // Set once our second exploder archive is actually resident.
    // Naming a creature after an archive that never loaded would
    // leave it without a model at all, so the rename waits for this.
    bool gSuperExploderResident = false;

    // Cheats page.  The rows are drawn where the menu lives, near the top of
    // this file, but their work needs the world editor's object registry,
    // which is included much further down - hence the split.
    constexpr const char* kCheatWeapons[] = {
        "Plasma Cutter", "Line Gun", "Pulse Rifle", "Ripper",
        "Flamethrower", "Contact Beam", "Force Gun",
    };
    constexpr unsigned kCheatWeaponCount =
        sizeof(kCheatWeapons) / sizeof(kCheatWeapons[0]);
    // One row per weapon, then the read-outs, the store and the light,
    // then the suit changer (pick a level, or random) and the weapon wipe.
    constexpr unsigned kCheatMenuRows = kCheatWeaponCount + 9;
    constexpr unsigned kCheatRowSuitPick = kCheatWeaponCount + 6;
    constexpr unsigned kCheatRowSuitRandom = kCheatWeaponCount + 7;
    constexpr unsigned kCheatRowWeaponWipe = kCheatWeaponCount + 8;
    unsigned gCheatMenuSelection = 0;
    // LEFT/RIGHT on the suit row; defined behind the progression include.
    void CycleSuitMenu(int delta);
    // A ShipStore keeps its stock as 21 authored item ids spread through
    // the object.  The offsets come from its own attribute decoder at
    // 005F9F60: of its 78 properties, these are the ones written as four
    // consecutive dwords, which is what a 16-byte item id looks like.
    // Four attempts to change the shop went through the PlayerStore and
    // changed nothing; this reads the object the shop is actually built
    // from.
    constexpr unsigned kShipStoreItemSlots[] = {
        0x098u, 0x0D8u, 0x118u, 0x158u, 0x198u, 0x1C8u, 0x1D8u, 0x1E8u,
        0x208u, 0x218u, 0x228u, 0x248u, 0x258u, 0x268u, 0x288u, 0x2A8u,
        0x2B8u, 0x2C8u, 0x2E8u, 0x2F8u, 0x308u,
    };
    constexpr unsigned kShipStoreItemSlotCount =
        sizeof(kShipStoreItemSlots) / sizeof(kShipStoreItemSlots[0]);

    void FormatCheatMenuRow(unsigned row, char* text, size_t capacity);
    void ApplyCheatMenuRow(unsigned row);
    RandomizerMenuDraft gRandomizerMenuDraft{};
    unsigned gRandomizerMenuSelection = 0;
    constexpr unsigned kRandomizerMenuRows = 9;
    bool gRandomizerSeedEditing = false;
    char gRandomizerSeedEdit[11]{};
    unsigned gRandomizerSeedEditLength = 0;
    bool gRandomizerMenuLoaded = false;

    bool ParseRandomizerSeed(const char* text, uint32_t& seed)
    {
        if (!text || !*text || *text == '-' || *text == '+')
            return false;
        const bool hex = text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
        if (hex && !text[2])
            return false;
        errno = 0;
        char* end = nullptr;
        const unsigned long value = std::strtoul(text, &end, hex ? 16 : 10);
        if (errno == ERANGE || end == text || *end || value > 0xFFFFFFFFul)
            return false;
        seed = static_cast<uint32_t>(value);
        return true;
    }

    uint32_t ReadMenuSeed()
    {
        char text[64]{};
        GetPrivateProfileStringA("DeadSpace1Patch", "EnemyRandomizerSeed",
                                 "875025", text, sizeof(text), gIniPath);
        uint32_t seed = 0;
        return ParseRandomizerSeed(text, seed) ? seed : 0x00D5A11u;
    }

    void LoadRandomizerMenuDraft()
    {
        gRandomizerMenuDraft.enemyRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizer", 0, gIniPath) != 0;
        gRandomizerMenuDraft.seed = ReadMenuSeed();
        gRandomizerMenuDraft.chance = static_cast<unsigned>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerChancePercent", 100, gIniPath));
        if (gRandomizerMenuDraft.chance > 100)
            gRandomizerMenuDraft.chance = 100;
        gRandomizerMenuDraft.crossFamily = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerCrossFamilyContexts", 0,
            gIniPath) != 0;
        const bool templates = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerGlobalTemplates", 0,
            gIniPath) != 0;
        const bool streaming = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerGlobalStreaming", 0,
            gIniPath) != 0;
        gRandomizerMenuDraft.globalResources = templates && streaming;
        gRandomizerMenuDraft.spawnsPerTrigger = static_cast<unsigned>(
            GetPrivateProfileIntA("DeadSpace1Patch", "SpawnsPerTrigger", 1,
                                  gIniPath));
        if (gRandomizerMenuDraft.spawnsPerTrigger < 1)
            gRandomizerMenuDraft.spawnsPerTrigger = 1;
        if (gRandomizerMenuDraft.spawnsPerTrigger > 16)
            gRandomizerMenuDraft.spawnsPerTrigger = 16;
        gRandomizerMenuDraft.progressionRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "ProgressionRandomizer", 0, gIniPath) != 0;
        gRandomizerMenuDraft.preset = 0;
        gRandomizerMenuLoaded = true;
    }

    void ApplyRandomizerPreset(unsigned preset)
    {
        gRandomizerMenuDraft.preset = preset;
        switch (preset)
        {
        case 1: // safe enemy randomizer
            gRandomizerMenuDraft.enemyRandomizer = true;
            gRandomizerMenuDraft.chance = 100;
            gRandomizerMenuDraft.crossFamily = false;
            gRandomizerMenuDraft.globalResources = true;
            gRandomizerMenuDraft.spawnsPerTrigger = 1;
            gRandomizerMenuDraft.progressionRandomizer = false;
            break;
        case 2: // full campaign experimental
            gRandomizerMenuDraft.enemyRandomizer = true;
            gRandomizerMenuDraft.chance = 100;
            gRandomizerMenuDraft.crossFamily = true;
            gRandomizerMenuDraft.globalResources = true;
            gRandomizerMenuDraft.spawnsPerTrigger = 1;
            gRandomizerMenuDraft.progressionRandomizer = true;
            break;
        case 3: // horde randomizer
            gRandomizerMenuDraft.enemyRandomizer = true;
            gRandomizerMenuDraft.chance = 100;
            gRandomizerMenuDraft.crossFamily = true;
            gRandomizerMenuDraft.globalResources = true;
            gRandomizerMenuDraft.spawnsPerTrigger = 3;
            gRandomizerMenuDraft.progressionRandomizer = false;
            break;
        case 4: // vanilla
            gRandomizerMenuDraft.enemyRandomizer = false;
            gRandomizerMenuDraft.chance = 100;
            gRandomizerMenuDraft.crossFamily = false;
            gRandomizerMenuDraft.globalResources = false;
            gRandomizerMenuDraft.spawnsPerTrigger = 1;
            gRandomizerMenuDraft.progressionRandomizer = false;
            break;
        default:
            break;
        }
    }

    const char* RandomizerPresetName()
    {
        static const char* names[] = {
            "Custom", "Safe enemies", "Full campaign [experimental]",
            "Horde randomizer", "Vanilla"
        };
        return names[gRandomizerMenuDraft.preset <= 4 ?
                     gRandomizerMenuDraft.preset : 0];
    }

    bool WriteMenuSetting(const char* key, const char* value)
    {
        return WritePrivateProfileStringA("DeadSpace1Patch", key, value,
                                          gIniPath) != FALSE;
    }

    bool SaveRandomizerMenuDraft()
    {
        char seed[16]{};
        char chance[8]{};
        char spawns[8]{};
        std::snprintf(seed, sizeof(seed), "%u", gRandomizerMenuDraft.seed);
        std::snprintf(chance, sizeof(chance), "%u", gRandomizerMenuDraft.chance);
        std::snprintf(spawns, sizeof(spawns), "%u",
                      gRandomizerMenuDraft.spawnsPerTrigger);
        const char* enabled = "1";
        const char* disabled = "0";
        const bool ok =
            WriteMenuSetting("EnemyRandomizer",
                gRandomizerMenuDraft.enemyRandomizer ? enabled : disabled) &&
            WriteMenuSetting("EnemyRandomizerSeed", seed) &&
            WriteMenuSetting("EnemyRandomizerChancePercent", chance) &&
            WriteMenuSetting("EnemyRandomizerContextV1", enabled) &&
            WriteMenuSetting("EnemyRandomizerCrossFamilyContexts",
                gRandomizerMenuDraft.crossFamily ? enabled : disabled) &&
            WriteMenuSetting("EnemyRandomizerGlobalTemplates",
                gRandomizerMenuDraft.globalResources ? enabled : disabled) &&
            WriteMenuSetting("EnemyRandomizerGlobalStreaming",
                gRandomizerMenuDraft.globalResources ? enabled : disabled) &&
            WriteMenuSetting("SpawnsPerTrigger", spawns) &&
            WriteMenuSetting("ProgressionRandomizer",
                gRandomizerMenuDraft.progressionRandomizer ? enabled : disabled);
        Log("debug menu: randomizer configuration %s for next launch "
            "(seed=%s chance=%s cross=%d global=%d spawns=%s progression=%d)",
            ok ? "saved" : "SAVE FAILED", seed, chance,
            gRandomizerMenuDraft.crossFamily ? 1 : 0,
            gRandomizerMenuDraft.globalResources ? 1 : 0, spawns,
            gRandomizerMenuDraft.progressionRandomizer ? 1 : 0);
        return ok;
    }

    void FormatRandomizerMenuRow(unsigned row, char* text, size_t capacity)
    {
        const char* on = "ON";
        const char* off = "OFF";
        switch (row)
        {
        case 0:
            std::snprintf(text, capacity, "Preset                 %s",
                          RandomizerPresetName());
            break;
        case 1:
            std::snprintf(text, capacity, "Enemy randomizer       %s",
                          gRandomizerMenuDraft.enemyRandomizer ? on : off);
            break;
        case 2:
            if (gRandomizerSeedEditing)
                std::snprintf(text, capacity, "Enemy seed              %-10s  [typing]",
                              gRandomizerSeedEdit);
            else
                std::snprintf(text, capacity, "Enemy seed              %u",
                              gRandomizerMenuDraft.seed);
            break;
        case 3:
            std::snprintf(text, capacity, "Replacement chance      %u%%",
                          gRandomizerMenuDraft.chance);
            break;
        case 4:
            std::snprintf(text, capacity, "Cross-family contexts   %s  [experimental]",
                          gRandomizerMenuDraft.crossFamily ? on : off);
            break;
        case 5:
            std::snprintf(text, capacity, "Global models + audio   %s",
                          gRandomizerMenuDraft.globalResources ? on : off);
            break;
        case 6:
            std::snprintf(text, capacity, "Enemies per trigger     %u",
                          gRandomizerMenuDraft.spawnsPerTrigger);
            break;
        case 7:
            std::snprintf(text, capacity, "Manifest shop+schematics %s  [experimental]",
                          gRandomizerMenuDraft.progressionRandomizer ? on : off);
            break;
        case 8:
            std::snprintf(text, capacity, "SAVE FOR NEXT LAUNCH");
            break;
        default:
            text[0] = '\0';
            break;
        }
    }

    void AdjustRandomizerMenuRow(int direction)
    {
        RandomizerMenuDraft& draft = gRandomizerMenuDraft;
        if (gRandomizerMenuSelection == 0)
        {
            int preset = static_cast<int>(gRandomizerMenuDraft.preset);
            preset += direction;
            if (preset < 1)
                preset = 4;
            if (preset > 4)
                preset = 1;
            ApplyRandomizerPreset(static_cast<unsigned>(preset));
            return;
        }
        draft.preset = 0;
        switch (gRandomizerMenuSelection)
        {
        case 1:
            draft.enemyRandomizer = !draft.enemyRandomizer;
            break;
        case 2:
            draft.seed = static_cast<uint32_t>(draft.seed + direction);
            break;
        case 3:
        {
            int chance = static_cast<int>(draft.chance) + direction * 5;
            if (chance < 0)
                chance = 0;
            if (chance > 100)
                chance = 100;
            draft.chance = static_cast<unsigned>(chance);
            break;
        }
        case 4:
            draft.crossFamily = !draft.crossFamily;
            break;
        case 5:
            draft.globalResources = !draft.globalResources;
            break;
        case 6:
        {
            int count = static_cast<int>(draft.spawnsPerTrigger) + direction;
            if (count < 1)
                count = 1;
            if (count > 16)
                count = 16;
            draft.spawnsPerTrigger = static_cast<unsigned>(count);
            break;
        }
        case 7:
            draft.progressionRandomizer = !draft.progressionRandomizer;
            break;
        default:
            break;
        }
    }

    LRESULT CALLBACK CheckpointMenuWindowProc(HWND window, UINT message,
                                               WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_ERASEBKGND)
            return 1;
        if (message == WM_MOUSEACTIVATE)
            return MA_ACTIVATEANDEAT;
        if (message == WM_NCHITTEST)
            return HTCLIENT;
        if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
            message == WM_RBUTTONDOWN || message == WM_RBUTTONUP ||
            message == WM_MBUTTONDOWN || message == WM_MBUTTONUP ||
            message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL)
            return 0;
        if (message == WM_PAINT)
        {
            PAINTSTRUCT paint{};
            HDC dc = BeginPaint(window, &paint);
            RECT client{};
            GetClientRect(window, &client);
            HBRUSH background = CreateSolidBrush(RGB(15, 20, 24));
            FillRect(dc, &client, background);
            DeleteObject(background);

            HPEN border = CreatePen(PS_SOLID, 2, RGB(66, 190, 205));
            HGDIOBJ oldPen = SelectObject(dc, border);
            HGDIOBJ oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, 1, 1, client.right - 1, client.bottom - 1);
            SelectObject(dc, oldBrush);
            SelectObject(dc, oldPen);
            DeleteObject(border);

            if (!gCheckpointMenuFont)
                gCheckpointMenuFont = CreateFontA(
                    -18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
            HGDIOBJ oldFont = gCheckpointMenuFont ?
                SelectObject(dc, gCheckpointMenuFont) : nullptr;
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(228, 242, 245));
            const char* title =
                gDebugMenuPage == kDebugMenuCheckpoints ?
                    "DEAD SPACE (2008) // CHECKPOINTS" :
                gDebugMenuPage == kDebugMenuRandomizer ?
                    "DEAD SPACE (2008) // RANDOMIZER" :
                    "DEAD SPACE (2008) // CHEATS";
            TextOutA(dc, 18, 14, title,
                     static_cast<int>(std::strlen(title)));
            SetTextColor(dc, RGB(96, 220, 156));
            const char* help =
                gDebugMenuPage == kDebugMenuCheckpoints ?
                    "TAB randomizer | arrows select/chapter | ENTER/F8 apply | F9 close" :
                gDebugMenuPage == kDebugMenuRandomizer ?
                    "TAB cheats | arrows change | ENTER select/save | F9 close" :
                    "TAB checkpoints | arrows select | ENTER apply | F9 close";
            TextOutA(dc, 18, 40, help,
                     static_cast<int>(std::strlen(help)));

            if (gDebugMenuPage == kDebugMenuCheckpoints)
            {
                const unsigned end =
                    (gCheckpointMenuScroll + kCheckpointMenuVisibleRows <
                     kCheckpointMenuEntryCount) ?
                    gCheckpointMenuScroll + kCheckpointMenuVisibleRows :
                    kCheckpointMenuEntryCount;
                for (unsigned i = gCheckpointMenuScroll; i < end; ++i)
                {
                    const LONG rowIndex = static_cast<LONG>(i - gCheckpointMenuScroll);
                    RECT row{14, 70 + rowIndex * 28,
                             client.right - 14, 96 + rowIndex * 28};
                    if (i == gCheckpointMenuSelection)
                    {
                        HBRUSH selected = CreateSolidBrush(RGB(30, 63, 70));
                        FillRect(dc, &row, selected);
                        DeleteObject(selected);
                        SetTextColor(dc, RGB(125, 235, 245));
                    }
                    else
                        SetTextColor(dc, RGB(178, 198, 207));
                    char line[128]{};
                    std::snprintf(line, sizeof(line), "%c %s",
                                  i == gCheckpointMenuSelection ? '>' : ' ',
                                  kCheckpointMenuEntries[i].name);
                    TextOutA(dc, 22, row.top + 5, line,
                             static_cast<int>(std::strlen(line)));
                }

                const auto& selected =
                    kCheckpointMenuEntries[gCheckpointMenuSelection];
                SetTextColor(dc, RGB(145, 166, 177));
                char position[96]{};
                std::snprintf(position, sizeof(position),
                              "Checkpoint %u / %u | Chapter %u",
                              gCheckpointMenuSelection + 1,
                              kCheckpointMenuEntryCount, selected.chapter);
                TextOutA(dc, 18, 386, position,
                         static_cast<int>(std::strlen(position)));
                TextOutA(dc, 18, 410, selected.note,
                         static_cast<int>(std::strlen(selected.note)));
            }
            else if (gDebugMenuPage == kDebugMenuRandomizer)
            {
                for (unsigned i = 0; i < kRandomizerMenuRows; ++i)
                {
                    RECT row{14, 70 + static_cast<LONG>(i) * 34,
                             client.right - 14,
                             101 + static_cast<LONG>(i) * 34};
                    if (i == gRandomizerMenuSelection)
                    {
                        HBRUSH selected = CreateSolidBrush(RGB(30, 63, 70));
                        FillRect(dc, &row, selected);
                        DeleteObject(selected);
                        SetTextColor(dc, RGB(125, 235, 245));
                    }
                    else
                        SetTextColor(dc, RGB(178, 198, 207));
                    char value[160]{};
                    char line[176]{};
                    FormatRandomizerMenuRow(i, value, sizeof(value));
                    std::snprintf(line, sizeof(line), "%c %s",
                                  i == gRandomizerMenuSelection ? '>' : ' ',
                                  value);
                    TextOutA(dc, 22, row.top + 7, line,
                             static_cast<int>(std::strlen(line)));
                }
                SetTextColor(dc, RGB(145, 166, 177));
                const char* warning =
                    "Settings apply only after restarting the game; active saves are never remapped.";
                TextOutA(dc, 18, 410, warning,
                         static_cast<int>(std::strlen(warning)));
            }
            else
            {
                for (unsigned i = 0; i < kCheatMenuRows; ++i)
                {
                    // 28 px rows: sixteen of them fit above the notes
                    RECT row{14, 70 + static_cast<LONG>(i) * 28,
                             client.right - 14,
                             97 + static_cast<LONG>(i) * 28};
                    if (i == gCheatMenuSelection)
                    {
                        HBRUSH mark = CreateSolidBrush(RGB(30, 63, 70));
                        FillRect(dc, &row, mark);
                        DeleteObject(mark);
                        SetTextColor(dc, RGB(125, 235, 245));
                    }
                    else
                        SetTextColor(dc, RGB(178, 198, 207));
                    char value[160]{};
                    char line[176]{};
                    FormatCheatMenuRow(i, value, sizeof(value));
                    std::snprintf(line, sizeof(line), "%c %s",
                                  i == gCheatMenuSelection ? '>' : ' ',
                                  value);
                    TextOutA(dc, 22, row.top + 7, line,
                             static_cast<int>(std::strlen(line)));
                }
                SetTextColor(dc, RGB(145, 166, 177));
                const char* note =
                    "Reads the inventory into the log; writing needs the layout that read will show.";
                TextOutA(dc, 18, client.bottom - 62, note,
                         static_cast<int>(std::strlen(note)));
            }
            SetTextColor(dc, RGB(255, 181, 85));
            TextOutA(dc, 18, client.bottom - 38, gCheckpointMenuFeedback,
                     static_cast<int>(std::strlen(gCheckpointMenuFeedback)));
            if (oldFont)
                SelectObject(dc, oldFont);
            EndPaint(window, &paint);
            return 0;
        }
        return DefWindowProcA(window, message, wparam, lparam);
    }

    bool GameIsForeground()
    {
        DWORD processId = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &processId);
        return processId == GetCurrentProcessId();
    }

    bool IsCheckpointMenuKey(DWORD key)
    {
        return key == VK_F8 || key == VK_UP || key == VK_DOWN || key == VK_LEFT ||
               key == VK_RIGHT || key == VK_PRIOR || key == VK_NEXT ||
               key == VK_RETURN || key == VK_ESCAPE || key == VK_TAB ||
               key == VK_BACK || (key >= '0' && key <= '9') ||
               (key >= VK_NUMPAD0 && key <= VK_NUMPAD9);
    }

    int DecimalDigitForKey(int key)
    {
        if (key >= '0' && key <= '9')
            return key - '0';
        if (key >= VK_NUMPAD0 && key <= VK_NUMPAD9)
            return key - VK_NUMPAD0;
        return -1;
    }

    LRESULT CALLBACK CheckpointMenuKeyboardHook(int code, WPARAM wparam,
                                                LPARAM lparam)
    {
        if (code >= 0 && lparam && GameIsForeground())
        {
            const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lparam);
            // F9 belongs exclusively to the browser while the feature is
            // enabled. Once open, consume every navigation/action key on
            // both key-down and key-up so neither DirectInput nor the normal
            // frontend can observe half of the press.
            if (IsCheckpointMenuKey(key->vkCode))
            {
                const bool down = wparam == WM_KEYDOWN ||
                                  wparam == WM_SYSKEYDOWN;
                const bool up = wparam == WM_KEYUP ||
                                wparam == WM_SYSKEYUP;
                const bool capture = gCheckpointMenuVisible ||
                    InterlockedCompareExchange(
                        &gCheckpointMenuKeyDown[key->vkCode], FALSE,
                        FALSE) != FALSE;
                if (up)
                    InterlockedExchange(
                        &gCheckpointMenuKeyDown[key->vkCode], FALSE);
                else if (down && capture && InterlockedExchange(
                             &gCheckpointMenuKeyDown[key->vkCode], TRUE) == FALSE)
                    InterlockedExchange(
                        &gCheckpointMenuKeyEdges[key->vkCode], TRUE);
                if (capture)
                    return 1;
            }
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }

    DWORD WINAPI CheckpointHotkeyThread(LPVOID)
    {
        const char* className = "DS1CheckpointMenuOverlay";
        WNDCLASSEXA windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = &CheckpointMenuWindowProc;
        windowClass.hInstance = GetModuleHandleA(nullptr);
        windowClass.lpszClassName = className;
        RegisterClassExA(&windowClass);
        HWND overlay = CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            className, "", WS_POPUP, 0, 0, 720, 600,
            nullptr, nullptr, windowClass.hInstance, nullptr);
        if (!overlay)
        {
            Log("checkpoint menu: overlay creation failed (error %lu)",
                GetLastError());
            return 0;
        }
        HHOOK keyboardHook = SetWindowsHookExA(
            WH_KEYBOARD_LL, &CheckpointMenuKeyboardHook, gPlugin, 0);
        Log("checkpoint menu: keyboard capture %s",
            keyboardHook ? "active" : "FAILED");
        Log("checkpoint changer: menu ready; configured hotkey VK=0x%02X",
            gCheckpointChangerHotkey);
        bool previousKeys[256]{};
        bool hotkeyRegistered = false;
        bool overlayWasVisible = false;
        for (;;)
        {
            MSG message{};
            while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_HOTKEY && message.wParam == 1)
                {
                    if (GameIsForeground())
                    {
                        if (!gCheckpointMenuVisible)
                        {
                            HWND foregroundWindow = GetForegroundWindow();
                            if (foregroundWindow != overlay)
                                gCheckpointGameWindow = foregroundWindow;
                            LoadRandomizerMenuDraft();
                            gRandomizerSeedEditing = false;
                        }
                        gCheckpointMenuVisible = !gCheckpointMenuVisible;
                        Log("checkpoint menu: %s",
                            gCheckpointMenuVisible ? "opened" : "closed");
                    }
                    continue;
                }
                TranslateMessage(&message);
                DispatchMessageA(&message);
            }

            bool keyEdges[256]{};
            const int watchedKeys[] = {
                VK_F8, VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT,
                VK_PRIOR, VK_NEXT, VK_RETURN, VK_ESCAPE, VK_TAB, VK_BACK,
                '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
                'A', 'B', 'C', 'D', 'E', 'F',
                VK_NUMPAD0, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4,
                VK_NUMPAD5, VK_NUMPAD6, VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9};
            for (const int key : watchedKeys)
            {
                const bool queued = keyboardHook && InterlockedExchange(
                    &gCheckpointMenuKeyEdges[key], FALSE) != FALSE;
                const bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
                keyEdges[key] = queued || (down && !previousKeys[key]);
                previousKeys[key] = down;
            }

            const bool foreground = GameIsForeground();
            if (foreground && !hotkeyRegistered)
            {
                hotkeyRegistered = RegisterHotKey(
                    overlay, 1, MOD_NOREPEAT, gCheckpointChangerHotkey) != FALSE;
                Log("checkpoint menu: foreground hotkey registration %s",
                    hotkeyRegistered ? "active" : "FAILED");
            }
            else if (!foreground && hotkeyRegistered)
            {
                UnregisterHotKey(overlay, 1);
                hotkeyRegistered = false;
                gCheckpointMenuVisible = FALSE;
            }

            if (foreground && gCheckpointMenuVisible)
            {
                if (keyEdges[VK_TAB] && !gRandomizerSeedEditing)
                {
                    gDebugMenuPage = static_cast<DebugMenuPage>(
                        (gDebugMenuPage + 1) % 3);
                    std::snprintf(gCheckpointMenuFeedback,
                                  sizeof(gCheckpointMenuFeedback),
                                  gDebugMenuPage == kDebugMenuCheckpoints ?
                                  "Checkpoint browser" :
                                  gDebugMenuPage == kDebugMenuRandomizer ?
                                  "Randomizer settings - changes require restart" :
                                  "Cheats");
                    InvalidateRect(overlay, nullptr, FALSE);
                }
                if (gDebugMenuPage == kDebugMenuCheckpoints)
                {
                    if (keyEdges[VK_UP])
                    {
                        gCheckpointMenuSelection = gCheckpointMenuSelection ?
                            gCheckpointMenuSelection - 1 :
                            kCheckpointMenuEntryCount - 1;
                        InvalidateRect(overlay, nullptr, FALSE);
                    }
                    if (keyEdges[VK_DOWN])
                    {
                        gCheckpointMenuSelection =
                            (gCheckpointMenuSelection + 1) %
                            kCheckpointMenuEntryCount;
                        InvalidateRect(overlay, nullptr, FALSE);
                    }
                    if (keyEdges[VK_PRIOR])
                    {
                        gCheckpointMenuSelection =
                            gCheckpointMenuSelection > kCheckpointMenuVisibleRows ?
                            gCheckpointMenuSelection - kCheckpointMenuVisibleRows : 0;
                        InvalidateRect(overlay, nullptr, FALSE);
                    }
                    if (keyEdges[VK_NEXT])
                    {
                        const unsigned candidate = gCheckpointMenuSelection +
                                                   kCheckpointMenuVisibleRows;
                        gCheckpointMenuSelection = candidate < kCheckpointMenuEntryCount ?
                            candidate : kCheckpointMenuEntryCount - 1;
                        InvalidateRect(overlay, nullptr, FALSE);
                    }
                    if (keyEdges[VK_LEFT])
                    {
                        const unsigned chapter =
                            kCheckpointMenuEntries[gCheckpointMenuSelection].chapter;
                        while (gCheckpointMenuSelection &&
                               kCheckpointMenuEntries[gCheckpointMenuSelection - 1].chapter == chapter)
                            --gCheckpointMenuSelection;
                        if (gCheckpointMenuSelection)
                        {
                            const unsigned previousChapter =
                                kCheckpointMenuEntries[gCheckpointMenuSelection - 1].chapter;
                            while (gCheckpointMenuSelection &&
                                   kCheckpointMenuEntries[gCheckpointMenuSelection - 1].chapter == previousChapter)
                                --gCheckpointMenuSelection;
                        }
                        InvalidateRect(overlay, nullptr, FALSE);
                    }
                    if (keyEdges[VK_RIGHT])
                    {
                        const unsigned chapter =
                            kCheckpointMenuEntries[gCheckpointMenuSelection].chapter;
                        while (gCheckpointMenuSelection + 1 < kCheckpointMenuEntryCount &&
                               kCheckpointMenuEntries[gCheckpointMenuSelection].chapter == chapter)
                            ++gCheckpointMenuSelection;
                        InvalidateRect(overlay, nullptr, FALSE);
                    }
                    if (gCheckpointMenuSelection < gCheckpointMenuScroll)
                        gCheckpointMenuScroll = gCheckpointMenuSelection;
                    else if (gCheckpointMenuSelection >=
                             gCheckpointMenuScroll + kCheckpointMenuVisibleRows)
                        gCheckpointMenuScroll = gCheckpointMenuSelection -
                                                kCheckpointMenuVisibleRows + 1;
                    if (keyEdges[VK_F8] || keyEdges[VK_RETURN])
                    {
                        const auto& entry =
                            kCheckpointMenuEntries[gCheckpointMenuSelection];
                        const bool applied = ApplyMainMenuCheckpoint(
                            entry.guid, entry.name, entry.neoUid, entry.chapter);
                        std::snprintf(gCheckpointMenuFeedback,
                            sizeof(gCheckpointMenuFeedback),
                            applied ? "Applied: %s - now choose New Game"
                                    : "FAILED: see DeadSpace1Patch.log",
                            applied ? entry.name : "");
                        InvalidateRect(overlay, nullptr, FALSE);
                    }
                    if (keyEdges[VK_ESCAPE])
                        gCheckpointMenuVisible = FALSE;
                }
                else if (gRandomizerSeedEditing)
                {
                    for (int key = 0; key < 256; ++key)
                    {
                        const int digit = keyEdges[key] ? DecimalDigitForKey(key) : -1;
                        if (digit >= 0 && gRandomizerSeedEditLength < 10)
                        {
                            gRandomizerSeedEdit[gRandomizerSeedEditLength++] =
                                static_cast<char>('0' + digit);
                            gRandomizerSeedEdit[gRandomizerSeedEditLength] = '\0';
                        }
                    }
                    if (keyEdges[VK_BACK] && gRandomizerSeedEditLength)
                        gRandomizerSeedEdit[--gRandomizerSeedEditLength] = '\0';
                    if (keyEdges[VK_RETURN] || keyEdges[VK_F8])
                    {
                        uint32_t parsed = 0;
                        if (gRandomizerSeedEditLength &&
                            !ParseRandomizerSeed(gRandomizerSeedEdit, parsed))
                        {
                            std::snprintf(gCheckpointMenuFeedback,
                                          sizeof(gCheckpointMenuFeedback),
                                          "Seed must be 0..4294967295");
                        }
                        else
                        {
                            if (gRandomizerSeedEditLength)
                                gRandomizerMenuDraft.seed = parsed;
                            gRandomizerSeedEditing = false;
                            gRandomizerMenuDraft.preset = 0;
                            std::snprintf(gCheckpointMenuFeedback,
                                          sizeof(gCheckpointMenuFeedback),
                                          "Seed staged: %u",
                                          gRandomizerMenuDraft.seed);
                        }
                    }
                    if (keyEdges[VK_ESCAPE])
                        gRandomizerSeedEditing = false;
                    InvalidateRect(overlay, nullptr, FALSE);
                }
                else if (gDebugMenuPage == kDebugMenuCheats)
                {
                    if (keyEdges[VK_UP])
                        gCheatMenuSelection = gCheatMenuSelection ?
                            gCheatMenuSelection - 1 : kCheatMenuRows - 1;
                    if (keyEdges[VK_DOWN])
                        gCheatMenuSelection =
                            (gCheatMenuSelection + 1) % kCheatMenuRows;
                    if (gCheatMenuSelection == kCheatRowSuitPick)
                    {
                        if (keyEdges[VK_LEFT]) CycleSuitMenu(-1);
                        if (keyEdges[VK_RIGHT]) CycleSuitMenu(1);
                    }
                    if (keyEdges[VK_RETURN] || keyEdges[VK_F8])
                        ApplyCheatMenuRow(gCheatMenuSelection);
                    if (keyEdges[VK_ESCAPE])
                        gCheckpointMenuVisible = FALSE;
                    InvalidateRect(overlay, nullptr, FALSE);
                }
                else
                {
                    if (keyEdges[VK_UP])
                        gRandomizerMenuSelection = gRandomizerMenuSelection ?
                            gRandomizerMenuSelection - 1 : kRandomizerMenuRows - 1;
                    if (keyEdges[VK_DOWN])
                        gRandomizerMenuSelection =
                            (gRandomizerMenuSelection + 1) % kRandomizerMenuRows;
                    if (keyEdges[VK_LEFT])
                        AdjustRandomizerMenuRow(-1);
                    if (keyEdges[VK_RIGHT])
                        AdjustRandomizerMenuRow(1);
                    if (keyEdges[VK_RETURN] || keyEdges[VK_F8])
                    {
                        if (gRandomizerMenuSelection == 2)
                        {
                            gRandomizerSeedEditing = true;
                            gRandomizerSeedEditLength = 0;
                            gRandomizerSeedEdit[0] = '\0';
                            std::snprintf(gCheckpointMenuFeedback,
                                          sizeof(gCheckpointMenuFeedback),
                                          "Type up to 10 decimal digits; ENTER commits");
                        }
                        else if (gRandomizerMenuSelection == 8)
                        {
                            const bool saved = SaveRandomizerMenuDraft();
                            std::snprintf(gCheckpointMenuFeedback,
                                          sizeof(gCheckpointMenuFeedback),
                                          saved ? "Saved. Restart Dead Space to apply safely."
                                                : "SAVE FAILED - see log");
                        }
                        else
                            AdjustRandomizerMenuRow(1);
                    }
                    if (keyEdges[VK_ESCAPE])
                        gCheckpointMenuVisible = FALSE;
                    InvalidateRect(overlay, nullptr, FALSE);
                }
            }

            if (foreground && gCheckpointMenuVisible)
            {
                HWND gameWindow = gCheckpointGameWindow;
                RECT client{};
                POINT origin{};
                if (gameWindow && IsWindow(gameWindow) &&
                    GetClientRect(gameWindow, &client) &&
                    ClientToScreen(gameWindow, &origin))
                {
                    SetWindowLongPtrA(overlay, GWLP_HWNDPARENT,
                                      reinterpret_cast<LONG_PTR>(gameWindow));
                    SetWindowPos(overlay, HWND_TOPMOST, origin.x + 28,
                                 origin.y + 28, 720, 600,
                                 SWP_SHOWWINDOW);
                    if (!overlayWasVisible)
                    {
                        const BOOL foregroundSet = SetForegroundWindow(overlay);
                        const HWND focusSet = SetFocus(overlay);
                        Log("debug menu: focus capture foreground=%d focus=%s "
                            "game=%08X overlay=%08X",
                            foregroundSet ? 1 : 0,
                            focusSet == overlay ? "overlay" : "other",
                            static_cast<unsigned>(reinterpret_cast<uintptr_t>(gameWindow)),
                            static_cast<unsigned>(reinterpret_cast<uintptr_t>(overlay)));
                    }
                    InvalidateRect(overlay, nullptr, FALSE);
                }
            }
            else
            {
                ShowWindow(overlay, SW_HIDE);
                if (overlayWasVisible && GameIsForeground() &&
                    gCheckpointGameWindow && IsWindow(gCheckpointGameWindow))
                    SetForegroundWindow(gCheckpointGameWindow);
            }
            overlayWasVisible = foreground && gCheckpointMenuVisible;
            Sleep(20);
        }
    }

    // Defined with the randomizer below; the substitutions and loot amounts draw from it too.
    uint32_t StableRandomizerHash(const Guid128& source, const Guid128& authoredTarget, uint32_t salt);

    // EnemyRandomizerDifficultyRamp, read by CandidateFor and ChooseVariantSpawn.
    bool gEnemyRandomizerDifficultyRamp = true;
    // EnemyRandomizerAudioPreload: the enemy voice archives (about 19 MB) stay resident
    // for the run. 0 leaves voices to whatever the level loads, to test whether the
    // pinned banks crowd out level sounds (sirens and voices went silent, 2026-10-22).
    bool gEnemyRandomizerAudioPreload = true;
    // EnemyRandomizerDirectSpawns: directly placed spawners take part too; their
    // vent donors are placed through the spawning decoder (corpse_randomizer.inc).
    bool gEnemyRandomizerDirectSpawns = true;
    bool gEnemyRandomizerHeavyFamilies = true;
    bool gDirectPlacementWanted = false;
    unsigned gLevelPlayPosition = 0;   // 1-12 in the order played, 0 = unknown (level_order.inc)

    bool LateGameVariant(const char* variant)
    {
        static const char* const kLate[] = {"super_slasher", "super_leaper", "super_lurker",
                                            "super_brute", "twitcher", "sup_expl", "hunter"};
        if (variant)
            for (const char* late : kLate)
                if (std::strcmp(variant, late) == 0)
                    return true;
        return false;
    }

#include "progression_randomizer.inc"
#include "first_weapon.inc"
#include "loose_items.inc"
#include "store_gate.inc"
#include "suit_debt.inc"
#include "limb_randomizer.inc"
#include "log_randomizer.inc"
#include "crate_randomizer.inc"
#include "corpse_randomizer.inc"
#include "exploder_randomizer.inc"
#include "super_exploder_audio.inc"
#include "death_message_transplant.inc"
#include "crash_capture.inc"
#include "menu_label.inc"
#include "loot_amounts.inc"
// After the exploder randomizer, not before: the super exploder shipped with
// its own two settings and the variant table falls back to them, so those
// have to be declared first.  Nothing in the exploder module depends on this
// one - the variant runtime installs its own hook on the model record.
#include "variant_size_diagnostics.inc"
#include "variant_runtime.inc"
#include "slotlist_guard.inc"

    // The first randomizer stage is deliberately restricted to families that
    // share their runtime actor class and have variant resources authored by
    // the game: Zombie (Slasher/Twitcher/Super Slasher), Lurker, Leaper and
    // Brute.  Bosses, wall-bound enemies, corpse actors and scripted parts
    // never enter this cache.
    bool IsRandomizerFamily(unsigned slot)
    {
        if (slot >= gActorCount)
            return false;
        const char* name = gActorNames[slot];
        return std::strcmp(name, "Zombie") == 0 ||
               std::strcmp(name, "Lurker") == 0 ||
               std::strcmp(name, "Leaper") == 0 ||
               std::strcmp(name, "Brute") == 0;
    }

    struct LearnedTarget
    {
        Guid128 target{};
        uintptr_t ownerSpawner = 0;
        unsigned actorSlot = 0xFFFF;
        unsigned successes = 0;
    };

    // These entries are process-local on purpose.  A level unload invalidates
    // the SimManager's target registry, so candidates are rebound to a live
    // FuncSpawn before every use rather than retaining any raw resolver
    // pointer across streams or game sessions.
    constexpr unsigned kMaxLearnedTargets = 192;
    LearnedTarget gLearnedTargets[kMaxLearnedTargets]{};
    unsigned gLearnedTargetCount = 0;
    unsigned gLearnedTargetCursor = 0;
    bool gEnemyRandomizer = false;
    bool gEnemyRandomizerSourceEnabled[kMaxActorClasses]{};
    bool gEnemyRandomizerSourceFlagsLoaded = false;
    bool gEnemyRandomizerDiagnostics = false;
    bool gEnemyRandomizerGlobalTemplates = false;
    bool gEnemyRandomizerGlobalStreaming = false;
    bool gEnemyRandomizerZeroGVariants = false;
    bool gEnemyRandomizerCrossFamilyGround = false;
    bool gEnemyRandomizerContextV1 = true;
    bool gEnemyRandomizerCrossFamilyContexts = false;
    // Treat the four vent surfaces as one.  The surface classification is a
    // heuristic: the offline catalog picks the nearest VentMarker inside the
    // same archive, but in several chapters the spawners live in NPC
    // streamsets and the markers in the level archives, so no marker is ever
    // found and the chapter drops out entirely - ch02 and ch05 have zero rows
    // for that reason.  The spawn mode, by contrast, is authored data read
    // from the actor's own packet.  Gate on the mode and let the surface be
    // advisory; the Exploder emerging cleanly from a floor vent it has no
    // authored instance for is the evidence that this is safe.
    bool gEnemyRandomizerIgnoreVentSurface = true;
    bool gEnemyRandomizerRetryFailedSpawns = false;
    char gEnemyRandomizerForcedVariant[64]{};
    // Semicolon-separated variants to keep out of the pool entirely.  A
    // variant can be technically ready and still be a bad idea: Leaper is the
    // proven case, because it gets stuck in the side-wall vents that the
    // context table happily pairs it with.  Excluding it here also keeps its
    // character archive from being pinned at all.
    char gEnemyRandomizerDisabledVariants[256]{};
    // Key that writes the player's position and axes to the log, so a place
    // seen in game can be reused for a deliberate extra spawn.  0 disables it.
    int gPlacementHotkey = 0x79;   // F10
    int gWorldEditorHotkey = 0x7A; // F11, lists the world objects around you
    // Declared here rather than in world_editor.inc: the settings are read
    // long before that file is included.
    bool gWorldEditor = false;
    // Guards the unchecked player lookup that crashes on the way to the menu.
    bool gPlayerQueryGuard = true;
    bool gLevelOrderDiagnostics = false;
    bool gLevelOrderRandomizer = false;
    int gZoneProbeHotkey = 0x7B;      // F12
    unsigned gZoneProbeOffset = 0;    // 0 = nur lesen
    unsigned gZoneProbeValue = 0;
    bool gNoClip = false;
    int gNoClipHotkey = 0x76;      // F7 schaltet um
    float gNoClipSpeed = 0.5f;     // Meter je Abfrage, Umschalt beschleunigt
    // Per-variant selection weight, read from EnemyRandomizerWeight_<variant>.
    // 10 is the neutral value, so 1 makes a variant roughly ten times rarer
    // than its peers without removing it.  The Hunter is the reason this
    // exists: it is a legitimate ceiling-vent enemy but cannot be killed, so
    // meeting one should be an event, not a routine.
    struct VariantWeight
    {
        const char* variant = nullptr;
        unsigned weight = 10;
    };
    VariantWeight gVariantWeights[32]{};
    unsigned gVariantWeightCount = 0;

    unsigned WeightForVariant(const char* variant)
    {
        if (!variant)
            return 10;
        for (unsigned i = 0; i < gVariantWeightCount; ++i)
            if (std::strcmp(gVariantWeights[i].variant, variant) == 0)
                return gVariantWeights[i].weight;
        return 10;
    }

    // EnemyRandomizerDifficultyRamp: the late game's enemies start at 1/12 of
    // their weight in the first chapter played and reach it in the last. With
    // the ramp off or the chapter unknown, the weights stay as they are.
    // chapter: the spawner's own (SourceChapter), 0 = unknown, which turns the ramp off.
    unsigned SelectionWeight(const char* variant, unsigned chapter)
    {
        const unsigned weight = WeightForVariant(variant);
        if (!gEnemyRandomizerDifficultyRamp || !chapter)
            return weight;
        return weight * (LateGameVariant(variant) ? chapter : 12u);
    }

    // SelectionWeight without the ramp, on the same scale.
    unsigned RampBaseWeight(const char* variant, unsigned chapter)
    {
        const unsigned weight = WeightForVariant(variant);
        return gEnemyRandomizerDifficultyRamp && chapter ? weight * 12u : weight;
    }
    // Deliberate extra spawns.  Unlike randomization, which only redirects an
    // authored trigger, this adds an enemy that the level never had: when the
    // named source fires, the handler is called again with the target bound to
    // the chosen variant.  Direct-mode targets are the right ones here because
    // nothing has to emerge - a Guardian is placed, not spawned from a vent.
    // One line per placement, read from the [ExtraSpawns] section:
    //   spawn_zombie_01_tram_repair=guardian,1,100
    // The key is the authored spawner name (a GUID also works), the value is
    // the variant, optionally how many and how often in percent.  Naming a
    // spawner is what makes this usable without a catalogue in hand; the F10
    // readout lists the names around you.
    struct ExtraSpawnRule
    {
        Guid128 source{};
        char variant[64]{};
        unsigned count = 1;
        unsigned chance = 100;
        bool anySource = false;
        // Optional world position.  A shaft is the right place for something
        // that climbs out of one; a Guardian belongs on a wall you picked,
        // not inside the vent.  Stand where you want it, press F10, copy the
        // three numbers into the rule.
        bool hasPosition = false;
        float position[3]{};
        // Facing, in degrees around the vertical axis, as the seventh field.
        // Without it the enemy faces wherever the borrowed spawner points -
        // which is where the vent points, not where you want a Guardian to
        // look.  F10 prints your own value in exactly this notation.
        bool hasYaw = false;
        float yaw = 0.0f;
        // Eighth field, a word rather than a number: 'dead' asks for a
        // target authored in spawn mode 4, which lies in the world as a
        // corpse and gets up when the player comes close.  'vent' and
        // 'direct' force the other two; empty lets the place decide.
        // The mode is a property of the target, so this is a filter on
        // which donor is borrowed, not something we set on the enemy.
        int wantMode = -1;
    };
    ExtraSpawnRule gExtraSpawnRules[32]{};
    unsigned gExtraSpawnRuleCount = 0;

    Guid128 gExtraSpawnSource{};
    bool gExtraSpawnSourceValid = false;
    bool gExtraSpawnAnySource = false;
    unsigned gExtraSpawnChancePercent = 100;
    char gExtraSpawnVariant[64]{};
    bool gExtraSpawnDiagnostics = false;
    unsigned gExtraSpawnCount = 1;

    bool ParseGuidText(const char* text, Guid128* out)
    {
        if (!text || !out)
            return false;
        unsigned words[4]{};
        int consumed = 0;
        if (std::sscanf(text, "%8x-%8x-%8x-%8x%n", &words[0], &words[1],
                        &words[2], &words[3], &consumed) != 4)
            return false;
        for (unsigned i = 0; i < 4; ++i)
            out->value[i] = words[i];
        return true;
    }
#include "vacuum_zones.inc"
    bool gSpawnCatalog = true;
    bool gSpawnResolverDiagnostics = false;
    unsigned gEnemyRandomizerChance = 100;
    uint32_t gEnemyRandomizerSeed = 0x00D5A11u;
    long gRandomizerReplacements = 0;
    long gRandomizerFallbacks = 0;

    // Same-class variants do not need a foreign SpawnInfo. A retail Zombie
    // target already owns the trigger/vent/transform contract; its
    // RagdollComponent field selects the concrete character package. Keep
    // the chapter-owned target and redirect only that package GUID in the
    // decompressed, process-private SimGroup backing store.
    constexpr Guid128 kZombiePackageEntity{{
        0x4BC78C36u, 0x9D4F9DF9u, 0x544E4543u, 0x53574F4Bu
    }};
    constexpr Guid128 kTwitcherPackageEntity{{
        0x38C896BCu, 0xA7D8FD25u, 0x534F5254u, 0x36314E49u
    }};

    struct LocalVariantBinding
    {
        Guid128 target{};
        uintptr_t address = 0;
    };
    constexpr unsigned kMaxLocalVariantBindings = 128;
    LocalVariantBinding gLocalVariantBindings[kMaxLocalVariantBindings]{};
    unsigned gLocalVariantBindingCount = 0;

    bool WritablePrivateAddress(uintptr_t address, size_t size)
    {
        MEMORY_BASIC_INFORMATION region{};
        if (!address || !VirtualQuery(reinterpret_cast<void*>(address), &region,
                                      sizeof(region)) ||
            region.State != MEM_COMMIT || region.Type != MEM_PRIVATE ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
            return false;
        const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                               PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
        const uintptr_t end = reinterpret_cast<uintptr_t>(region.BaseAddress) +
                              region.RegionSize;
        return (region.Protect & writable) && address + size >= address &&
               address + size <= end;
    }

    bool PatchLocalZombieBinding(const Guid128& target)
    {
        for (unsigned i = 0; i < gLocalVariantBindingCount; ++i)
        {
            const LocalVariantBinding& known = gLocalVariantBindings[i];
            if (!SameGuid(known.target, target) ||
                !WritablePrivateAddress(known.address, sizeof(Guid128)))
                continue;
            __try
            {
                if (SameGuid(*reinterpret_cast<const Guid128*>(known.address),
                             kTwitcherPackageEntity))
                    return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
            }
        }

        SYSTEM_INFO system{};
        GetSystemInfo(&system);
        uintptr_t cursor = reinterpret_cast<uintptr_t>(
            system.lpMinimumApplicationAddress);
        const uintptr_t maximum = reinterpret_cast<uintptr_t>(
            system.lpMaximumApplicationAddress);
        uintptr_t bindings[8]{};
        unsigned matches = 0;

        while (cursor < maximum)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<void*>(cursor), &region,
                              sizeof(region)) || !region.RegionSize)
                break;
            const uintptr_t base = reinterpret_cast<uintptr_t>(region.BaseAddress);
            const uintptr_t end = base + region.RegionSize;
            const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                                   PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
            if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE &&
                !(region.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
                (region.Protect & writable) && region.RegionSize <= 0x10000000)
            {
                __try
                {
                    for (uintptr_t at = base; at + sizeof(Guid128) <= end; at += 4)
                    {
                        if (!SameGuid(*reinterpret_cast<const Guid128*>(at), target))
                            continue;
                        const uintptr_t nearBegin = at > base + 0x400 ?
                            at - 0x400 : base;
                        const uintptr_t nearEnd = (end - at > 0x800) ?
                            at + 0x800 : end;
                        for (uintptr_t candidate = nearBegin;
                             candidate + sizeof(Guid128) <= nearEnd;
                             candidate += 4)
                        {
                            if (!SameGuid(*reinterpret_cast<const Guid128*>(candidate),
                                          kZombiePackageEntity))
                                continue;
                            bool duplicate = false;
                            for (unsigned i = 0; i < matches; ++i)
                                if (bindings[i] == candidate)
                                    duplicate = true;
                            if (!duplicate)
                            {
                                if (matches < 8)
                                    bindings[matches] = candidate;
                                ++matches;
                            }
                        }
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    // Streaming may release a region after VirtualQuery.
                }
            }
            if (end <= cursor)
                break;
            cursor = end;
        }

        if (!matches || matches > 8)
        {
            if (gEnemyRandomizerDiagnostics)
                Log("Randomizer: local Twitcher binding unresolved for %08X-%08X-%08X-%08X (%u candidates)",
                    target.value[0], target.value[1], target.value[2],
                    target.value[3], matches);
            return false;
        }

        for (unsigned i = 0; i < matches; ++i)
        {
            const uintptr_t binding = bindings[i];
            if (!WritablePrivateAddress(binding, sizeof(Guid128)))
                return false;
            __try
            {
                std::memcpy(reinterpret_cast<void*>(binding),
                            &kTwitcherPackageEntity,
                            sizeof(kTwitcherPackageEntity));
                if (!SameGuid(*reinterpret_cast<const Guid128*>(binding),
                              kTwitcherPackageEntity))
                    return false;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
            if (gLocalVariantBindingCount < kMaxLocalVariantBindings)
                gLocalVariantBindings[gLocalVariantBindingCount++] = {
                    target, binding
                };
        }
        if (gEnemyRandomizerDiagnostics)
            Log("Randomizer: patched %u local SpawnInfo package binding copy/copies (Zombie -> Twitcher)",
                matches);
        return true;
    }

    // The randomizer needs an authored catalogue, but asking the player to
    // stop at every encounter is the wrong way to build one.  This is a
    // deliberately tiny passive recorder: one row for each unique
    // {source-UID, authored-target-UID, actor-class} observed during normal
    // gameplay.  The source UID can later be joined offline against the DS1
    // SimG table, without another in-game experiment.
    struct CatalogEntry
    {
        Guid128 source{};
        Guid128 target{};
        unsigned actorSlot = 0xFFFF;
    };
    constexpr unsigned kMaxCatalogEntries = 2048;
    CatalogEntry gCatalogEntries[kMaxCatalogEntries]{};
    unsigned gCatalogCount = 0;
    long gCatalogRows = 0;

    struct ResolverEntry
    {
        Guid128 source{};
        Guid128 target{};
    };
    constexpr unsigned kMaxResolverEntries = 512;
    ResolverEntry gResolverEntries[kMaxResolverEntries]{};
    unsigned gResolverCount = 0;

    struct GlobalEnemyTarget
    {
        Guid128 target{};
        const char* actorClass = nullptr;
        const char* variant = nullptr;
        // Authored spawn mode.  True means the actor emerges from a vent,
        // false means it is placed directly.  Every reviewed randomizer source
        // is a vent, so only vent-mode targets may be selected there; the
        // direct-mode ones exist for deliberate extra spawns, where nothing
        // has to climb out of anything.
        bool ventMode = false;
        // How the enemy arrives, read from the authored record: 1 out
        // of a vent, 3 already lying in the world as a corpse.  The
        // pool generator rewrites one vent target per variant into the
        // lying form, so this is a property of our own sidecar rather
        // than something borrowed from a far-away chapter.
        unsigned spawnMode = 0;
    };
    // The first payload value of the authored DSNPCSpawningComponent
    // record, which is what actually decides how an enemy arrives.
    // The flags word only differs as a consequence of the payload
    // being composed differently.
    constexpr unsigned kSpawnModeVent = 1;
    constexpr unsigned kSpawnModePlayingDead = 3;
    const GlobalEnemyTarget kGlobalEnemyTargets[] = {
#include "global_enemy_target_manifest.inc"
    };
    constexpr unsigned kGlobalEnemyTargetCount =
        sizeof(kGlobalEnemyTargets) / sizeof(kGlobalEnemyTargets[0]);
    bool gGlobalEnemyTargetReady[kGlobalEnemyTargetCount]{};

    struct GlobalEnemyPackageContract
    {
        const char* actorClass = nullptr;
        const char* variant = nullptr;
        Guid128 requiredTarget{};
    };
    const GlobalEnemyPackageContract kGlobalEnemyPackageContracts[] = {
#include "global_enemy_package_manifest.inc"
    };
    constexpr unsigned kGlobalEnemyPackageContractCount =
        sizeof(kGlobalEnemyPackageContracts) /
        sizeof(kGlobalEnemyPackageContracts[0]);
    bool gGlobalEnemyPoolLoaded = false;

    enum SpawnContext : uint32_t
    {
        kSpawnContextVent = 1u << 0,
        kSpawnContextGround = 1u << 1,
        kSpawnContextWallOrClimb = 1u << 2,
        kSpawnContextTubeOrShaft = 1u << 3,
        kSpawnContextFloorVent = 1u << 4,
        kSpawnContextCeiling = 1u << 5,
        kSpawnContextCloset = 1u << 6,
        kSpawnContextDoor = 1u << 7,
        // Offline VentMarker orientation and local gravity are part of the
        // spawn contract.  Keeping these as distinct bits prevents a floor
        // entry from being selected for a ceiling or side-wall FuncSpawn.
        kSpawnContextVentAngledNormal = 1u << 8,
        kSpawnContextVentCeilingNormal = 1u << 9,
        kSpawnContextVentFloorNormal = 1u << 10,
        kSpawnContextVentSideWallNormal = 1u << 11,
        // A vent whose marker orientation is unknown: matches only while
        // EnemyRandomizerIgnoreVentSurface lets the spawn mode decide.
        kSpawnContextVentUnknownSurface = 1u << 12,
        // The authored target is placed, not climbing out of a vent.
        kSpawnContextDirect = 1u << 13,
        // No random Hunter here: a death message counts kills in this room,
        // or the chapter has a Hunter of its own.
        kSpawnContextNoHunter = 1u << 14,
        // A ScriptedSequence drives the authored actor (scripted_cast.inc).
        kSpawnContextScripted = 1u << 15,
        // ... and the scene needs that very actor: the authored enemy stays.
        kSpawnContextScriptedFixed = 1u << 20,
    };
    // Bits 16-19: the spawner's chapter (build_ds1_runtime_spawn_compatibility.py).
    constexpr unsigned kSpawnContextChapterShift = 16;
    constexpr unsigned kSpawnContextAnyVent =
        kSpawnContextVentAngledNormal | kSpawnContextVentCeilingNormal |
        kSpawnContextVentFloorNormal | kSpawnContextVentSideWallNormal |
        kSpawnContextVentUnknownSurface;

    const char* SpawnContextName(uint32_t context)
    {
        switch (context & 0xFFFFu &
                ~static_cast<uint32_t>(kSpawnContextDirect | kSpawnContextNoHunter | kSpawnContextScripted))
        {
        case kSpawnContextVentUnknownSurface:
            return "vent/unknown-surface/normal-gravity";
        case 0:
            return "direct placement";
        case kSpawnContextVentAngledNormal:
            return "vent/angled/normal-gravity";
        case kSpawnContextVentCeilingNormal:
            return "vent/ceiling/normal-gravity";
        case kSpawnContextVentFloorNormal:
            return "vent/floor/normal-gravity";
        case kSpawnContextVentSideWallNormal:
            return "vent/side-wall/normal-gravity";
        case kSpawnContextVent:
            return "legacy/vent-unspecified";
        case kSpawnContextGround:
            return "legacy/ground";
        case kSpawnContextWallOrClimb:
            return "legacy/wall-or-climb";
        case kSpawnContextTubeOrShaft:
            return "legacy/tube-or-shaft";
        case kSpawnContextFloorVent:
            return "legacy/floor-vent";
        case kSpawnContextCeiling:
            return "legacy/ceiling";
        case kSpawnContextCloset:
            return "legacy/closet";
        case kSpawnContextDoor:
            return "legacy/door";
        default:
            return "unknown";
        }
    }

    struct SpawnCompatibilitySource
    {
        Guid128 source{};
        Guid128 authoredTarget{};
        const char* actorClass = nullptr;
        const char* variant = nullptr;
        uint32_t context = 0;
    };

    struct SpawnCompatibilityRule
    {
        uint32_t context = 0;
        const char* sourceClass = nullptr;
        const char* sourceVariant = nullptr;
        const char* candidateClass = nullptr;
        const char* candidateVariant = nullptr;
        bool crossClass = false;
    };

    const SpawnCompatibilitySource kSpawnCompatibilitySources[] = {
#include "spawn_compatibility_sources.inc"
    };
    constexpr unsigned kSpawnCompatibilitySourceCount =
        sizeof(kSpawnCompatibilitySources) / sizeof(kSpawnCompatibilitySources[0]);
    struct ExploderTargetMode
    {
        Guid128 target{};
        bool ventMode = false;
    };
    const ExploderTargetMode kExploderTargetModes[] = {
#include "exploder_spawn_modes.inc"
    };
    constexpr unsigned kExploderTargetModeCount =
        sizeof(kExploderTargetModes) / sizeof(kExploderTargetModes[0]);
    // The authored name of each known spawner.  A GUID in the log says which
    // spawner fired but not where it is; the name the level designer gave it
    // does, and it is the same name the offline catalog uses.
    struct SpawnSourceName
    {
        Guid128 source{};
        const char* name = nullptr;
    };
    const SpawnSourceName kSpawnSourceNames[] = {
#include "spawn_source_names.inc"
    };
    constexpr unsigned kSpawnSourceNameCount =
        sizeof(kSpawnSourceNames) / sizeof(kSpawnSourceNames[0]);

    const char* SpawnerName(const Guid128& source)
    {
        for (unsigned i = 0; i < kSpawnSourceNameCount; ++i)
            if (SameGuid(kSpawnSourceNames[i].source, source))
                return kSpawnSourceNames[i].name;
        return "unbenannt";
    }

    bool SpawnerGuidByName(const char* name, Guid128* out)
    {
        for (unsigned i = 0; i < kSpawnSourceNameCount; ++i)
            if (_stricmp(kSpawnSourceNames[i].name, name) == 0)
            {
                *out = kSpawnSourceNames[i].source;
                return true;
            }
        return false;
    }

    const SpawnCompatibilityRule kSpawnCompatibilityRules[] = {
#include "spawn_compatibility_rules.inc"
    };
    constexpr unsigned kSpawnCompatibilityRuleCount =
        sizeof(kSpawnCompatibilityRules) / sizeof(kSpawnCompatibilityRules[0]);

    // DS1 retail StreamManager bindings recovered from the decrypted PC image.
    // The constructor at 0x006C8090 publishes its object at 0x00F648E0 and
    // installs vtable 0x00C80F60.  The loader at 0x006C8B00 is the same native
    // path used by global_assets, level archives and character streams.
    constexpr uintptr_t kStreamManagerGlobal = 0x00F648E0;
    constexpr uintptr_t kStreamManagerVtable = 0x00C80F60;
    constexpr uintptr_t kStreamManagerLoad = 0x006C8B00;
    constexpr uintptr_t kSimManagerGlobal = 0x00F64930;
    constexpr ULONGLONG kGlobalStreamReadyTimeoutMs = 25000;
    struct GlobalEnemyCharacterStream
    {
        const char* path = nullptr;
        uint32_t guid = 0;
    };
    const GlobalEnemyCharacterStream kGlobalEnemyCharacterStreams[] = {
#include "global_enemy_stream_manifest.inc"
    };
    constexpr unsigned kGlobalEnemyCharacterStreamCount =
        sizeof(kGlobalEnemyCharacterStreams) / sizeof(kGlobalEnemyCharacterStreams[0]);
    struct GlobalEnemyAudioStream
    {
        const char* path = nullptr;
        uint32_t bankGuid = 0;
        const char* families = nullptr;
    };
    const GlobalEnemyAudioStream kGlobalEnemyAudioStreams[] = {
#include "global_enemy_audio_stream_manifest.inc"
        // The custom streamed subset for sup_expl. The matching payloads in
        // the character bank are built from the same donor.
        {"ds1_randomizer_assets\\sup_expl_audio", 0x2D58CA08u, "sup_expl"},
    };
    constexpr unsigned kGlobalEnemyAudioStreamCount =
        sizeof(kGlobalEnemyAudioStreams) / sizeof(kGlobalEnemyAudioStreams[0]);
    struct GlobalEnemyTargetStream
    {
        const char* path = nullptr;
        uint32_t guid = 0;
    };
    // Loose streams are looked up by path before the explicit-GUID branch.
    // Every root therefore needs a unique path as well as a unique GUID;
    // reusing one multi-entry path could alias the first registered handle.
    constexpr GlobalEnemyTargetStream kGlobalEnemyTargetStreams[] = {
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_0", 0xD5120100u},
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_1", 0xD5120101u},
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_2", 0xD5120102u},
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_3", 0xD5120103u},
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_4", 0xD5120104u},
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_5", 0xD5120105u},
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_6", 0xD5120106u},
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_7", 0xD5120107u},
        // Vent-mode donors: the side-wall Exploders and the Lurker taken from
        // ch09 barracks.  The Exploder the pool had before was a directly
        // placed one, so it spawned inside the vent and never came out.
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_8", 0xD5120108u},
        // The vent-mode Hunters from ch10 char_c; donor 6 only has the
        // directly placed one, which cannot emerge.
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_9", 0xD5120109u},
        // Five vent-mode Lurkers and five vent-mode Leapers from ch09
        // entrance; every earlier donor for both was a directly placed one.
        {"ds1_randomizer_assets\\ds1_enemy_pool_ordinary_10", 0xD512010Au},
        // Five playing-dead donors were listed here and are gone: mode 4
        // does not survive an extra spawn, and every retained stream costs
        // memory the renderer needs - sixteen archives made rooms go black.
    };
    constexpr unsigned kGlobalEnemyTargetStreamCount =
        sizeof(kGlobalEnemyTargetStreams) /
        sizeof(kGlobalEnemyTargetStreams[0]);
    // The proven loose-file registration contract uses the custom path only.
    // Bit 0 must stay clear: the retail dependency-graph branch expects a
    // native archive entry and is not valid for collision-free sidecars.
    constexpr uint8_t kStreamLoadRetainedCustom = 4u;
    // Retail archives take the native dependency/activation path, which is the
    // only one that publishes an archive's SimGroup root as a resource the
    // StreamManager can resolve.  SimManager filter 0 (0x00403B80) looks the
    // Animated reference of a spawn target up exactly there: the reference
    // hashes to the character archive's root (Twitcher 0x422EAC26, Slasher
    // 0x117742B3, Super Slasher 0x494EAD7C).  Loaded with flags 4 a retail
    // character archive still reaches state 3 and still returns a retained
    // handle, but stays invisible to that lookup, so every global target using
    // it is rejected before the factory.  Bit 0 is wrong only for the loose
    // synthetic sidecars, which have no retail TOC entry to activate.
    constexpr uint8_t kStreamLoadRetainedNative = 3u;

    using StreamLoadFn = uint32_t(SAFETYHOOK_THISCALL*)(
        void*, const char*, float, uint8_t, const uint32_t*, const char*, int);

    enum GlobalStreamPhase : LONG
    {
        kGlobalStreamIdle = 0,
        kGlobalStreamCharactersRequested = 1,
        kGlobalStreamTargetsActivated = 3,
        kGlobalStreamReady = 4,
        kGlobalStreamFailed = 5,
    };
    volatile LONG gGlobalStreamPhase = kGlobalStreamIdle;
    safetyhook::InlineHook gStreamLoadHook;
    uint32_t gGlobalCharacterHandles[kGlobalEnemyCharacterStreamCount]{};
    // The level's own archive at the last pin (CharacterArchiveOwned false), by stream index.
    bool gGlobalCharacterLent[kGlobalEnemyCharacterStreamCount]{};
    unsigned gGlobalCharacterHandleCount = 0;
    unsigned gGlobalCharacterRequiredCount = 0;
    unsigned gGlobalCharacterIdentityCount = 0;
    unsigned gGlobalCharacterFailedIndex = 0xFFFFFFFFu;
    uint32_t gGlobalAudioHandles[kGlobalEnemyAudioStreamCount]{};
    unsigned gGlobalAudioHandleCount = 0;
    unsigned gGlobalAudioNativeCount = 0;   // retail voice archives the native branch accepted
    uint32_t gSwarmCompanionHandle = 0;
    unsigned gGlobalAudioRequiredCount = 0;
    unsigned gGlobalAudioFailedIndex = 0xFFFFFFFFu;
    uint32_t gGlobalTargetHandles[kGlobalEnemyTargetStreamCount]{};
    unsigned gGlobalTargetHandleCount = 0;
    // How many donor archives this run actually set out to register.
    unsigned gGlobalTargetRequiredCount = 0;
    uint32_t gSuperExploderHandle = 0;
    uint32_t gSuperExploderLastRootStream = 0xFFFFFFFFu;
    int gSuperExploderLastRootState = -2;
    // How often the archive may be pulled back in after it dropped on its own.
    constexpr unsigned kSuperExploderReloadCeiling = 8;
    unsigned gSuperExploderReloads = 0;
    ULONGLONG gGlobalStreamRequestedTick = 0;
    thread_local bool gDispatchingGlobalStreams = false;
    bool gGlobalStreamAwaitingLevel = false;
    // Set while a restore message drives publication.  The live-spawner guard
    // below is right for the normal case - publishing into a world that is
    // being torn down is how the sidecars ended up in the dying world once -
    // but during a checkpoint restore there are no spawners yet by definition,
    // and that is exactly when the restored enemies need their targets.
    bool gRestoreDrivenPublish = false;
    // True once the pool has been usable at least once.  A lookup while this
    // is set but the pool is not currently ready is exactly the window a
    // checkpoint restore falls into; during the first level load the same
    // lookups are just our own sidecar registering and drown out the signal.
    bool gPoolWasReadyOnce = false;
    // FuncSpawn construction count at the moment the pool was lost.  A dying
    // world still has spawners that look alive, so "a live spawner exists" is
    // not proof of a new world and republishing on it puts the sidecars into
    // the world that is being torn down.  A rising construction count is
    // proof: it only happens while a new world is being built, which is also
    // the only window in which synthetic target keys register.
    long gGlobalPoolLostSpawnerMark = 0;
    ULONGLONG gGlobalPoolLostTick = 0;
    bool gGlobalCharactersPinned = false;
    // How many policy variants the pool actually reached.  A partial pool is a
    // normal, intended outcome: a variant can lack a registered key or a live
    // donor for reasons that have nothing to do with a reload.  Loss therefore
    // means "fewer than we had", never "fewer than we wanted" - comparing
    // against the full requirement made every partial pool look like a
    // checkpoint reload and restarted publication on a loop.
    unsigned gGlobalReadyVariantBaseline = 0;
    // Residency watch.  A character archive can stop resolving without any
    // call passing through our StreamManager hook, because level areas release
    // their dependencies internally.  Sampling the same lookup filter 0 uses
    // and logging only the changes shows exactly when a variant leaves the
    // pool, at four samples a second and without flooding the log.
    struct ResidencySample
    {
        uint32_t stream = 0;
        int state = -2;      // -2 = never sampled
    };
    ResidencySample gCharacterResidency[kGlobalEnemyCharacterStreamCount]{};
    bool gTargetReadyPrevious[kGlobalEnemyTargetCount]{};
    bool gTargetReadyPreviousValid = false;
    // The native load path resolves a retail archive through the dependency
    // graph, so timing decides whether it works at all.  Three boundaries were
    // measured.  Before global_assets.str the request simply returns zero.
    // Between global_assets.str and the level archive it returns a handle and
    // the loading screen then waits forever for a stream the level loader
    // never resolves.  Issued from a FuncSpawn constructor during level
    // construction it deadlocks the same way: the loader waits for the new
    // stream, and the stream request waits behind the constructor it came
    // from.  The only safe boundary is a level that has finished loading,
    // which is what the settled target sidecars below stand for.
    constexpr ULONGLONG kGlobalCharacterPreloadSettleMs = 3000;
    unsigned gGlobalCharacterPreloadAttempts = 0;
    constexpr unsigned kGlobalCharacterPreloadAttemptLimit = 64;

    // Defined in variant_runtime.inc, which is included further down
    // because it falls back to the exploder settings.
    void ResetVariantSubstitutionSequence();

    bool StillASpawner(uintptr_t spawner);
    unsigned LiveSpawnerCount();
    bool TargetOwnerIsLive(const LearnedTarget& candidate);
    bool IsLoadedSimGRoot(const Guid128& root);
    uintptr_t TargetKeyFor(const Guid128& target);
    unsigned ResidentSlotFor(const Guid128& target);
    bool ResolveStreamManager(void** managerOut);
    int StreamStateFor(void* managerObject, uint32_t streamId);
    uintptr_t StreamObjectFor(void* managerObject, uint32_t streamId);
    bool CharacterStreamRequiredByPolicy(const GlobalEnemyCharacterStream& s);
    bool FamilyListContains(const char* families, const char* value);
    uintptr_t LiveSpawnerForTarget(const Guid128& target, uintptr_t exclude);
    bool PickResidentTarget(const Guid128& original, unsigned expectedSlot,
                            uintptr_t current, Guid128* replacement);
    bool EnsureGlobalEnemyCharactersPinned(void* manager);
    void ProgressGlobalEnemyStreaming();

    bool PathMentions(const char* path, const char* needle)
    {
        const size_t length = std::strlen(needle);
        for (const char* cursor = path; *cursor; ++cursor)
            if (_strnicmp(cursor, needle, length) == 0)
                return true;
        return false;
    }

    uint32_t __fastcall StreamLoad_Hook(void* self, void*, const char* path,
                                       float priority, uint8_t flags,
                                       const uint32_t* dependencies,
                                       const char* context, int unknown)
    {
        static thread_local bool inside = false;
        if (inside || gDispatchingGlobalStreams)
            return gStreamLoadHook.thiscall<uint32_t>(
                self, path, priority, flags, dependencies, context, unknown);

        inside = true;
        const uint32_t result = gStreamLoadHook.thiscall<uint32_t>(
            self, path, priority, flags, dependencies, context, unknown);

        // While this archive is resident the loading screen is on screen.
        if (result && path && PathMentions(path, "loading_screen"))
            gLoadingScreenHandle = result;
        // The chapter archive is the last flags-3 request of a level load that
        // is not one of the global/frontend roots.  Everything the level needs
        // hangs below it, so its completion is the earliest safe moment for a
        // native character request.
        if (result && path && flags == kStreamLoadRetainedNative &&
            !PathMentions(path, "text_assets") &&
            !PathMentions(path, "frontend") &&
            !PathMentions(path, "loading_screen") &&
            !PathMentions(path, "global_assets") &&
            !PathMentions(path, "ds1_randomizer_assets"))
        {
            gLevelArchiveHandle = result;
            ResetVariantSubstitutionSequence();
        }

        // Filter 0 rejects a target when the stream that publishes its
        // "Animated" reference is not resident.  The probe reports that
        // stream only as an id, so record which archive each id belongs to.
        if (gSpawnResolverDiagnostics)
        {
            static volatile LONG loadLines = 0;
            if (InterlockedIncrement(&loadLines) <= 400)
            {
                // A retail character archive requested on the native path
                // registers its root but never advances past state 0, so the
                // remaining arguments of the game's own native calls are the
                // next thing to match exactly rather than guess.
                unsigned dependency = 0;
                if (dependencies)
                    ReadProcessMemory(GetCurrentProcess(), dependencies,
                                      &dependency, sizeof(dependency), nullptr);
                Log("stream load: id=%08X flags=%02X prio=%.3f deps=%08X[%08X] "
                    "ctx=%s arg=%d path=%s",
                    result, static_cast<unsigned>(flags), priority,
                    static_cast<unsigned>(
                        reinterpret_cast<uintptr_t>(dependencies)),
                    dependency, context ? context : "(null)", unknown,
                    path ? path : "(null)");
            }
        }

        // Save-game restore can reconstruct already-live randomized actors
        // before any FuncSpawn receives a message.  Pin character and audio
        // packages on
        // the first stable stock StreamManager call, not on the first enemy
        // spawn.  These process-lifetime leases survive checkpoint reloads;
        // only synthetic spawn-target sidecars are level-scoped.
        EnsureGlobalEnemyCharactersPinned(self);

        // The class-table worker never calls into the game.  It only gates
        // this callback until actor slots exist.  Requiring SimManager as well
        // avoids publishing target sidecars during the very earliest global
        // bootstrap, before entity registration is available.
        uintptr_t simManager = 0;
        if (gActorCount &&
            ReadProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<void*>(kSimManagerGlobal),
                              &simManager, sizeof(simManager), nullptr) &&
            simManager)
            ProgressGlobalEnemyStreaming();

        inside = false;
        return result;
    }

    unsigned ActorSlotNamed(const char* name)
    {
        for (unsigned i = 0; i < gActorCount; ++i)
            if (std::strcmp(gActorNames[i], name) == 0)
                return i;
        return 0xFFFF;
    }

    // Keep this narrow while the cross-family path is being proven. A
    // Pregnant uses an ordinary floor SpawnInfo like a Zombie, so the source
    // FuncSpawn retains the authored trigger, placement and vent behaviour.
    // Other cross-family pairs remain excluded until they have the same proof.
    bool IsCrossFamilySource(unsigned slot)
    {
        return gEnemyRandomizerCrossFamilyGround && slot < gActorCount &&
               std::strcmp(gActorNames[slot], "Pregnant") == 0;
    }

    bool IsRandomizerSource(unsigned slot)
    {
        return slot < gActorCount &&
               (!gEnemyRandomizerSourceFlagsLoaded || gEnemyRandomizerSourceEnabled[slot]) &&
               (IsRandomizerFamily(slot) || IsCrossFamilySource(slot));
    }

    unsigned ExpectedReplacementSlot(unsigned sourceSlot)
    {
        return IsCrossFamilySource(sourceSlot) ? ActorSlotNamed("Zombie") : sourceSlot;
    }

    void LogGuid(const char* prefix, const Guid128& guid, unsigned slot)
    {
        if (!gEnemyRandomizerDiagnostics)
            return;
        Log("Randomizer: %s %08X-%08X-%08X-%08X -> %s", prefix,
            guid.value[0], guid.value[1], guid.value[2], guid.value[3],
            slot < gActorCount ? gActorNames[slot] : "unknown");
    }

    void RecordSpawnCatalog(const Guid128& source, const Guid128& authoredTarget,
                            unsigned actorSlot)
    {
        if (!gSpawnCatalog || actorSlot >= gActorCount || !gSpawnCatalogPath[0])
            return;

        for (unsigned i = 0; i < gCatalogCount; ++i)
        {
            const CatalogEntry& known = gCatalogEntries[i];
            if (known.actorSlot == actorSlot && SameGuid(known.source, source) &&
                SameGuid(known.target, authoredTarget))
                return;
        }
        if (gCatalogCount >= kMaxCatalogEntries)
            return;

        gCatalogEntries[gCatalogCount++] = { source, authoredTarget, actorSlot };
        FILE* file = nullptr;
        if (fopen_s(&file, gSpawnCatalogPath, "a") || !file)
            return;
        std::fprintf(file, "%08X-%08X-%08X-%08X\t%08X-%08X-%08X-%08X\t%s\n",
                     source.value[0], source.value[1], source.value[2], source.value[3],
                     authoredTarget.value[0], authoredTarget.value[1],
                     authoredTarget.value[2], authoredTarget.value[3],
                     gActorNames[actorSlot]);
        std::fclose(file);
        const long row = InterlockedIncrement(&gCatalogRows);
        if (row <= 16 || row % 100 == 0)
            Log("catalog: recorded %ld unique authored enemy spawns", row);
    }

    void RecordResolver(const Guid128& source, const Guid128& target,
                        uintptr_t targetKey, uintptr_t resolved)
    {
        if (!gSpawnResolverDiagnostics)
            return;
        for (unsigned i = 0; i < gResolverCount; ++i)
            if (SameGuid(gResolverEntries[i].source, source) &&
                SameGuid(gResolverEntries[i].target, target))
                return;
        if (gResolverCount >= kMaxResolverEntries)
            return;
        gResolverEntries[gResolverCount++] = { source, target };

        unsigned keyWords[8]{};
        unsigned objectWords[8]{};
        if (targetKey)
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(targetKey), keyWords,
                              sizeof(keyWords), nullptr);
        if (resolved)
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(resolved), objectWords,
                              sizeof(objectWords), nullptr);
        Log("resolver: source=%08X-%08X-%08X-%08X target=%08X-%08X-%08X-%08X key=%08X "
            "keydata=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X object=%08X "
            "data=%08X,%08X,%08X,%08X,%08X,%08X,%08X,%08X",
            source.value[0], source.value[1], source.value[2], source.value[3],
            target.value[0], target.value[1], target.value[2], target.value[3],
            static_cast<unsigned>(targetKey), keyWords[0], keyWords[1], keyWords[2], keyWords[3],
            keyWords[4], keyWords[5], keyWords[6], keyWords[7], static_cast<unsigned>(resolved),
            objectWords[0], objectWords[1], objectWords[2], objectWords[3], objectWords[4],
            objectWords[5], objectWords[6], objectWords[7]);
    }

    void RememberTarget(const Guid128& target, unsigned slot, uintptr_t owner)
    {
        // A mixed FuncSpawn call can create a prop as well as an actor.  It is
        // deliberately not an entry in the randomizer catalogue: the target
        // has no single, proven actor contract in that case.
        if (!IsRandomizerSource(slot) || !owner)
            return;

        for (unsigned i = 0; i < gLearnedTargetCount; ++i)
        {
            LearnedTarget& known = gLearnedTargets[i];
            if (!SameGuid(known.target, target))
                continue;
            known.ownerSpawner = owner;
            known.actorSlot = slot;
            ++known.successes;
            return;
        }

        const unsigned index = gLearnedTargetCount < kMaxLearnedTargets
                                   ? gLearnedTargetCount++
                                   : gLearnedTargetCursor++ % kMaxLearnedTargets;
        gLearnedTargets[index] = { target, owner, slot, 1 };
        LogGuid("learned", target, slot);
    }

    unsigned LearnedSlotFor(const Guid128& target)
    {
        for (unsigned i = 0; i < gLearnedTargetCount; ++i)
            if (SameGuid(gLearnedTargets[i].target, target))
                return gLearnedTargets[i].actorSlot;
        return 0xFFFF;
    }

    const SpawnCompatibilitySource* CompatibilitySourceFor(
        const Guid128& source, const Guid128& authoredTarget)
    {
        const SpawnCompatibilitySource* match = nullptr;
        for (unsigned i = 0; i < kSpawnCompatibilitySourceCount; ++i)
        {
            const SpawnCompatibilitySource& candidate = kSpawnCompatibilitySources[i];
            if (!SameGuid(candidate.source, source) ||
                !SameGuid(candidate.authoredTarget, authoredTarget))
                continue;
            // Duplicate/conflicting offline rows fail closed.
            if (match)
                return nullptr;
            match = &candidate;
        }
        return match;
    }

    const SpawnCompatibilitySource* CompatibilityMetadataForTarget(
        const Guid128& target)
    {
        const SpawnCompatibilitySource* match = nullptr;
        for (unsigned i = 0; i < kSpawnCompatibilitySourceCount; ++i)
        {
            const SpawnCompatibilitySource& candidate = kSpawnCompatibilitySources[i];
            if (!SameGuid(candidate.authoredTarget, target))
                continue;
            if (match && (std::strcmp(match->actorClass, candidate.actorClass) != 0 ||
                          std::strcmp(match->variant, candidate.variant) != 0))
                return nullptr;
            match = &candidate;
        }
        return match;
    }

    int ExploderVentModeFor(const Guid128& target)
    {
        for (unsigned i = 0; i < kExploderTargetModeCount; ++i)
            if (SameGuid(kExploderTargetModes[i].target, target))
                return kExploderTargetModes[i].ventMode ? 1 : 0;
        return -1;
    }

    // ---------------------------------------------------------------------
    // Directly placed spawners (kSpawnContextDirect). Any of these families may
    // stand where a directly placed enemy stood: a vent donor has its arrival
    // rewritten to direct (ApplyVariantSpawnPlacement), a direct donor arrives
    // as authored. Guardians hang on walls, swarms and divider parts come in
    // groups: they never take part. Direct donors live in the large pools
    // unless the minimal sidecars are installed (pool 2 alone carries 327
    // entities of chapter six's scripting), so they wait for those.
    // ---------------------------------------------------------------------
#include "minimal_sidecars_generated.inc"
    bool gMinimalSidecars = false;

    void DetectMinimalSidecars()
    {
        unsigned matching = 0;
        for (const MinimalSidecar& sidecar : kMinimalSidecars)
        {
            char path[MAX_PATH]{};
            std::snprintf(path, sizeof(path), "%sds1_randomizer_assets\\%s", gGameRoot, sidecar.name);
            WIN32_FILE_ATTRIBUTE_DATA info{};
            if (GetFileAttributesExA(path, GetFileExInfoStandard, &info) && !info.nFileSizeHigh &&
                info.nFileSizeLow == sidecar.size)
                ++matching;
        }
        gMinimalSidecars = matching == kMinimalSidecarCount;
        Log("randomizer sidecars: %u of %u match the minimal build, so directly placed donors are %s",
            matching, kMinimalSidecarCount,
            gMinimalSidecars ? "usable" : "left out (trainer/ds1_install.py --install --minimal-assets)");
    }

    bool DirectCandidateClass(const char* actorClass)
    {
        static const char* const kClasses[] = {"Zombie", "Lurker", "Leaper", "Exploder", "Hunter",
                                               "Pregnant", "Infector", "Brute"};
        if (actorClass)
            for (const char* name : kClasses)
                if (std::strcmp(actorClass, name) == 0)
                    return true;
        return false;
    }

    // No vent instance anywhere in the game: only direct donors, and their own
    // character archives and banks.
    bool HeavyFamily(const char* actorClass)
    {
        return actorClass && (std::strcmp(actorClass, "Pregnant") == 0 ||
                              std::strcmp(actorClass, "Infector") == 0 ||
                              std::strcmp(actorClass, "Brute") == 0);
    }

    // Whether a directly placed spawner of this class exists at all; without
    // cross-family replacement only those can choose the class.
    bool DirectSourceOfClass(const char* actorClass)
    {
        for (unsigned i = 0; i < kSpawnCompatibilitySourceCount; ++i)
        {
            const SpawnCompatibilitySource& source = kSpawnCompatibilitySources[i];
            if ((source.context & kSpawnContextDirect) && source.actorClass &&
                std::strcmp(source.actorClass, actorClass) == 0)
                return true;
        }
        return false;
    }

    bool DirectVariantEnabled(const char* actorClass, const char* variant)
    {
        if (!gEnemyRandomizerDirectSpawns || !actorClass || !variant ||
            std::strcmp(variant, "unknown") == 0 || !DirectCandidateClass(actorClass))
            return false;
        // Our own exploder look is resident only with EnhancedExploders.
        if (std::strcmp(variant, "sup_expl") == 0 && !gSuperExploderWanted)
            return false;
        if (HeavyFamily(actorClass) && (!gEnemyRandomizerHeavyFamilies || !gMinimalSidecars))
            return false;
        return gEnemyRandomizerCrossFamilyContexts || DirectSourceOfClass(actorClass);
    }

    bool DirectReplacement(const SpawnCompatibilitySource& source, const GlobalEnemyTarget& candidate)
    {
        if (!(source.context & kSpawnContextDirect) || !source.actorClass || !source.variant ||
            !DirectVariantEnabled(candidate.actorClass, candidate.variant))
            return false;
        if (!candidate.ventMode && !gMinimalSidecars)
            return false;
        if (std::strcmp(candidate.actorClass, source.actorClass) == 0)
            return std::strcmp(candidate.variant, source.variant) != 0;
        return gEnemyRandomizerCrossFamilyContexts;
    }

    bool CompatibleReplacement(const SpawnCompatibilitySource& source,
                               const char* candidateClass,
                               const char* candidateVariant)
    {
        if (!candidateClass || !candidateVariant || !source.actorClass ||
            !source.variant)
            return false;
        for (unsigned i = 0; i < kSpawnCompatibilityRuleCount; ++i)
        {
            const SpawnCompatibilityRule& rule = kSpawnCompatibilityRules[i];
            const bool contextMatches =
                gEnemyRandomizerIgnoreVentSurface
                    ? ((rule.context & kSpawnContextAnyVent) != 0 &&
                       (source.context & kSpawnContextAnyVent) != 0)
                    : ((rule.context & source.context) != 0);
            if (!contextMatches ||
                std::strcmp(rule.sourceClass, source.actorClass) != 0 ||
                std::strcmp(rule.sourceVariant, source.variant) != 0 ||
                std::strcmp(rule.candidateClass, candidateClass) != 0 ||
                std::strcmp(rule.candidateVariant, candidateVariant) != 0)
                continue;
            return !rule.crossClass || gEnemyRandomizerCrossFamilyContexts;
        }
        return false;
    }

    bool PolicyUsesGlobalVariant(const GlobalEnemyTarget& candidate)
    {
        if (!candidate.actorClass || !candidate.variant ||
            std::strcmp(candidate.variant, "unknown") == 0)
            return false;
        if (FamilyListContains(gEnemyRandomizerDisabledVariants, candidate.variant) ||
            WeightForVariant(candidate.variant) == 0)
            return false;
        if (gEnemyRandomizerForcedVariant[0] &&
            std::strcmp(candidate.variant,
                        gEnemyRandomizerForcedVariant) != 0)
            return false;
        if (DirectVariantEnabled(candidate.actorClass, candidate.variant) &&
            (candidate.ventMode || gMinimalSidecars))
            return true;
        for (unsigned i = 0; i < kSpawnCompatibilityRuleCount; ++i)
        {
            const SpawnCompatibilityRule& rule = kSpawnCompatibilityRules[i];
            if (std::strcmp(rule.candidateClass, candidate.actorClass) == 0 &&
                std::strcmp(rule.candidateVariant, candidate.variant) == 0 &&
                (!rule.crossClass || gEnemyRandomizerCrossFamilyContexts))
                return true;
        }
        return false;
    }

    bool CandidateVariantEnabled(const char* actorClass, const char* variant)
    {
        if (!actorClass || !variant)
            return false;
        if (gEnemyRandomizerForcedVariant[0] &&
            std::strcmp(variant, gEnemyRandomizerForcedVariant) != 0)
            return false;
        if (FamilyListContains(gEnemyRandomizerDisabledVariants, variant))
            return false;
        if (WeightForVariant(variant) == 0)
            return false;
        if (DirectVariantEnabled(actorClass, variant))
            return true;
        for (unsigned i = 0; i < kSpawnCompatibilityRuleCount; ++i)
        {
            const SpawnCompatibilityRule& rule = kSpawnCompatibilityRules[i];
            if (std::strcmp(rule.candidateClass, actorClass) == 0 &&
                std::strcmp(rule.candidateVariant, variant) == 0 &&
                (!rule.crossClass || gEnemyRandomizerCrossFamilyContexts))
                return true;
        }
        return false;
    }

    const GlobalEnemyPackageContract* PackageContractFor(
        const char* actorClass, const char* variant)
    {
        for (unsigned i = 0; i < kGlobalEnemyPackageContractCount; ++i)
        {
            const GlobalEnemyPackageContract& package =
                kGlobalEnemyPackageContracts[i];
            if (std::strcmp(package.actorClass, actorClass) == 0 &&
                std::strcmp(package.variant, variant) == 0)
                return &package;
        }
        return nullptr;
    }

    // The archive root hash the game will look up for this variant.  It is the
    // same value filter 0 derives from the packet's "Animated" reference, and
    // it already lives in the character stream manifest.
    uint32_t CharacterRootFor(const char* variant)
    {
        for (unsigned i = 0; i < kGlobalEnemyCharacterStreamCount; ++i)
        {
            const GlobalEnemyCharacterStream& stream =
                kGlobalEnemyCharacterStreams[i];
            const char* base = std::strrchr(stream.path, '\\');
            base = base ? base + 1 : stream.path;
            if (_stricmp(base, variant) == 0)
                return stream.guid;
        }
        return 0;
    }

    // Retail 0x006CA7A0: which stream publishes this resource.  `this` arrives
    // in EDX and the hash on the stack, so the second __fastcall argument is
    // the object.  Zero means no loaded stream provides it.
    using StreamResourceFn = uint32_t(__fastcall*)(void*, void*, uint32_t);
    constexpr uintptr_t kStreamResourceLookup = 0x006CA7A0;

    uint32_t ResourceStreamIdFor(void* manager, uint32_t rootHash)
    {
        if (!manager || !rootHash)
            return 0;
        const auto lookup =
            reinterpret_cast<StreamResourceFn>(kStreamResourceLookup);
        return lookup(nullptr, manager, rootHash);
    }

    void RefreshSuperExploderResidency(void* manager)
    {
        if (!gSuperExploderWanted || !gSuperExploderHandle || !manager)
            return;
        constexpr uint32_t kSuperExploderRoot = 0xD5100010u;
        const uint32_t rootStream =
            ResourceStreamIdFor(manager, kSuperExploderRoot);
        const int rootState =
            rootStream ? StreamStateFor(manager, rootStream) : -1;
        const bool resident = rootStream && rootState == 3;
        gSuperExploderResident = resident;
        if (rootStream == gSuperExploderLastRootStream &&
            rootState == gSuperExploderLastRootState)
            return;
        Log("super exploder residency: handle=%08X state=%d | root=%08X -> "
            "stream=%08X state=%d (%s)", gSuperExploderHandle,
            StreamStateFor(manager, gSuperExploderHandle), kSuperExploderRoot,
            rootStream, rootState, resident ? "ready" : "not ready");
        const bool wasResident = gSuperExploderLastRootState == 3;
        gSuperExploderLastRootStream = rootStream;
        gSuperExploderLastRootState = rootState;

        // The archive can die on its own while the target keys survive.
        //
        // A checkpoint restart rebuilds the level, and the synthetic target
        // keys come back with it - the reject line even counts four of them -
        // but our retained lease on the character archive does not.  The loss
        // detection in ProgressGlobalEnemyStreaming only watches those keys,
        // so nothing noticed: the stale handle stayed, no new request went
        // out, and every substitution fell back to the authored Exploder.
        //
        // Measured 2026-09-08: requested at 12:07:20 as handle 3B, reached
        // state 3, and at 12:07:33 the root resolved to stream 0 with state
        // -1.  From then on the log reads "no usable target" for the rest of
        // the life.
        //
        // So the archive gets its own recovery.  It is a plain retained
        // stream rather than a level-owned key, and the same call that world
        // construction makes works here too; the request is only made while a
        // level is actually up, and a small ceiling keeps a permanently
        // failing load from retrying forever.
        if (resident || !wasResident)
            return;
        if (gSuperExploderReloads >= kSuperExploderReloadCeiling)
            return;
        if (!gLevelArchiveHandle || StreamStateFor(manager, gLevelArchiveHandle) != 3)
            return;
        const auto load = reinterpret_cast<StreamLoadFn>(kStreamManagerLoad);
        const uint32_t handle = load(
            manager, "ds1_randomizer_assets\\sup_expl", 0.0f,
            kStreamLoadRetainedCustom, &kSuperExploderRoot, gGameRoot, 0);
        ++gSuperExploderReloads;
        if (handle)
        {
            gSuperExploderHandle = handle;
            gSuperExploderLastRootStream = 0xFFFFFFFFu;
            gSuperExploderLastRootState = -2;
        }
        Log("super exploder archive dropped out; re-requested (#%u): handle=%08X",
            gSuperExploderReloads, handle);
    }

    void LogSuperExploderVisualResidency()
    {
        if (!gSpawnResolverDiagnostics)
            return;
        static unsigned reports = 0;
        if (reports++ >= 6)
            return;

        struct VisualResource
        {
            const char* name;
            uint32_t id;
        };
        constexpr VisualResource resources[] = {
            {"root", 0xD5100010u},
            {"metamodel", 0x69315FE6u},
            {"rcb", 0x0AAFDD76u},
            {"material_cap", 0xB7E819A9u},
            {"material_arm", 0xB1C35D06u},
            {"material_body", 0xDF7AF71Eu},
            {"texture_arm_c", 0x453F3C82u},
            {"texture_body_c", 0x6E66E101u},
            {"texture_cap_c", 0x0F7D4F4Bu},
        };

        void* manager = nullptr;
        if (!ResolveStreamManager(&manager))
        {
            Log("super exploder visual residency: StreamManager unavailable");
            return;
        }
        unsigned ready = 0;
        for (const VisualResource& resource : resources)
        {
            const uint32_t stream = ResourceStreamIdFor(manager, resource.id);
            const int state = stream ? StreamStateFor(manager, stream) : -1;
            if (stream && state == 3)
                ++ready;
            else
                Log("super exploder visual resource missing: %s id=%08X "
                    "stream=%08X state=%d", resource.name, resource.id,
                    stream, state);
        }
        Log("super exploder visual residency: %u/%u critical resources ready",
            ready, static_cast<unsigned>(_countof(resources)));
    }

    // Which visual resources does the finished creature actually name?
    //
    // The stream binding is already proven correct - a Super Exploder holds
    // our archive's handle at +0x2D8 and never the retail one - and every
    // link inside the archive checks out offline: the root names our RCB and
    // MetaModel, all 37 meshes carry rewritten material keys, the materials
    // point at our textures, and the body texture really is black.  Yet it
    // renders retail.  So the question is no longer whether the archive is
    // right; it is which resources the object ends up holding.
    //
    // Same technique that found the stream handle: scan the object and report
    // every hit, ours against retail, with the offset.  The ids come from the
    // two archive manifests and are stable per build.
    // Small helper so the two offsets can be printed in one line without a
    // second buffer at every call site.
    const char* FormatOffset(int offset)
    {
        static char text[4][16];
        static unsigned slot = 0;
        slot = (slot + 1) % 4;
        std::snprintf(text[slot], sizeof(text[slot]), "+%X", offset);
        return text[slot];
    }

    struct VisualResourceProbe
    {
        uint32_t retail;
        uint32_t ours;
        const char* what;
    };
    constexpr VisualResourceProbe kVisualProbes[] = {
        {0xEDBBC721u, 0x69315FE6u, "MetaModel"},
        {0xCFA15CACu, 0x0AAFDD76u, "RCB"},
        {0xC599FABDu, 0xDF7AF71Eu, "material body"},
        {0x2A742601u, 0xB1C35D06u, "material arm"},
        {0x2B5F40D7u, 0xB7E819A9u, "material cap"},
    };

    void LogExploderVisualResources(uintptr_t object, bool expectedSuper)
    {
        if (!gSpawnResolverDiagnostics || !object)
            return;
        static unsigned reports = 0;
        if (reports++ >= 6)
            return;
        unsigned char bytes[0x700]{};
        SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<const void*>(object), bytes,
                               sizeof(bytes), &read))
            return;
        for (const VisualResourceProbe& probe : kVisualProbes)
        {
            int retailAt = -1;
            int oursAt = -1;
            for (unsigned offset = 0;
                 offset + sizeof(uint32_t) <= read; offset += sizeof(uint32_t))
            {
                uint32_t value = 0;
                std::memcpy(&value, bytes + offset, sizeof(value));
                if (value == probe.retail && retailAt < 0)
                    retailAt = static_cast<int>(offset);
                if (value == probe.ours && oursAt < 0)
                    oursAt = static_cast<int>(offset);
            }
            if (retailAt < 0 && oursAt < 0)
                continue;
            // The offset is the lead: whoever writes the retail id into the
            // object writes it there, and that is the place to look next.
            Log("exploder visual: object=%08X expected=%s %s retail@%s ours@%s",
                static_cast<unsigned>(object),
                expectedSuper ? "super" : "normal", probe.what,
                retailAt < 0 ? "none" : FormatOffset(retailAt),
                oursAt < 0 ? "none" : FormatOffset(oursAt));
        }
    }

    void LogExploderStreamBindings(uintptr_t object, bool expectedSuper)
    {
        if (!gSpawnResolverDiagnostics || !object)
            return;
        static unsigned reports = 0;
        if (reports++ >= 8)
            return;

        void* manager = nullptr;
        if (!ResolveStreamManager(&manager))
            return;
        const uint32_t normalStream =
            ResourceStreamIdFor(manager, 0xBD951E79u);
        const uint32_t superStream =
            ResourceStreamIdFor(manager, 0xD5100010u);
        unsigned char bytes[0x700]{};
        SIZE_T read = 0;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<const void*>(object), bytes,
                               sizeof(bytes), &read))
            return;

        char normalOffsets[160]{};
        char superOffsets[160]{};
        unsigned normalUsed = 0;
        unsigned superUsed = 0;
        for (unsigned offset = 0;
             offset + sizeof(uint32_t) <= read; offset += sizeof(uint32_t))
        {
            uint32_t value = 0;
            std::memcpy(&value, bytes + offset, sizeof(value));
            if (normalStream && value == normalStream &&
                normalUsed + 12 < sizeof(normalOffsets))
                normalUsed += std::snprintf(
                    normalOffsets + normalUsed,
                    sizeof(normalOffsets) - normalUsed, "%s+%03X",
                    normalUsed ? "," : "", offset);
            if (superStream && value == superStream &&
                superUsed + 12 < sizeof(superOffsets))
                superUsed += std::snprintf(
                    superOffsets + superUsed,
                    sizeof(superOffsets) - superUsed, "%s+%03X",
                    superUsed ? "," : "", offset);
        }
        Log("exploder stream binding: object=%08X expected=%s normal=%08X "
            "at=%s super=%08X at=%s", static_cast<unsigned>(object),
            expectedSuper ? "super" : "normal", normalStream,
            normalOffsets[0] ? normalOffsets : "none", superStream,
            superOffsets[0] ? superOffsets : "none");
    }

    bool PackageReadyFor(const char* actorClass, const char* variant)
    {
        const GlobalEnemyPackageContract* package =
            PackageContractFor(actorClass, variant);
        if (!package || !gGlobalCharactersPinned)
            return false;

        // A retained handle is not residency.  When the level already had the
        // archive loaded our request joins that existing stream instead of
        // creating one, and the handle does not survive the level releasing it
        // - measured with Leaper, which was resident at pin time (id 0x17,
        // state 3) and gone by the time it was offered.  The old check only
        // asked whether Load had once returned a handle, so the randomizer
        // kept offering Leapers and lost every one of them at filter 0.  Ask
        // exactly what the game asks instead.
        const uint32_t root = CharacterRootFor(variant);
        if (!root)
            return true;   // root still unknown: keep the old, weaker contract
        void* manager = nullptr;
        if (!ResolveStreamManager(&manager))
            return false;
        const uint32_t stream = ResourceStreamIdFor(manager, root);
        return stream && StreamStateFor(manager, stream) == 3;
    }

    // Who created the stream that holds a variant's character archive: the
    // request priority is stored at creation (0x006C979E).  Ours ask with 0,
    // the level with 75.  An archive the level created only lent us its
    // handle; when the level drops it, every actor made from it goes too
    // (ch01: two Leapers vanished when the lounge streamed out).
    bool CharacterArchiveOwned(const char* variant)
    {
        const uint32_t root = CharacterRootFor(variant);
        if (!root)
            return true;
        void* manager = nullptr;
        if (!ResolveStreamManager(&manager))
            return false;
        const uintptr_t stream = StreamObjectFor(manager, ResourceStreamIdFor(manager, root));
        float priority = -1.0f;
        return stream &&
               ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(stream + 0x3C), &priority,
                                 sizeof(priority), nullptr) &&
               priority == 0.0f;
    }

    // A variant whose archive was the level's at the last pin is never offered, so the
    // level dropping that archive costs the pool nothing.  Counting it made every such
    // drop a full restream of 15 character and 22 audio archives (1-2 s stalls in ch01,
    // zombievar1 at 22:54 and 23:00 on 2026-10-21).
    bool CharacterArchiveLentAtPin(const char* variant)
    {
        for (unsigned i = 0; i < kGlobalEnemyCharacterStreamCount; ++i)
        {
            const char* base = std::strrchr(kGlobalEnemyCharacterStreams[i].path, '\\');
            base = base ? base + 1 : kGlobalEnemyCharacterStreams[i].path;
            if (_stricmp(base, variant) == 0)
                return gGlobalCharacterLent[i];
        }
        return false;
    }

    bool CandidateRuntimeReady(const GlobalEnemyTarget& candidate)
    {
        if (!PackageReadyFor(candidate.actorClass, candidate.variant))
            return false;
        const unsigned expected = ActorSlotNamed(candidate.actorClass);
        // Synthetic sidecars publish their SpawnInfo key before an actor is
        // instantiated. In that state key+0x14 is deliberately null and the
        // class contract lives at key+0x28. ResidentSlotFor validates either
        // that pre-spawn signature or an existing object's exact vtable.
        const uintptr_t key = TargetKeyFor(candidate.target);
        uintptr_t object = 0;
        if (expected >= gActorCount || !key ||
            ResidentSlotFor(candidate.target) != expected ||
            !ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(key + 0x14), &object,
                               sizeof(object), nullptr))
            return false;

        // If the game has already materialised the actor, its common runtime
        // root must be linked. Pre-spawn candidates instead require the
        // sidecar's matching FuncSpawn donor, which supplies the complete
        // authored SpawnInfo packet when the trigger is rebound.
        if (object)
        {
            uintptr_t runtimeRoot = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(object + 0x268),
                                   &runtimeRoot, sizeof(runtimeRoot), nullptr) ||
                !runtimeRoot)
                return false;
        }
        return LiveSpawnerForTarget(candidate.target, 0) != 0;
    }

    unsigned RefreshGlobalTargetReadiness()
    {
        unsigned ready = 0;
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
        {
            gGlobalEnemyTargetReady[i] =
                CandidateRuntimeReady(kGlobalEnemyTargets[i]);
            if (gGlobalEnemyTargetReady[i])
                ++ready;
        }
        // Which individual target came or went, and why it is unusable now.
        // Only transitions are logged; a steady pool writes nothing.
        if (gEnemyRandomizerDiagnostics)
        {
            for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
            {
                if (gTargetReadyPreviousValid &&
                    gTargetReadyPrevious[i] == gGlobalEnemyTargetReady[i])
                    continue;
                if (!gTargetReadyPreviousValid && !gGlobalEnemyTargetReady[i])
                    continue;
                const GlobalEnemyTarget& target = kGlobalEnemyTargets[i];
                const uintptr_t key = TargetKeyFor(target.target);
                Log("target %s/%s %08X-%08X: %s (key=%08X slot=%u donor=%08X "
                    "package=%s)",
                    target.actorClass, target.variant, target.target.value[0],
                    target.target.value[1],
                    gGlobalEnemyTargetReady[i] ? "usable" : "unusable",
                    static_cast<unsigned>(key),
                    key ? ResidentSlotFor(target.target) : 0xFFFFu,
                    static_cast<unsigned>(
                        LiveSpawnerForTarget(target.target, 0)),
                    PackageReadyFor(target.actorClass, target.variant)
                        ? "ready" : "MISSING");
            }
            std::memcpy(gTargetReadyPrevious, gGlobalEnemyTargetReady,
                        sizeof(gTargetReadyPrevious));
            gTargetReadyPreviousValid = true;
        }
        return ready;
    }

    // One full picture of every published target the first time the pool goes
    // ready.  The transition log only reports changes, which hides targets that
    // were never usable in the first place - and "never got a SimManager key"
    // is exactly the state we cannot explain from the files alone.
    void LogEveryTargetOnce()
    {
        static bool done = false;
        if (done || !gEnemyRandomizerDiagnostics)
            return;
        done = true;
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
        {
            const GlobalEnemyTarget& target = kGlobalEnemyTargets[i];
            const uintptr_t key = TargetKeyFor(target.target);
            uintptr_t object = 0;
            if (key)
                ReadProcessMemory(GetCurrentProcess(),
                                  reinterpret_cast<void*>(key + 0x14), &object,
                                  sizeof(object), nullptr);
            Log("target census %08X-%08X %s/%s %s: key=%08X object=%08X "
                "slot=%u donor=%08X package=%s",
                target.target.value[0], target.target.value[1],
                target.actorClass, target.variant,
                target.ventMode ? "vent" : "direct",
                static_cast<unsigned>(key), static_cast<unsigned>(object),
                key ? ResidentSlotFor(target.target) : 0xFFFFu,
                static_cast<unsigned>(LiveSpawnerForTarget(target.target, 0)),
                PackageReadyFor(target.actorClass, target.variant) ? "ready"
                                                                  : "missing");
        }
    }

    // Sampled from the heartbeat; writes a line only when a character archive
    // appears, disappears or changes state.
    void LogCharacterResidencyChanges()
    {
        if (!gEnemyRandomizerDiagnostics || !gGlobalCharactersPinned)
            return;
        void* manager = nullptr;
        if (!ResolveStreamManager(&manager))
            return;
        for (unsigned i = 0; i < kGlobalEnemyCharacterStreamCount; ++i)
        {
            const GlobalEnemyCharacterStream& archive =
                kGlobalEnemyCharacterStreams[i];
            if (!archive.guid || !CharacterStreamRequiredByPolicy(archive))
                continue;
            const uint32_t stream = ResourceStreamIdFor(manager, archive.guid);
            const int state = stream ? StreamStateFor(manager, stream) : -1;
            // The pin looks at our own archive once, while it is still
            // loading, so the flag has to be kept current here or the rename
            // never happens at all.
            if (archive.guid == 0xD5100010u)
            {
                const bool resident = state == 3;
                if (resident != gSuperExploderResident)
                    Log("super exploder asset is now %s",
                        resident ? "resident" : "gone");
                gSuperExploderResident = resident;
            }
            ResidencySample& previous = gCharacterResidency[i];
            if (previous.stream == stream && previous.state == state)
                continue;
            const char* base = std::strrchr(archive.path, '\\');
            Log("residency: %s root=%08X stream %08X(state %d) -> %08X(state %d)",
                base ? base + 1 : archive.path, archive.guid, previous.stream,
                previous.state, stream, state);
            previous.stream = stream;
            previous.state = state;
        }
    }

    void ClearGlobalTargetReadiness()
    {
        std::memset(gGlobalEnemyTargetReady, 0,
                    sizeof(gGlobalEnemyTargetReady));
    }

    // Can any enabled feature choose this donor target?  The same question
    // CharacterStreamRequiredByPolicy asks about character archives, asked
    // about the targets, so the donor pools follow the same rule instead of
    // all eleven being pinned whatever is switched on.
    bool GlobalTargetSelectable(const GlobalEnemyTarget& target)
    {
        if (gSuperExploderWanted && target.variant &&
            _stricmp(target.variant, "sup_expl") == 0)
        {
            // Vent-only leaves the direct-mode donor out, and that donor is
            // the expensive one: pool 2 carries 327 entities - sixty
            // ScriptedSequences, fifty-two MultiManagers, fifty-eight
            // ExecuteVFX and the rest of chapter six's scripting - for a
            // single Exploder, while the vent donor carries three Exploders
            // in seventy-eight.  Worse, pool 2 comes from filtration_z01, so
            // in chapter six the world ends up holding a second copy of its
            // own script objects.  Those classes register as message
            // receivers, and the teardown that crashed walks a receiver list.
            // Only the vent donors.  A direct spawn borrows one of them and
            // has its arrival kind rewritten, so the expensive direct-mode
            // pool never has to enter the world at all.
            return target.ventMode;
        }
        if (gExtraSpawnVariant[0] && target.variant &&
            _stricmp(target.variant, gExtraSpawnVariant) == 0)
            return true;
        for (unsigned i = 0; i < gExtraSpawnRuleCount; ++i)
            if (target.variant &&
                _stricmp(target.variant, gExtraSpawnRules[i].variant) == 0)
                return true;
        if (!gEnemyRandomizer)
            return false;
        return CandidateVariantEnabled(target.actorClass, target.variant);
    }

    bool CharacterStreamRequiredByPolicy(
        const GlobalEnemyCharacterStream& stream)
    {
        const char* base = std::strrchr(stream.path, '\\');
        base = base ? base + 1 : stream.path;
        // An extra-spawn variant needs its character archive resident just
        // like a policy variant: filter 0 checks it either way.
        if (gExtraSpawnVariant[0] && _stricmp(base, gExtraSpawnVariant) == 0)
            return true;
        // Our own second exploder look.  It is not a retail package, so no
        // contract covers it; it is required exactly when the feature that
        // uses it is on.
        if (gSuperExploderWanted && _stricmp(base, "sup_expl") == 0)
            return true;
        for (unsigned i = 0; i < gExtraSpawnRuleCount; ++i)
            if (_stricmp(base, gExtraSpawnRules[i].variant) == 0)
                return true;
        // Candidate packages belong to the full enemy randomizer. Loading
        // them while only a standalone feature is enabled can saturate the
        // level loader with archives that no active policy can ever select.
        if (!gEnemyRandomizer)
            return false;
        bool zombieBaseRequired = false;
        for (unsigned i = 0; i < kGlobalEnemyPackageContractCount; ++i)
        {
            const GlobalEnemyPackageContract& package =
                kGlobalEnemyPackageContracts[i];
            if (!CandidateVariantEnabled(package.actorClass, package.variant))
                continue;
            if (_stricmp(base, package.variant) == 0)
                return true;
            if (std::strcmp(package.actorClass, "Zombie") == 0)
                zombieBaseRequired = true;
        }
        // Twitcher, Super Slasher and zombievar1 all inherit shared Zombie
        // resources.  Pin the retail base package beside any selected Zombie
        // variant rather than relying on the current chapter to provide it.
        return zombieBaseRequired && _stricmp(base, "zombie") == 0;
    }

    // A Pregnant cut open spawns its swarm, which needs swarm.str resident
    // (pregnant_swarm.inc). Wanted whenever the Pregnant package is.
    const GlobalEnemyCharacterStream* SwarmCompanionStream()
    {
        const GlobalEnemyCharacterStream* swarm = nullptr;
        bool pregnant = false;
        for (unsigned i = 0; i < kGlobalEnemyCharacterStreamCount; ++i)
        {
            const char* base = std::strrchr(kGlobalEnemyCharacterStreams[i].path, '\\');
            base = base ? base + 1 : kGlobalEnemyCharacterStreams[i].path;
            if (_stricmp(base, "swarm") == 0)
                swarm = &kGlobalEnemyCharacterStreams[i];
            else if (_stricmp(base, "pregnant") == 0)
                pregnant = CharacterStreamRequiredByPolicy(kGlobalEnemyCharacterStreams[i]);
        }
        return pregnant ? swarm : nullptr;
    }

    bool FamilyListContains(const char* families, const char* value)
    {
        if (!families || !value || !*value)
            return false;
        const size_t valueLength = std::strlen(value);
        const char* cursor = families;
        while (*cursor)
        {
            const char* end = std::strchr(cursor, ';');
            const size_t length = end ? static_cast<size_t>(end - cursor)
                                      : std::strlen(cursor);
            if (length == valueLength &&
                _strnicmp(cursor, value, valueLength) == 0)
                return true;
            if (!end)
                break;
            cursor = end + 1;
        }
        return false;
    }

    bool AudioStreamRequiredByPolicy(const GlobalEnemyAudioStream& stream)
    {
        if (!gEnemyRandomizerAudioPreload)
            return false;
        // Ours is not a retail package, so no policy contract covers it: it is
        // wanted exactly when the feature that uses it is on.
        if (gSuperExploderWanted &&
            FamilyListContains(stream.families, "sup_expl"))
            return true;
        if (!gEnemyRandomizer)
            return false;
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
        {
            const GlobalEnemyTarget& candidate = kGlobalEnemyTargets[i];
            if (!PolicyUsesGlobalVariant(candidate))
                continue;
            if (FamilyListContains(stream.families, candidate.actorClass) ||
                FamilyListContains(stream.families, candidate.variant))
                return true;
        }
        return false;
    }

    bool SameGlobalVariant(const GlobalEnemyTarget& a,
                           const GlobalEnemyTarget& b)
    {
        return a.actorClass && b.actorClass && a.variant && b.variant &&
            std::strcmp(a.actorClass, b.actorClass) == 0 &&
            std::strcmp(a.variant, b.variant) == 0;
    }

    void LogGlobalPolicyVariantStatus()
    {
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
        {
            const GlobalEnemyTarget& candidate = kGlobalEnemyTargets[i];
            if (!PolicyUsesGlobalVariant(candidate))
                continue;
            bool first = true;
            for (unsigned previous = 0; previous < i; ++previous)
                if (SameGlobalVariant(candidate, kGlobalEnemyTargets[previous]))
                {
                    first = false;
                    break;
                }
            if (!first)
                continue;

            unsigned total = 0;
            unsigned keyed = 0;
            unsigned objectBacked = 0;
            unsigned classMatched = 0;
            unsigned linked = 0;
            unsigned resident = 0;
            for (unsigned probe = i; probe < kGlobalEnemyTargetCount; ++probe)
            {
                const GlobalEnemyTarget& target = kGlobalEnemyTargets[probe];
                if (!SameGlobalVariant(candidate, target))
                    continue;
                ++total;
                const uintptr_t key = TargetKeyFor(target.target);
                uintptr_t object = 0;
                uintptr_t runtimeRoot = 0;
                if (key)
                {
                    ++keyed;
                    if (ReadProcessMemory(GetCurrentProcess(),
                            reinterpret_cast<void*>(key + 0x14), &object,
                            sizeof(object), nullptr) && object)
                    {
                        ++objectBacked;
                        if (ResidentSlotFor(target.target) ==
                            ActorSlotNamed(target.actorClass))
                            ++classMatched;
                        if (ReadProcessMemory(GetCurrentProcess(),
                                reinterpret_cast<void*>(object + 0x268),
                                &runtimeRoot, sizeof(runtimeRoot), nullptr) &&
                            runtimeRoot)
                            ++linked;
                    }
                }
                if (CandidateRuntimeReady(target))
                    ++resident;
            }
            Log("randomizer policy variant: %s/%s ready=%u/%u key=%u object=%u class=%u linked=%u %s",
                candidate.actorClass, candidate.variant, resident, total,
                keyed, objectBacked, classMatched, linked,
                resident ? "READY" : "MISSING");
        }
    }

    uint32_t MixRandomizerWord(uint32_t hash, uint32_t value)
    {
        hash ^= value;
        hash *= 0x85EBCA6Bu;
        hash ^= hash >> 13;
        return hash;
    }

    uint32_t StableRandomizerHash(const Guid128& source,
                                  const Guid128& authoredTarget,
                                  uint32_t salt)
    {
        uint32_t hash = MixRandomizerWord(0x9E3779B9u, gEnemyRandomizerSeed ^ salt);
        for (unsigned i = 0; i < 4; ++i)
            hash = MixRandomizerWord(hash, source.value[i]);
        for (unsigned i = 0; i < 4; ++i)
            hash = MixRandomizerWord(hash, authoredTarget.value[i]);
        hash ^= hash >> 16;
        hash *= 0x7FEB352Du;
        hash ^= hash >> 15;
        return hash;
    }

    // Any published target of that variant whose key is live and which still
    // has a donor FuncSpawn.  Deliberately ignores the vent-mode restriction
    // and the readiness cache, both of which encode randomizer policy rather
    // than whether this particular actor can be built.
    // wantVent: 1 the place is a shaft, 0 it is a direct placement, -1 do not
    // care.  A donor that emerges from a shaft carries the climb-out animation
    // with it; borrow a direct-placement donor for a shaft and the enemy just
    // stands inside it.  Callers may forbid the fallback when preserving the
    // authored arrival behavior is required.
    bool ExtraSpawnTargetFor(const char* variant, uintptr_t exclude,
                             Guid128* out, int wantVent = -1,
                             bool* modeMatched = nullptr,
                             int wantMode = -1,
                             bool allowVentFallback = true)
    {
      // "key, donor or character archive missing" named three possible
      // causes and never said which, so a failure could not be acted on.
      // Count them instead.
      unsigned seenVariant = 0;
      unsigned rejectedMode = 0;
      unsigned rejectedArchive = 0;
      unsigned rejectedKey = 0;
      unsigned rejectedSpawner = 0;
      unsigned lastStreamState = 0xFFFFFFFFu;
      for (unsigned pass = 0; pass < 2; ++pass)
      {
        const bool strict = pass == 0 && wantVent >= 0;
        if (pass == 1 && (wantVent < 0 || !allowVentFallback))
            break;
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
        {
            const GlobalEnemyTarget& candidate = kGlobalEnemyTargets[i];
            if (!candidate.variant || !candidate.actorClass ||
                _stricmp(candidate.variant, variant) != 0)
                continue;
            // An explicitly requested mode is not a preference and has no
            // fallback pass: asking for a corpse and silently getting an
            // ordinary spawn would look like the feature is broken rather
            // than unavailable, so no target is better than the wrong one.
            ++seenVariant;
            if (wantMode >= 0 &&
                candidate.spawnMode != static_cast<unsigned>(wantMode))
            {
                ++rejectedMode;
                continue;
            }
            if (strict && wantMode < 0 &&
                candidate.ventMode != (wantVent == 1))
                continue;
            // Not PackageReadyFor: that requires a reviewed package contract,
            // which classes like Guardian deliberately do not have.  What
            // actually decides the spawn is the same question filter 0 asks -
            // is the character archive resident.
            const uint32_t root = CharacterRootFor(candidate.variant);
            if (root)
            {
                void* manager = nullptr;
                if (!ResolveStreamManager(&manager))
                {
                    ++rejectedArchive;
                    continue;
                }
                const uint32_t stream = ResourceStreamIdFor(manager, root);
                const uint32_t state =
                    stream ? StreamStateFor(manager, stream) : 0xFFFFFFFFu;
                if (!stream || state != 3)
                {
                    ++rejectedArchive;
                    lastStreamState = state;
                    continue;
                }
            }
            if (!TargetKeyFor(candidate.target))
            {
                ++rejectedKey;
                continue;
            }
            if (!LiveSpawnerForTarget(candidate.target, exclude))
            {
                ++rejectedSpawner;
                continue;
            }
            *out = candidate.target;
            if (modeMatched)
                *modeMatched =
                    wantVent < 0 || candidate.ventMode == (wantVent == 1);
            return true;
        }
      }
        Log("extra spawn reject %s: %u candidate(s) of this variant, "
            "mode %u, archive %u (last stream state %d), key %u, "
            "no live spawner %u", variant ? variant : "(none)",
            seenVariant, rejectedMode, rejectedArchive,
            static_cast<int>(lastStreamState), rejectedKey,
            rejectedSpawner);
        return false;
    }

#include "spawner_rules.inc"
#include "watchdog.inc"
#include "scripted_cast.inc"
#include "pregnant_swarm.inc"

    // EnemyRandomizerFamilyWeight_<Class>: how often a family is drawn before
    // any of its variants. With equal weights a directly placed Slasher came
    // out heavy (Super Brute, Pregnant, Infector, Super Exploder) four times in
    // seven, so the heavy families start lower and the Hunter lowest.
    unsigned DefaultFamilyWeight(const char* actorClass)
    {
        if (std::strcmp(actorClass, "Hunter") == 0)
            return 1;
        if (std::strcmp(actorClass, "Brute") == 0)
            return 2;
        if (std::strcmp(actorClass, "Pregnant") == 0 || std::strcmp(actorClass, "Infector") == 0 ||
            std::strcmp(actorClass, "Exploder") == 0)
            return 5;
        return 10;
    }

    struct FamilyWeightRow
    {
        const char* actorClass = nullptr;
        unsigned weight = 10;
    };
    FamilyWeightRow gFamilyWeights[24]{};
    unsigned gFamilyWeightCount = 0;

    unsigned WeightForFamily(const char* actorClass)
    {
        for (unsigned i = 0; i < gFamilyWeightCount; ++i)
            if (std::strcmp(gFamilyWeights[i].actorClass, actorClass) == 0)
                return gFamilyWeights[i].weight;
        return DefaultFamilyWeight(actorClass);
    }

    void LoadFamilyWeights()
    {
        gFamilyWeightCount = 0;
        for (unsigned i = 0; i < kGlobalEnemyTargetCount && gFamilyWeightCount < 24; ++i)
        {
            const char* actorClass = kGlobalEnemyTargets[i].actorClass;
            if (!actorClass || std::strcmp(actorClass, "Hunter") == 0)   // EnemyRandomizerHunterChance
                continue;
            bool known = false;
            for (unsigned k = 0; k < gFamilyWeightCount && !known; ++k)
                known = std::strcmp(gFamilyWeights[k].actorClass, actorClass) == 0;
            if (known)
                continue;
            char key[96]{};
            std::snprintf(key, sizeof(key), "EnemyRandomizerFamilyWeight_%s", actorClass);
            unsigned weight = static_cast<unsigned>(GetPrivateProfileIntA(
                "DeadSpace1Patch", key, static_cast<int>(DefaultFamilyWeight(actorClass)), gIniPath));
            if (weight > 1000)
                weight = 1000;
            gFamilyWeights[gFamilyWeightCount++] = {actorClass, weight};
            Log("randomizer family weight: %s = %u", actorClass, weight);
        }
    }

    // The spawner's own chapter (bits 16-19 of its context), not the last level request.
    unsigned SourceChapter(const SpawnCompatibilitySource* source)
    {
        // With the level order shuffled, how far the run is counts, not the chapter's number.
        if (gLevelOrderRandomizer && gLevelPlayPosition)
            return gLevelPlayPosition;
        const unsigned chapter = source ? (source->context >> kSpawnContextChapterShift) & 0xFu : 0u;
        return chapter ? chapter : gLevelPlayPosition;
    }

    struct FamilyPick
    {
        const char* family;
        const char* variant;
    };

    // Family first, then a variant of it, each drawn from the seed over the
    // whole compatible pool, loaded or not, so what a spawner turns into
    // depends on the seed and the settings alone. How many donor targets a
    // variant happens to have no longer matters, and the difficulty ramp thins
    // a family by the share of its variant weight it keeps. family is null when
    // every family left weighs 0.
    FamilyPick FamilyFirstPick(const unsigned* choices, unsigned count, const Guid128& sourceEntity,
                               const Guid128& original, unsigned chapter)
    {
        struct Group
        {
            const char* family;
            const char* variant;
            unsigned weight;
            unsigned base;
        };
        constexpr unsigned kMaxGroups = 40;
        Group groups[kMaxGroups]{};
        unsigned groupCount = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            const GlobalEnemyTarget& target = kGlobalEnemyTargets[choices[i]];
            bool known = false;
            for (unsigned g = 0; g < groupCount && !known; ++g)
                known = std::strcmp(groups[g].family, target.actorClass) == 0 &&
                        std::strcmp(groups[g].variant, target.variant) == 0;
            if (!known && groupCount < kMaxGroups)
                groups[groupCount++] = {target.actorClass, target.variant,
                                        SelectionWeight(target.variant, chapter),
                                        RampBaseWeight(target.variant, chapter)};
        }
        const char* families[kMaxGroups]{};
        uint64_t familyScore[kMaxGroups]{};
        unsigned familyCount = 0;
        uint64_t total = 0;
        for (unsigned g = 0; g < groupCount; ++g)
        {
            unsigned f = 0;
            while (f < familyCount && std::strcmp(families[f], groups[g].family) != 0)
                ++f;
            if (f == familyCount)
                families[familyCount++] = groups[g].family;
        }
        for (unsigned f = 0; f < familyCount; ++f)
        {
            uint64_t weighted = 0, base = 0;
            for (unsigned g = 0; g < groupCount; ++g)
                if (std::strcmp(groups[g].family, families[f]) == 0)
                {
                    weighted += groups[g].weight;
                    base += groups[g].base;
                }
            familyScore[f] = base ? WeightForFamily(families[f]) * 1024ull * weighted / base : 0;
            total += familyScore[f];
        }
        if (!total)
            return {nullptr, nullptr};
        uint64_t roll = StableRandomizerHash(sourceEntity, original, 0x46414D49u) % total;
        unsigned family = 0;
        for (; family + 1 < familyCount && roll >= familyScore[family]; ++family)
            roll -= familyScore[family];

        uint64_t variantTotal = 0;
        for (unsigned g = 0; g < groupCount; ++g)
            if (std::strcmp(groups[g].family, families[family]) == 0)
                variantTotal += groups[g].weight;
        unsigned group = groupCount;
        uint64_t variantRoll = variantTotal
            ? StableRandomizerHash(sourceEntity, original, 0x56415249u) % variantTotal : 0;
        for (unsigned g = 0; g < groupCount; ++g)
        {
            if (std::strcmp(groups[g].family, families[family]) != 0)
                continue;
            group = g;   // the last one of the family when every weight is 0
            if (variantTotal && variantRoll < groups[g].weight)
                break;
            variantRoll -= variantTotal ? groups[g].weight : 0;
        }
        return group < groupCount ? FamilyPick{groups[group].family, groups[group].variant}
                                  : FamilyPick{nullptr, nullptr};
    }

    // EnemyRandomizerHunterChance: per cent per chapter that a random Hunter appears.
    unsigned gEnemyRandomizerHunterChance = 15;

    bool HunterCompatibleAt(const SpawnCompatibilitySource& source)
    {
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
        {
            const GlobalEnemyTarget& t = kGlobalEnemyTargets[i];
            if (!t.actorClass || !t.variant || std::strcmp(t.actorClass, "Hunter") != 0)
                continue;
            if ((t.ventMode && (source.context & kSpawnContextAnyVent) &&
                 CompatibleReplacement(source, t.actorClass, t.variant)) || DirectReplacement(source, t))
                return true;
        }
        return false;
    }

    // At most one random Hunter per chapter, decided by the seed alone: the
    // chapter rolls EnemyRandomizerHunterChance, and the quiet spawner (no kill
    // counting, no Hunter of its own: kSpawnContextNoHunter) with the lowest
    // seeded hash that can take a Hunter gets it. It cannot die, so it never
    // goes where a room waits for kills.
    const SpawnCompatibilitySource* DesignatedHunterSpawner(unsigned chapter)
    {
        static const SpawnCompatibilitySource* cache[16]{};
        static bool known[16]{};
        if (!chapter || chapter > 15 || !gEnemyRandomizerHunterChance)
            return nullptr;
        if (known[chapter])
            return cache[chapter];
        known[chapter] = true;
        Guid128 key{};
        key.value[0] = chapter;
        if (StableRandomizerHash(key, key, 0x48434843u) % 100u >= gEnemyRandomizerHunterChance)
            return nullptr;
        uint32_t best = 0xFFFFFFFFu;
        for (unsigned i = 0; i < kSpawnCompatibilitySourceCount; ++i)
        {
            const SpawnCompatibilitySource& candidate = kSpawnCompatibilitySources[i];
            if (((candidate.context >> kSpawnContextChapterShift) & 0xFu) != chapter ||
                (candidate.context & kSpawnContextNoHunter) || !candidate.actorClass ||
                std::strcmp(candidate.actorClass, "Hunter") == 0 || !HunterCompatibleAt(candidate))
                continue;
            const uint32_t hash = StableRandomizerHash(candidate.source, candidate.authoredTarget, 0x48554E54u);
            if (!cache[chapter] || hash < best)
            {
                best = hash;
                cache[chapter] = &candidate;
            }
        }
        if (cache[chapter])
            Log("randomizer: chapter %u's random Hunter waits at %s", chapter, SpawnerName(cache[chapter]->source));
        return cache[chapter];
    }

    // A checkpoint reload often has the level load an archive before the pin
    // lends it, so the same seed missed its pick and the authored enemy came
    // back (2026-10-22, spawn_zombie_01_post_mnt_bay). Each spawner keeps this
    // session's outcome instead: the same replacement, borrowed archive or not
    // (the user accepts that the level may drop it), or the authored enemy if
    // that is what a borrowed archive left the first time.
    struct RememberedPick
    {
        Guid128 source;
        Guid128 original;
        int target;   // kGlobalEnemyTargets index, or kRememberedAuthored
    };
    constexpr int kRememberedNone = -1;
    constexpr int kRememberedAuthored = -2;
    constexpr unsigned kMaxRememberedPicks = 1024;
    RememberedPick gRememberedPicks[kMaxRememberedPicks]{};
    unsigned gRememberedPickCount = 0;
    SRWLOCK gRememberedPickLock = SRWLOCK_INIT;
    volatile LONG gRememberedReused = 0, gRememberedBorrowed = 0, gRememberedAuthoredKept = 0;

    int RememberedPickFor(const Guid128& source, const Guid128& original)
    {
        int found = kRememberedNone;
        AcquireSRWLockShared(&gRememberedPickLock);
        for (unsigned i = 0; i < gRememberedPickCount && found == kRememberedNone; ++i)
            if (SameGuid(gRememberedPicks[i].source, source) && SameGuid(gRememberedPicks[i].original, original))
                found = gRememberedPicks[i].target;
        ReleaseSRWLockShared(&gRememberedPickLock);
        return found;
    }

    void RememberPick(const Guid128& source, const Guid128& original, int target)
    {
        AcquireSRWLockExclusive(&gRememberedPickLock);
        bool known = false;
        for (unsigned i = 0; i < gRememberedPickCount && !known; ++i)
            known = SameGuid(gRememberedPicks[i].source, source) && SameGuid(gRememberedPicks[i].original, original);
        if (!known && gRememberedPickCount < kMaxRememberedPicks)
            gRememberedPicks[gRememberedPickCount++] = {source, original, target};
        ReleaseSRWLockExclusive(&gRememberedPickLock);
    }

    constexpr Guid128 kZeroGSources[] = {
#include "zero_g_sources_generated.inc"
    };

    bool ZeroGSource(const Guid128& source)
    {
        for (const Guid128& zeroG : kZeroGSources)
            if (SameGuid(zeroG, source))
                return true;
        return false;
    }

    bool CandidateFor(const Guid128& original, unsigned sourceSlot,
                      const Guid128& sourceEntity, unsigned* expectedSlot,
                      uintptr_t current, Guid128* replacement,
                      bool* placeDirectly = nullptr, SequenceRecast* recast = nullptr)
    {
        if (placeDirectly)
            *placeDirectly = false;
        if (!replacement || !expectedSlot || sourceSlot >= gActorCount)
            return false;

        const SpawnCompatibilitySource* source = nullptr;
        if (gEnemyRandomizerContextV1)
        {
            source = CompatibilitySourceFor(sourceEntity, original);
            if (!source || !source->actorClass ||
                std::strcmp(source->actorClass, gActorNames[sourceSlot]) != 0)
                return false;
            if (gEnemyRandomizerDiagnostics)
                Log("Randomizer: source contract %s/%s at %s [%s]",
                    source->actorClass, source->variant,
                    SpawnContextName(source->context),
                    SpawnerName(sourceEntity));
        }
        else if (!IsRandomizerSource(sourceSlot))
            return false;

        if (gEnemyRandomizerSourceFlagsLoaded && !gEnemyRandomizerSourceEnabled[sourceSlot])
        {
            static LONG disabledSourceLines = 0;
            if (InterlockedIncrement(&disabledSourceLines) <= 24)
                Log("randomizer source disabled: %s at %s",
                    gActorNames[sourceSlot], SpawnerName(sourceEntity));
            return false;
        }

        SpawnerRule ruleCopy;
        const SpawnerRule* spawnerRule = SpawnerRuleFor(sourceEntity, &ruleCopy) ? &ruleCopy : nullptr;
        if (spawnerRule && spawnerRule->kind == SpawnerRuleKind::Keep)
        {
            ReportSpawnerRule(spawnerRule, "keeps its authored enemy");
            return false;
        }
        if (!spawnerRule &&
            KeepChanceKeeps(source ? source->actorClass : gActorNames[sourceSlot], sourceEntity, original))
            return false;
        const bool zeroGSource = ZeroGSource(sourceEntity);
        if (zeroGSource && (!gEnemyRandomizerZeroGVariants ||
                            !gEnemyRandomizerGlobalTemplates || !gGlobalEnemyPoolLoaded))
        {
            static LONG zeroGUnavailable = 0;
            if (InterlockedIncrement(&zeroGUnavailable) <= 32 || gEnemyRandomizerDiagnostics)
                Log("zero G variant: %s keeps its authored enemy (setting=%u, global pool=%u)",
                    SpawnerName(sourceEntity), gEnemyRandomizerZeroGVariants ? 1u : 0u,
                    gGlobalEnemyPoolLoaded ? 1u : 0u);
            return false;
        }

        // Global sidecar candidates carry a catalogued actor-class contract.
        // Presence in SimManager proves residency; the factory hooks below
        // still verify the actual class and retry the authored target if a
        // malformed candidate violates that contract.
        if (gEnemyRandomizerGlobalTemplates && gGlobalEnemyPoolLoaded)
        {
            // The authored Hunter belongs to its chapter's script (the cryo
            // trap in chapter 5), so it is never replaced.
            if (source && std::strcmp(source->actorClass, "Hunter") == 0)
                return false;
            // Every authored Brute is a set piece: chapter 4's spawn_brute_msg became a Super
            // Brute that never showed, and the door its fight opens stayed shut (2026-10-22).
            if (source && std::strcmp(source->actorClass, "Brute") == 0)
            {
                static LONG bruteLines = 0;
                if (InterlockedIncrement(&bruteLines) <= 16)
                    Log("randomizer: the authored Brute at %s stays", SpawnerName(sourceEntity));
                return false;
            }
            // Leapers and Lurkers in weightless rooms move on their own zero G graph; a
            // stranger there floats or never reaches the player (zero_g_sources.py).
            const bool sourceVent = source && (source->context & kSpawnContextAnyVent) != 0;
            const bool scripted = source && (source->context & kSpawnContextScripted) != 0;
            const bool authoredVent = sourceVent && !(source->context & kSpawnContextDirect);
            if (scripted)
            {
                const char* where = SpawnerName(sourceEntity);
                if (!gEnemyRandomizerScriptedVariants)
                {
                    NoteScriptedKept(&gScriptedKeptOff, where, "EnemyRandomizerScriptedVariants=0");
                    return false;
                }
                if ((source->context & kSpawnContextScriptedFixed) || !recast)
                {
                    NoteScriptedKept(&gScriptedKeptFixed, where, "its scene needs the authored actor itself");
                    return false;
                }
                if (!PrepareSequenceRecast(original, recast, where))
                    return false;
            }
            if (zeroGSource)
            {
                const char* alternate = nullptr;
                if (source && source->variant && std::strcmp(source->actorClass, "Leaper") == 0)
                    alternate = std::strcmp(source->variant, "leaper") == 0 ? "super_leaper" :
                                std::strcmp(source->variant, "super_leaper") == 0 ? "leaper" : nullptr;
                else if (source && source->variant && std::strcmp(source->actorClass, "Lurker") == 0)
                    alternate = std::strcmp(source->variant, "lurker") == 0 ? "super_lurker" :
                                std::strcmp(source->variant, "super_lurker") == 0 ? "lurker" : nullptr;
                const bool swap = (StableRandomizerHash(sourceEntity, original, 0x5A455247u) & 1u) != 0;
                if (!alternate || !swap)
                {
                    static LONG zeroGKeepLines = 0;
                    if (InterlockedIncrement(&zeroGKeepLines) <= 64 || gEnemyRandomizerDiagnostics)
                        Log("zero G variant: %s %s/%s keeps authored target (seed roll=%s)",
                            SpawnerName(sourceEntity), source ? source->actorClass : "unknown",
                            source && source->variant ? source->variant : "unknown",
                            swap ? "swap, no reviewed pair" : "keep");
                    return false;
                }
                if (gEnemyRandomizerForcedVariant[0] &&
                    std::strcmp(gEnemyRandomizerForcedVariant, alternate) != 0)
                    return false;
                if (FamilyListContains(gEnemyRandomizerDisabledVariants, alternate) ||
                    WeightForVariant(alternate) == 0 ||
                    WeightForFamily(source->actorClass) == 0)
                    return false;
                const int remembered = RememberedPickFor(sourceEntity, original);
                if (remembered == kRememberedAuthored)
                    return false;
                const unsigned first = StableRandomizerHash(sourceEntity, original, 0x5A47444Eu) %
                                       kGlobalEnemyTargetCount;
                unsigned eligible = 0, unready = 0, borrowed = 0, busy = 0;
                for (unsigned offset = 0; offset < kGlobalEnemyTargetCount; ++offset)
                {
                    const unsigned index = (first + offset) % kGlobalEnemyTargetCount;
                    const GlobalEnemyTarget& candidate = kGlobalEnemyTargets[index];
                    if (!candidate.actorClass || !candidate.variant ||
                        std::strcmp(candidate.actorClass, source->actorClass) != 0 ||
                        std::strcmp(candidate.variant, alternate) != 0 ||
                        candidate.ventMode != authoredVent ||
                        (remembered >= 0 && static_cast<unsigned>(remembered) != index) ||
                        !SpawnerRuleAllows(spawnerRule, candidate.actorClass, candidate.variant))
                        continue;
                    ++eligible;
                    if (!gGlobalEnemyTargetReady[index] || !CandidateRuntimeReady(candidate))
                    {
                        ++unready;
                        continue;
                    }
                    if (!CharacterArchiveOwned(candidate.variant))
                    {
                        ++borrowed;
                        continue;
                    }
                    if (scripted && !DonorIsFree(candidate.target))
                    {
                        ++busy;
                        continue;
                    }
                    *replacement = candidate.target;
                    *expectedSlot = ActorSlotNamed(candidate.actorClass);
                    if (*expectedSlot >= gActorCount)
                        return false;
                    RememberPick(sourceEntity, original, static_cast<int>(index));
                    Log("zero G variant: %s %s/%s -> %s (target %08X-%08X, scripted=%u)",
                        SpawnerName(sourceEntity), source->actorClass, source->variant, alternate,
                        candidate.target.value[0], candidate.target.value[1], scripted ? 1u : 0u);
                    return true;
                }
                static LONG zeroGFailedLines = 0;
                if (InterlockedIncrement(&zeroGFailedLines) <= 64 || gEnemyRandomizerDiagnostics)
                    Log("zero G variant: %s %s/%s -> %s unavailable (eligible=%u, unready=%u, borrowed=%u, busy=%u, remembered=%d); authored stays",
                        SpawnerName(sourceEntity), source->actorClass, source->variant, alternate,
                        eligible, unready, borrowed, busy, remembered);
                if (remembered == kRememberedNone && borrowed && !unready && !busy)
                    RememberPick(sourceEntity, original, kRememberedAuthored);
                return false;
            }
            const unsigned chapter = SourceChapter(source);
            const bool ruleHunter = SpawnerRuleNamesHunter(spawnerRule);
            const bool hunterHere = ruleHunter ||
                (source && DesignatedHunterSpawner((source->context >> kSpawnContextChapterShift) & 0xFu) == source);
            unsigned choices[kGlobalEnemyTargetCount]{};
            bool placeChoice[kGlobalEnemyTargetCount]{};
            unsigned count = 0;
            unsigned hunterChoice = kGlobalEnemyTargetCount;
            // Every compatible target, loaded or not: the pick may not depend on what has streamed in yet.
            for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
            {
                const GlobalEnemyTarget& candidate = kGlobalEnemyTargets[i];
                if (!candidate.actorClass || !candidate.variant ||
                    SameGuid(candidate.target, original))
                    continue;
                if (gEnemyRandomizerForcedVariant[0] &&
                    std::strcmp(candidate.variant, gEnemyRandomizerForcedVariant) != 0)
                    continue;
                if (FamilyListContains(gEnemyRandomizerDisabledVariants, candidate.variant) ||
                    WeightForVariant(candidate.variant) == 0 ||
                    WeightForFamily(candidate.actorClass) == 0)
                    continue;
                bool placed = false;
                if (source)
                {
                    // Out of the source's own vent, or standing where a
                    // directly placed enemy stood.
                    if (candidate.ventMode && sourceVent &&
                        CompatibleReplacement(*source, candidate.actorClass,
                                              candidate.variant))
                        placed = false;
                    else if (DirectReplacement(*source, candidate))
                        placed = candidate.ventMode;
                    else
                        continue;
                }
                else if (!candidate.ventMode || *expectedSlot >= gActorCount ||
                         std::strcmp(candidate.actorClass,
                                     gActorNames[*expectedSlot]) != 0)
                    continue;
                // The sequences play the authored class's animations at the authored
                // arrival; the spawn mode (kSpawnContextDirect) beats the nearest vent marker.
                if (scripted && (placed || candidate.ventMode != authoredVent ||
                                 std::strcmp(candidate.actorClass, source->actorClass) != 0))
                    continue;
                if (!SpawnerRuleAllows(spawnerRule, candidate.actorClass, candidate.variant))
                    continue;
                const bool hunter = std::strcmp(candidate.actorClass, "Hunter") == 0;
                if (hunter && !hunterHere)
                    continue;
                // The designated spawner's Hunter is decided apart from the family draw.
                if (hunter && !ruleHunter)
                {
                    if (hunterChoice == kGlobalEnemyTargetCount)
                        hunterChoice = count;
                    placeChoice[count] = placed;
                    choices[count++] = i;
                    continue;
                }
                placeChoice[count] = placed;
                choices[count++] = i;
            }
            if (spawnerRule && !count)
            {
                ReportSpawnerRule(spawnerRule, "leaves no candidate here; the authored enemy stays");
                return false;
            }
            if (scripted && !count)
            {
                NoteScriptedKept(&gScriptedKeptFixed, SpawnerName(sourceEntity),
                                 "no variant of its class arrives the same way");
                return false;
            }
            if (count)
            {
                auto finish = [&](unsigned picked, const char* how) -> bool {
                    const unsigned selectedIndex = choices[picked];
                    const GlobalEnemyTarget& selected = kGlobalEnemyTargets[selectedIndex];
                    *replacement = selected.target;
                    *expectedSlot = ActorSlotNamed(selected.actorClass);
                    if (*expectedSlot >= gActorCount)
                        return false;
                    if (placeDirectly)
                        *placeDirectly = placeChoice[picked];
                    RememberPick(sourceEntity, original, static_cast<int>(selectedIndex));
                    static LONG pickLines = 0;
                    if (InterlockedIncrement(&pickLines) <= 300 || gEnemyRandomizerDiagnostics)
                        Log("randomizer pick: %s/%s -> %s/%s, %s, at %s (%u candidates, chapter %u)%s%s",
                            source ? source->actorClass : gActorNames[sourceSlot],
                            source ? source->variant : "?", selected.actorClass, selected.variant,
                            placeChoice[picked] ? "vent donor placed directly" :
                                selected.ventMode ? "out of the vent" : "placed directly",
                            SpawnerName(sourceEntity), count, chapter,
                            scripted ? ", its sequences follow" : "", how);
                    return true;
                };
                const int remembered = RememberedPickFor(sourceEntity, original);
                if (remembered == kRememberedAuthored)
                {
                    if (InterlockedIncrement(&gRememberedAuthoredKept) <= 32)
                        Log("randomizer pick at %s: the authored enemy stays, as it did before in this session",
                            SpawnerName(sourceEntity));
                    return false;
                }
                for (unsigned c = 0; remembered >= 0 && c < count; ++c)
                {
                    if (choices[c] != static_cast<unsigned>(remembered))
                        continue;
                    const GlobalEnemyTarget& target = kGlobalEnemyTargets[choices[c]];
                    if (CandidateRuntimeReady(target) && (!scripted || DonorIsFree(target.target)))
                    {
                        InterlockedIncrement(&gRememberedReused);
                        const bool borrowed = !CharacterArchiveOwned(target.variant);
                        if (borrowed)
                            InterlockedIncrement(&gRememberedBorrowed);
                        return finish(c, borrowed ? " (as before; its archive is borrowed from the level)"
                                                  : " (as before)");
                    }
                    break;
                }
                FamilyPick pick{nullptr, nullptr};
                if (hunterChoice < count)
                {
                    const GlobalEnemyTarget& hunter = kGlobalEnemyTargets[choices[hunterChoice]];
                    pick = {hunter.actorClass, hunter.variant};
                }
                else
                {
                    // Weights cannot thin out a class whose candidates are all
                    // late ones (Exploder -> sup_expl), so the ramp keeps the
                    // authored enemy there with the chance the weights would.
                    bool lateOnly = gEnemyRandomizerDifficultyRamp && chapter;
                    for (unsigned i = 0; lateOnly && i < count; ++i)
                        lateOnly = LateGameVariant(kGlobalEnemyTargets[choices[i]].variant);
                    if (lateOnly && StableRandomizerHash(sourceEntity, original,
                                                         0x52414D50u) % 12u >= chapter)
                    {
                        if (gEnemyRandomizerDiagnostics)
                            Log("Randomizer: difficulty ramp keeps the authored %s (chapter %u, only late candidates)",
                                gActorNames[sourceSlot], chapter);
                        return false;
                    }
                    pick = FamilyFirstPick(choices, count, sourceEntity, original, chapter);
                    if (!pick.family)
                    {
                        if (gEnemyRandomizerDiagnostics)
                            Log("Randomizer: every candidate family at %s weighs 0; the authored enemy stays",
                                SpawnerName(sourceEntity));
                        return false;
                    }
                }
                // How it arrives comes from the seed as well: out of the vent (0),
                // as a direct donor (1) or as a vent donor placed directly (2).
                auto inPick = [&](unsigned c) {
                    const GlobalEnemyTarget& t = kGlobalEnemyTargets[choices[c]];
                    return std::strcmp(t.actorClass, pick.family) == 0 && std::strcmp(t.variant, pick.variant) == 0;
                };
                auto arrivalOf = [&](unsigned c) {
                    return placeChoice[c] ? 2u : kGlobalEnemyTargets[choices[c]].ventMode ? 0u : 1u;
                };
                bool arrivals[3]{};
                unsigned arrivalCount = 0;
                for (unsigned c = 0; c < count; ++c)
                    if (inPick(c) && !arrivals[arrivalOf(c)])
                    {
                        arrivals[arrivalOf(c)] = true;
                        ++arrivalCount;
                    }
                unsigned nth = arrivalCount ? StableRandomizerHash(sourceEntity, original, 0x41525256u) % arrivalCount : 0;
                unsigned arrival = 0;
                for (; arrival < 3; ++arrival)
                    if (arrivals[arrival] && !nth--)
                        break;
                // Only which of the equal donors serves it may follow what is loaded.
                unsigned ready[kGlobalEnemyTargetCount]{};
                unsigned readyCount = 0;
                for (unsigned c = 0; c < count; ++c)
                    if (inPick(c) && arrivalOf(c) == arrival && gGlobalEnemyTargetReady[choices[c]])
                        ready[readyCount++] = c;
                unsigned picked = count;
                bool lent = false, busy = false;
                const unsigned first = readyCount
                    ? StableRandomizerHash(sourceEntity, original, 0x54415247u) % readyCount : 0;
                for (unsigned k = 0; k < readyCount && picked == count; ++k)
                {
                    const unsigned c = ready[(first + k) % readyCount];
                    const GlobalEnemyTarget& target = kGlobalEnemyTargets[choices[c]];
                    // The cache removes dozens of map traversals from every
                    // spawn. Verify only the final target immediately before
                    // use; a world teardown therefore still fails closed.
                    if (!CandidateRuntimeReady(target))
                        gGlobalEnemyTargetReady[choices[c]] = false;
                    else if (!CharacterArchiveOwned(target.variant))
                        lent = true;
                    else if (scripted && !DonorIsFree(target.target))
                        busy = true;
                    else
                        picked = c;
                }
                if (picked == count && scripted && busy)
                {
                    NoteScriptedKept(&gScriptedKeptBusy, SpawnerName(sourceEntity),
                                     "every loaded donor of that variant has a live instance");
                    return false;
                }
                if (picked == count && lent && !busy)
                    RememberPick(sourceEntity, original, kRememberedAuthored);
                if (picked == count)
                {
                    static LONG notReadyLines = 0;
                    if (InterlockedIncrement(&notReadyLines) <= 64 || gEnemyRandomizerDiagnostics)
                        Log("randomizer pick: %s/%s at %s is not loaded yet%s; the authored enemy stays",
                            pick.family, pick.variant, SpawnerName(sourceEntity),
                            lent ? " (its archive is the level's, which may drop it)" : "");
                    return false;
                }
                return finish(picked, "");
            }
        }

        // With the pool on, the seed alone decides: no fallback to the
        // targets this session happens to have seen.
        if (gEnemyRandomizerGlobalTemplates)
        {
            static LONG pendingLines = 0;
            if (!gGlobalEnemyPoolLoaded && InterlockedIncrement(&pendingLines) <= 16)
                Log("randomizer: the global pool is not ready yet at %s; the authored enemy stays",
                    SpawnerName(sourceEntity));
            return false;
        }

        // A family or variant rule is only defined for the global pool's picks.
        if (spawnerRule)
        {
            ReportSpawnerRule(spawnerRule, "needs the global pool (EnemyRandomizerGlobalTemplates=1); "
                              "the authored enemy stays");
            return false;
        }

        // A newly streamed encounter often fires its Pregnant before the
        // first Zombie. The SimManager has already registered both runtime
        // target objects at that point, so cross-family selection can inspect
        // their proven vtables without waiting for either actor to spawn.
        if (!source && IsCrossFamilySource(sourceSlot) &&
            PickResidentTarget(original, *expectedSlot, current, replacement))
            return true;

        if (gLearnedTargetCount < 2)
            return false;

        unsigned candidates[kMaxLearnedTargets]{};
        unsigned count = 0;
        for (unsigned i = 0; i < gLearnedTargetCount; ++i)
        {
            const LearnedTarget& candidate = gLearnedTargets[i];
            if (SameGuid(candidate.target, original) || !TargetOwnerIsLive(candidate))
                continue;
            if (source)
            {
                const SpawnCompatibilitySource* candidateMeta =
                    CompatibilityMetadataForTarget(candidate.target);
                if (!candidateMeta || !CompatibleReplacement(
                        *source, candidateMeta->actorClass, candidateMeta->variant))
                    continue;
            }
            else if (candidate.actorSlot != *expectedSlot)
                continue;
            if (candidate.actorSlot < gActorCount &&
                std::strcmp(gActorNames[candidate.actorSlot], "Hunter") == 0)
                continue;   // random Hunters come only from the pool's designated spawner
            candidates[count++] = i;
        }
        if (!count)
            return false;

        const LearnedTarget& selected = gLearnedTargets[candidates[
            StableRandomizerHash(sourceEntity, original, 0x4C4F434Cu) % count]];
        *replacement = selected.target;
        *expectedSlot = selected.actorSlot;
        return true;
    }

    // Read next to the executable so the settings sit where the player looks
    // for them, not inside the plugins folder.
    // One line per enemy class, exactly like the EnemyType_ sliders in the
    // Dead Space 3 patch. 100 means as authored, 0 never multiplies that kind.
    // One count per class, given as the total that arrives on a trigger rather
    // than a percentage of something else. 1 is the game as shipped, 0 also
    // means leave it alone, and anything higher is that many enemies. A class
    // with no line of its own falls back to SpawnsPerTrigger.
    void LoadEnemyPercents()
    {
        gEnemyRandomizerSourceFlagsLoaded = false;
        for (unsigned i = 0; i < gActorCount; ++i)
        {
            char key[64]{};
            std::snprintf(key, sizeof(key), "EnemyRandomizerSource_%s", gActorNames[i]);
            gEnemyRandomizerSourceEnabled[i] = GetPrivateProfileIntA(
                "DeadSpace1Patch", key, 1, gIniPath) != 0;
            if (!gEnemyRandomizerSourceEnabled[i])
                Log("randomizer source disabled: %s", gActorNames[i]);
            std::snprintf(key, sizeof(key), "EnemyType_%s", gActorNames[i]);
            unsigned value = static_cast<unsigned>(
                GetPrivateProfileIntA("DeadSpace1Patch", key, gExtraSpawns,
                                      gIniPath));
            if (value > 16)
                value = 16;
            gActorPercent[i] = value;
            Log("   %-20s %u per trigger", gActorNames[i], value);
        }
        gEnemyRandomizerSourceFlagsLoaded = true;
    }

    uint32_t LoadRandomizerSeed()
    {
        char text[64]{};
        GetPrivateProfileStringA("DeadSpace1Patch", "EnemyRandomizerSeed",
                                 "875025", text, sizeof(text), gIniPath);
        uint32_t seed = 0;
        if (!ParseRandomizerSeed(text, seed))
        {
            Log("randomizer: invalid seed '%s'; using 875025", text);
            return 0x00D5A11u;
        }
        return seed;
    }

    // Pulled out of LoadSettings so the rules can be re-read while the
    // game runs.  Editing a rule used to mean restarting, which cost more
    // time per experiment than the experiment itself.
    void LoadExtraSpawnRules()
    {
        gExtraSpawnRuleCount = 0;
        {
            char section[4096]{};
            const DWORD used = GetPrivateProfileSectionA(
                "ExtraSpawns", section, sizeof(section) - 2, gIniPath);
            const char* cursor = section;
            while (used && *cursor && gExtraSpawnRuleCount < 32)
            {
                const char* equals = std::strchr(cursor, '=');
                if (equals)
                {
                    char key[96]{};
                    const size_t keyLength = static_cast<size_t>(equals - cursor);
                    if (keyLength && keyLength < sizeof(key))
                    {
                        std::memcpy(key, cursor, keyLength);
                        ExtraSpawnRule rule;
                        bool haveSource = false;
                        if (_stricmp(key, "any") == 0)
                        {
                            rule.anySource = true;
                            haveSource = true;
                        }
                        else
                            haveSource = SpawnerGuidByName(key, &rule.source) ||
                                         ParseGuidText(key, &rule.source);
                        char value[160]{};
                        std::strncpy(value, equals + 1, sizeof(value) - 1);
                        char* fields[8]{};
                        unsigned fieldCount = 0;
                        char* token = value;
                        while (fieldCount < 8 && token)
                        {
                            fields[fieldCount++] = token;
                            char* next = std::strchr(token, ',');
                            if (!next)
                                break;
                            *next = 0;
                            token = next + 1;
                        }
                        std::strncpy(rule.variant, fields[0],
                                     sizeof(rule.variant) - 1);
                        if (fieldCount > 1)
                            rule.count =
                                static_cast<unsigned>(std::atoi(fields[1]));
                        if (fieldCount > 2)
                            rule.chance =
                                static_cast<unsigned>(std::atoi(fields[2]));
                        if (fieldCount >= 6 && fields[3][0] && fields[4][0] &&
                            fields[5][0])
                        {
                            rule.hasPosition = true;
                            for (unsigned axis = 0; axis < 3; ++axis)
                                rule.position[axis] = static_cast<float>(
                                    std::atof(fields[3 + axis]));
                        }
                        if (fieldCount >= 7 && fields[6][0])
                        {
                            rule.hasYaw = true;
                            rule.yaw = static_cast<float>(std::atof(fields[6]));
                        }
                        if (fieldCount >= 8 && fields[7][0])
                        {
                            const char* word = fields[7];
                            while (*word == ' ')
                                ++word;
                            if (_stricmp(word, "dead") == 0)
                                rule.wantMode = kSpawnModePlayingDead;
                            else if (_stricmp(word, "vent") == 0)
                                rule.wantMode = kSpawnModeVent;
                            else if (_stricmp(word, "direct") == 0)
                                rule.wantMode = 7;
                        }
                        if (rule.count == 0 || rule.count > 8)
                            rule.count = 1;
                        if (rule.chance == 0 || rule.chance > 100)
                            rule.chance = 100;
                        if (haveSource && rule.variant[0])
                        {
                            gExtraSpawnRules[gExtraSpawnRuleCount++] = rule;
                            char where[80]{};
                            if (rule.hasPosition)
                                std::snprintf(where, sizeof(where),
                                              " at %.1f %.1f %.1f",
                                              rule.position[0],
                                              rule.position[1],
                                              rule.position[2]);
                            char facing[32]{};
                            if (rule.hasYaw)
                                std::snprintf(facing, sizeof(facing),
                                              " facing %.0f deg", rule.yaw);
                            Log("extra spawn rule: %s -> %u x %s, %u%%%s%s",
                                key, rule.count, rule.variant, rule.chance,
                                where, facing);
                        }
                        else
                            Log("extra spawn rule ignored: '%s' is not a known "
                                "spawner name or GUID", key);
                    }
                }
                cursor += std::strlen(cursor) + 1;
            }
        }
    }


    // ---------------------------------------------------------------------
    // Live reload.
    //
    // Only values that are read fresh each time they are used can change while
    // the game runs.  Anything that decides whether a hook gets installed is
    // read once at startup and stays that way - flipping it here would leave
    // the code and the setting disagreeing, which is worse than a restart.
    // ---------------------------------------------------------------------
    // Defined further down, next to the box test itself.
    void LoadVacuumBoxes();

    FILETIME gIniWriteTime{};

    bool IniChanged()
    {
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!GetFileAttributesExA(gIniPath, GetFileExInfoStandard, &info))
            return false;
        if (info.ftLastWriteTime.dwLowDateTime == gIniWriteTime.dwLowDateTime &&
            info.ftLastWriteTime.dwHighDateTime == gIniWriteTime.dwHighDateTime)
            return false;
        const bool first = gIniWriteTime.dwLowDateTime == 0 &&
                           gIniWriteTime.dwHighDateTime == 0;
        gIniWriteTime = info.ftLastWriteTime;
        return !first;
    }

    void ReloadLiveSettings()
    {
        gZoneProbeOffset = static_cast<unsigned>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "ZoneProbeOffset", 0, gIniPath));
        gZoneProbeValue = static_cast<unsigned>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "ZoneProbeValue", 0, gIniPath));
        gZoneProbeHotkey = GetPrivateProfileIntA(
            "DeadSpace1Patch", "ZoneProbeHotkey", 0x7B, gIniPath);
        gWorldEditorHotkey = GetPrivateProfileIntA(
            "DeadSpace1Patch", "WorldEditorHotkey", 0x7A, gIniPath);
        gNoClipHotkey = GetPrivateProfileIntA(
            "DeadSpace1Patch", "NoClipHotkey", 0x76, gIniPath);
        gNoClipSpeed = static_cast<float>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "NoClipSpeedCm", 50, gIniPath)) / 100.0f;
        gExtraSpawns = static_cast<unsigned>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "SpawnsPerTrigger", 1, gIniPath));
        gExtraSpawnDiagnostics = GetPrivateProfileIntA(
            "DeadSpace1Patch", "ExtraSpawnDiagnostics", 0, gIniPath) != 0;
        gLevelOrderDiagnostics = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LevelOrderDiagnostics", 0, gIniPath) != 0;
        LoadExtraSpawnRules();
        LoadSpawnerRules();
        // The vacuum zone list, but not the switch that decides whether
        // its hook was installed: changing that live would leave the
        // setting and the installed code disagreeing.
        {
            const bool wasEnabled = gVacuumZones;
            LoadVacuumZoneSettings();
            gVacuumZones = wasEnabled;
            LoadVacuumBoxes();
        }
        Log("settings reloaded: zone probe +0x%03X=%08X, %u extra spawn rule(s), "
            "%u spawn(s) per trigger, noclip %.2f m, %u vacuum zone(s)",
            gZoneProbeOffset, gZoneProbeValue, gExtraSpawnRuleCount,
            gExtraSpawns, gNoClipSpeed, gVacuumZoneRuleCount);
    }

    void LoadSettings()
    {
        LoadVacuumZoneSettings();
        LoadDeathMessageSettings();
        LoadCrashSettings();
        LoadLootAmountSettings();
        LoadWatchdogSettings();
        gExtraSpawns = static_cast<unsigned>(
            GetPrivateProfileIntA("DeadSpace1Patch", "SpawnsPerTrigger",
                                  GetPrivateProfileIntA("DeadSpace1Patch",
                                      "ExtraSpawnsPerTrigger", 1, gIniPath),
                                  gIniPath));
        gDiscover = GetPrivateProfileIntA("DeadSpace1Patch", "DiscoverClasses",
                                          0, gIniPath) != 0;
        gEnemyRandomizer = GetPrivateProfileIntA("DeadSpace1Patch",
                                                  "EnemyRandomizer", 0,
                                                  gIniPath) != 0;
        gEnemyRandomizerDiagnostics = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerDiagnostics", 0, gIniPath) != 0;
        gEnemyRandomizerGlobalTemplates = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerGlobalTemplates", 0, gIniPath) != 0;
        gEnemyRandomizerGlobalStreaming = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerGlobalStreaming", 0, gIniPath) != 0;
        gEnemyRandomizerZeroGVariants = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerZeroGVariants", 0, gIniPath) != 0;
        gEnemyRandomizerCrossFamilyGround = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerCrossFamilyGround", 0, gIniPath) != 0;
        gEnemyRandomizerDifficultyRamp = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerDifficultyRamp", 1, gIniPath) != 0;
        gEnemyRandomizerAudioPreload = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerAudioPreload", 1, gIniPath) != 0;
        gEnemyRandomizerDirectSpawns = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerDirectSpawns", 1, gIniPath) != 0;
        gEnemyRandomizerHeavyFamilies = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerHeavyFamilies", 1, gIniPath) != 0;
        {
            const int hunter = GetPrivateProfileIntA("DeadSpace1Patch", "EnemyRandomizerHunterChance", 15, gIniPath);
            gEnemyRandomizerHunterChance = static_cast<unsigned>(hunter < 0 ? 0 : hunter > 100 ? 100 : hunter);
        }
        gEnemyRandomizerContextV1 = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerContextV1", 1, gIniPath) != 0;
        gPlacementHotkey = GetPrivateProfileIntA(
            "DeadSpace1Patch", "PlacementHotkey", 0x79, gIniPath);
        gWorldEditor = GetPrivateProfileIntA(
            "DeadSpace1Patch", "WorldEditor", 0, gIniPath) != 0;
        gPlayerQueryGuard = GetPrivateProfileIntA(
            "DeadSpace1Patch", "PlayerQueryGuard", 1, gIniPath) != 0;
        gLevelOrderDiagnostics = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LevelOrderDiagnostics", 0, gIniPath) != 0;
        gLevelOrderRandomizer = GetPrivateProfileIntA(
            "DeadSpace1Patch", "LevelOrderRandomizer", 0, gIniPath) != 0;
        gZoneProbeHotkey = GetPrivateProfileIntA(
            "DeadSpace1Patch", "ZoneProbeHotkey", 0x7B, gIniPath);
        gZoneProbeOffset = static_cast<unsigned>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "ZoneProbeOffset", 0, gIniPath));
        gZoneProbeValue = static_cast<unsigned>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "ZoneProbeValue", 0, gIniPath));
        gWorldEditorHotkey = GetPrivateProfileIntA(
            "DeadSpace1Patch", "WorldEditorHotkey", 0x7A, gIniPath);
        gNoClip = GetPrivateProfileIntA(
            "DeadSpace1Patch", "NoClip", 0, gIniPath) != 0;
        gNoClipHotkey = GetPrivateProfileIntA(
            "DeadSpace1Patch", "NoClipHotkey", 0x76, gIniPath);
        gNoClipSpeed = static_cast<float>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "NoClipSpeedCm", 50, gIniPath)) / 100.0f;
        gPinAtWorldBuilt = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerPinAtWorldBuilt", 1,
            gIniPath) != 0;
        gEnemyRandomizerIgnoreVentSurface = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerIgnoreVentSurface", 1,
            gIniPath) != 0;
        gEnemyRandomizerCrossFamilyContexts = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerCrossFamilyContexts", 1, gIniPath) != 0;
        gEnemyRandomizerScriptedVariants = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerScriptedVariants", 1, gIniPath) != 0;
        gEnemyRandomizerRetryFailedSpawns = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerRetryFailedSpawns", 0,
            gIniPath) != 0;
        GetPrivateProfileStringA(
            "DeadSpace1Patch", "EnemyRandomizerForcedVariant", "",
            gEnemyRandomizerForcedVariant,
            static_cast<DWORD>(sizeof(gEnemyRandomizerForcedVariant)), gIniPath);
        // [ExtraSpawns] wins over the single-source keys below; both stay
        // available so an existing configuration keeps working.
        LoadExtraSpawnRules();
        LoadSpawnerRules();

        char extraSource[80]{};
        GetPrivateProfileStringA("DeadSpace1Patch", "ExtraSpawnSource", "",
                                 extraSource,
                                 static_cast<DWORD>(sizeof(extraSource)), gIniPath);
        gExtraSpawnAnySource = _stricmp(extraSource, "any") == 0;
        gExtraSpawnSourceValid =
            gExtraSpawnAnySource || ParseGuidText(extraSource, &gExtraSpawnSource);
        gExtraSpawnChancePercent = GetPrivateProfileIntA(
            "DeadSpace1Patch", "ExtraSpawnChancePercent", 100, gIniPath);
        if (gExtraSpawnChancePercent > 100)
            gExtraSpawnChancePercent = 100;
        GetPrivateProfileStringA("DeadSpace1Patch", "ExtraSpawnVariant", "",
                                 gExtraSpawnVariant,
                                 static_cast<DWORD>(sizeof(gExtraSpawnVariant)),
                                 gIniPath);
        gExtraSpawnCount = GetPrivateProfileIntA("DeadSpace1Patch",
                                                 "ExtraSpawnCount", 1, gIniPath);
        if (gExtraSpawnCount > 8)
            gExtraSpawnCount = 8;
        gVariantWeightCount = 0;
        for (unsigned i = 0; i < kGlobalEnemyPackageContractCount && i < 32; ++i)
        {
            const char* variant = kGlobalEnemyPackageContracts[i].variant;
            bool known = false;
            for (unsigned seen = 0; seen < gVariantWeightCount; ++seen)
                if (std::strcmp(gVariantWeights[seen].variant, variant) == 0)
                    known = true;
            if (known)
                continue;
            char key[96]{};
            std::snprintf(key, sizeof(key), "EnemyRandomizerWeight_%s", variant);
            unsigned weight = GetPrivateProfileIntA("DeadSpace1Patch", key, 10,
                                                    gIniPath);
            if (weight > 1000)
                weight = 1000;
            gVariantWeights[gVariantWeightCount++] = { variant, weight };
        }
        unsigned superExploderWeight = GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerWeight_sup_expl", 10, gIniPath);
        if (superExploderWeight > 1000)
            superExploderWeight = 1000;
        gVariantWeights[gVariantWeightCount++] = { "sup_expl", superExploderWeight };
        GetPrivateProfileStringA(
            "DeadSpace1Patch", "EnemyRandomizerDisabledVariants", "",
            gEnemyRandomizerDisabledVariants,
            static_cast<DWORD>(sizeof(gEnemyRandomizerDisabledVariants)),
            gIniPath);
        gSpawnCatalog = GetPrivateProfileIntA("DeadSpace1Patch", "SpawnCatalog", 1,
                                              gIniPath) != 0;
        gSpawnResolverDiagnostics = GetPrivateProfileIntA(
            "DeadSpace1Patch", "SpawnResolverDiagnostics", 0, gIniPath) != 0;
        gEnemyRandomizerChance = static_cast<unsigned>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "EnemyRandomizerChancePercent", 100, gIniPath));
        if (gEnemyRandomizerChance > 100)
            gEnemyRandomizerChance = 100;
        gEnemyRandomizerSeed = LoadRandomizerSeed();
        gCheckpointChanger = GetPrivateProfileIntA(
            "DeadSpace1Patch", "CheckpointChanger", 1, gIniPath) != 0;
        gCheckpointChangerHotkey = static_cast<unsigned>(GetPrivateProfileIntA(
            "DeadSpace1Patch", "CheckpointChangerHotkey", VK_F9, gIniPath));
        if (gExtraSpawns > 16)
            gExtraSpawns = 16;      // past this the game has more corpses than floor
        Log("settings: SpawnsPerTrigger=%u (from %s)", gExtraSpawns, gIniPath);
        Log("randomizer: %s, chance=%u%%, diagnostics=%s (same-family, live targets only)",
            gEnemyRandomizer ? "enabled" : "disabled", gEnemyRandomizerChance,
            gEnemyRandomizerDiagnostics ? "on" : "off");
        Log("randomizer global templates: %s (%u collision-free ordinary targets)",
            gEnemyRandomizerGlobalTemplates ? "enabled" : "disabled",
            kGlobalEnemyTargetCount);
        Log("randomizer global streaming: %s (native StreamManager, retained handles)",
            gEnemyRandomizerGlobalStreaming ? "enabled" : "disabled");
        Log("randomizer cross-family ground: %s (Pregnant -> live Zombie only)",
            gEnemyRandomizerCrossFamilyGround ? "enabled" : "disabled");
        Log("randomizer difficulty ramp: %s (enhanced enemies, twitchers, sup_expl and the hunter "
            "grow from 1/12 of their weight to all of it over the chapters played)",
            gEnemyRandomizerDifficultyRamp ? "enabled" : "disabled");
        Log("randomizer enemy voice archives: %s", gEnemyRandomizerAudioPreload
                ? "kept resident for the run" : "NOT preloaded (EnemyRandomizerAudioPreload=0, voices come from the level)");
        gDirectPlacementWanted = gEnemyRandomizer && gEnemyRandomizerDirectSpawns;
        Log("randomizer cross-family: %s; directly placed spawners: %s; heavy families "
            "(Pregnant, Infector, Super Brute): %s",
            gEnemyRandomizerCrossFamilyContexts ? "enabled" : "disabled",
            gEnemyRandomizerDirectSpawns ? "enabled" : "disabled",
            gEnemyRandomizerHeavyFamilies ? "enabled" : "disabled");
        DetectMinimalSidecars();
        LoadFamilyWeights();
        Log("placement readout: hotkey VK=0x%02X (0 disables)",
            gPlacementHotkey);
        Log("randomizer vent surfaces: %s",
            gEnemyRandomizerIgnoreVentSurface
                ? "interchangeable (mode decides)"
                : "strict (authored surface required)");
        Log("randomizer context V1: %s, seed=%u, sources=%u, rules=%u, cross-family=%s",
            gEnemyRandomizerContextV1 ? "enabled" : "disabled",
            gEnemyRandomizerSeed, kSpawnCompatibilitySourceCount,
            kSpawnCompatibilityRuleCount,
            gEnemyRandomizerCrossFamilyContexts ? "experimental" : "disabled");
        Log("zero G normal/enhanced variants: %s (seeded 50/50, global pool required)",
            gEnemyRandomizerZeroGVariants ? "enabled" : "disabled");
        if (gExtraSpawnVariant[0] && gExtraSpawnAnySource)
            Log("extra spawn: %u x %s at every known source, %u%% chance",
                gExtraSpawnCount, gExtraSpawnVariant, gExtraSpawnChancePercent);
        else if (gExtraSpawnVariant[0] && gExtraSpawnSourceValid)
            Log("extra spawn: %u x %s at source %08X-%08X-%08X-%08X",
                gExtraSpawnCount, gExtraSpawnVariant,
                gExtraSpawnSource.value[0], gExtraSpawnSource.value[1],
                gExtraSpawnSource.value[2], gExtraSpawnSource.value[3]);
        else if (gExtraSpawnVariant[0] || extraSource[0])
            Log("extra spawn: NOT armed (variant='%s', source='%s')",
                gExtraSpawnVariant, extraSource);
        for (unsigned i = 0; i < gVariantWeightCount; ++i)
            if (gVariantWeights[i].weight != 10)
                Log("randomizer weight: %s = %u (10 is neutral)",
                    gVariantWeights[i].variant, gVariantWeights[i].weight);
        Log("randomizer disabled variants: %s",
            gEnemyRandomizerDisabledVariants[0] ?
                gEnemyRandomizerDisabledVariants : "none");
        Log("randomizer forced variant: %s",
            gEnemyRandomizerForcedVariant[0] ?
                gEnemyRandomizerForcedVariant : "none");
        Log("randomizer failed-spawn retries: %s",
            gEnemyRandomizerRetryFailedSpawns ?
                "EXPERIMENTAL (may repeat scripted side effects)" :
                "disabled (single native handler call)");
        Log("passive spawn catalog: %s", gSpawnCatalog ? "enabled" : "disabled");
        Log("resolver recorder: %s", gSpawnResolverDiagnostics ? "enabled" : "disabled");
        Log("checkpoint changer: %s, hotkey VK=0x%02X",
            gCheckpointChanger ? "enabled" : "disabled",
            gCheckpointChangerHotkey);
        LoadProgressionSettings();
        LoadSuitDebtSettings();
        LoadLimbSettings();
        LoadCrateSettings();
        LoadLogSettings();
        LoadLooseItemSettings();
        LoadCorpseSettings();
        LoadExploderSettings();
        LoadVariantTunings();
        LoadSlotListSettings();
    }
    long gForced = 0;
    long gSkipped = 0;
    // An inline hook, not a mid hook: the decision needs the result of the
    // game's own call, and a hook that only sits in front of the function
    // cannot wait for it. The previous version worked around that by calling
    // the handler an extra time to watch what it built, and only then decided
    // whether to multiply. That extra call happened for every trigger, which
    // is how a second Kendra and a second Hive Mind appeared while the counter
    // still read "0 multiplied": the counter sits behind the decision, the
    // damage was done in front of it.
    safetyhook::InlineHook gSpawnHandler;
    safetyhook::MidHook gSpawnResolverCall;
    safetyhook::MidHook gSpawnResolver;
    safetyhook::MidHook gResolverPreflightPassed;
    safetyhook::MidHook gResolverFilterResult;
    safetyhook::MidHook gResolverFiltersPassed;
    safetyhook::MidHook gResolverCallbackResult;
    safetyhook::MidHook gResolverFactoryEnter;
    safetyhook::MidHook gResolverFactoryResult;
    thread_local uintptr_t gWatchingResolverKey = 0;
    thread_local uintptr_t gWatchingResolverManager = 0;
    thread_local unsigned gWatchingResolverFilterCount = 0;
    thread_local unsigned gWatchingResolverManagerMask = 0;
    thread_local bool gResolverPassedPreflight = false;
    thread_local bool gResolverPassedFilters = false;
    thread_local int gResolverRejectedFilter = -1;
    thread_local uintptr_t gResolverCallback = 0;
    thread_local bool gResolverEnteredFactory = false;
    thread_local uintptr_t gResolverFactoryObject = 0;
    thread_local uintptr_t gLastResolverFactoryObject = 0;
    thread_local int gResolverBypassedFilter = -1;
    safetyhook::MidHook gFilterAnimatedReference;
    safetyhook::MidHook gFilterAnimatedStream;
    safetyhook::MidHook gFilterAnimatedState;
    thread_local bool gFilterSawReference = false;
    thread_local unsigned gFilterReference[4]{};
    thread_local uintptr_t gFilterStreamId = 0;
    thread_local int gFilterState = -1;

    constexpr unsigned kSeenSpawnerCapacity = 512;
    uintptr_t gSeen[kSeenSpawnerCapacity]{};
    volatile LONG gSeenCount = 0;
    volatile LONG gSeenWriteCursor = 0;
    long gPatched = 0;

    unsigned SeenSpawnerSnapshotCount()
    {
        const LONG count = InterlockedCompareExchange(&gSeenCount, 0, 0);
        if (count <= 0)
            return 0;
        return static_cast<unsigned>(count) < kSeenSpawnerCapacity ?
            static_cast<unsigned>(count) : kSeenSpawnerCapacity;
    }

    bool StillASpawner(uintptr_t spawner)
    {
        unsigned vtable = 0;
        return spawner &&
               ReadProcessMemory(GetCurrentProcess(),
                                 reinterpret_cast<void*>(spawner), &vtable,
                                 sizeof(vtable), nullptr) &&
               vtable == kFuncSpawnVtable;
    }

    uintptr_t LiveSpawnerForTarget(const Guid128& target, uintptr_t exclude)
    {
        const unsigned seenCount = SeenSpawnerSnapshotCount();
        for (unsigned i = 0; i < seenCount; ++i)
        {
            const uintptr_t candidate = gSeen[i];
            if (candidate == exclude || !StillASpawner(candidate))
                continue;
            Guid128 candidateTarget{};
            if (ReadProcessMemory(GetCurrentProcess(),
                                  reinterpret_cast<void*>(candidate + kTargetGuid),
                                  &candidateTarget, sizeof(candidateTarget), nullptr) &&
                SameGuid(candidateTarget, target))
                return candidate;
        }
        return 0;
    }

    unsigned LiveSpawnerCount()
    {
        unsigned live = 0;
        const unsigned seenCount = SeenSpawnerSnapshotCount();
        for (unsigned i = 0; i < seenCount; ++i)
            if (StillASpawner(gSeen[i]))
                ++live;
        return live;
    }

    constexpr unsigned kTargetMapOffset = 0x5C;
    struct RetailActorVtable
    {
        const char* actorClass;
        uintptr_t vtable;
        uint32_t classSignature;
    };

    // Complete set used by kWanted. Recovered from the retail PC class
    // registration -> factory -> constructor chain in
    // DeadSpace1_LiveDump.exe, not transplanted from the Xbox PDB. For
    // composite actors (Guardian, HiveMind and tentacles) this is the vtable
    // written to the primary returned actor object; embedded helper-object
    // vtables are intentionally excluded.
    constexpr RetailActorVtable kRetailActorVtables[] = {
        {"Zombie",             0x00C678E0, 0x04B57466},
        {"Lurker",             0x00C65C18, 0x52DEFB4F},
        {"Brute",              0x00C61708, 0xF2968656},
        {"Leaper",             0x00C65408, 0xFF9B7C15},
        {"Exploder",           0x00C69580, 0x73553751},
        {"Infector",           0x00C64DD8, 0xC2557958},
        {"Pregnant",           0x00C66478, 0xF8983DBD},
        {"Guardian",           0x00C69A40, 0x848FAAF1},
        {"GuardianPod",        0x00C69E18, 0xA1D03DF4},
        {"GuardianTentacle",   0x00C6A360, 0xFD5A3423},
        {"DividerCore",        0x00C68550, 0xDD2CD3F8},
        {"DividerPart",        0x00C68AA0, 0x332094AC},
        {"SwarmEnemy",         0x00C6A630, 0x6100F210},
        {"StaticEnemy",        0x00C66BE0, 0xB9D7B35A},
        {"GenericNPC",         0x00C63D30, 0x240E5B6A},
        {"NeutralNPC",         0x00C68FB8, 0x82C7DF9A},
        {"Slug",               0x00C6B3C0, 0x1C336D6B},
        {"SlugTentacle",       0x00C6B740, 0xAB3E829D},
        {"HiveMind",           0x00C6AB68, 0x1F0BAFA2},
        {"HiveMindEye",        0x00C6B010, 0xE56CE5EF},
        {"Leviathan",          0x00C67DB8, 0x3A1315B6},
        {"LeviathanTentacle", 0x00C681C8, 0x0858D4E8},
        {"DragTentacle",       0x00C787E0, 0x736E8D86},
        {"SlamDragTentacle",   0x00C66928, 0xA10D070B},
        {"Hunter",             0x00C64680, 0x4480BEE0},
    };

    bool ReadTargetKeyGuid(uintptr_t node, Guid128* guid)
    {
        if (!node || !guid)
            return false;
        unsigned flags = 0;
        int relative = 0;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(node + 0x1C), &flags,
                               sizeof(flags), nullptr))
            return false;

        uintptr_t address = 0;
        if (flags & 1)
        {
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(node + 0x24), &relative,
                                   sizeof(relative), nullptr) || !relative)
                return false;
            address = node + 0x24 + relative;
        }
        else
        {
            uintptr_t cursor = node + 0x20;
            for (unsigned step = 0; step < 128; ++step)
            {
                unsigned sentinel = 0;
                if (!ReadProcessMemory(GetCurrentProcess(),
                                       reinterpret_cast<void*>(cursor), &relative,
                                       sizeof(relative), nullptr) ||
                    !ReadProcessMemory(GetCurrentProcess(),
                                       reinterpret_cast<void*>(cursor + 4), &sentinel,
                                       sizeof(sentinel), nullptr))
                    return false;
                if (sentinel == 0x40000000)
                {
                    address = cursor + 8;
                    break;
                }
                if (!relative)
                    return false;
                cursor += relative;
            }
        }
        return address &&
               ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                                 guid, sizeof(*guid), nullptr);
    }

    uintptr_t TargetKeyFor(const Guid128& target)
    {
        uintptr_t manager = 0;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(kSimManagerGlobal), &manager,
                               sizeof(manager), nullptr) || !manager)
            return 0;
        const uintptr_t map = manager + kTargetMapOffset;
        uintptr_t buckets = 0;
        unsigned bucketCount = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(map),
                               &buckets, sizeof(buckets), nullptr) ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(map + 4),
                               &bucketCount, sizeof(bucketCount), nullptr) ||
            !buckets || !bucketCount || bucketCount > 0x100000)
            return 0;

        uint32_t hash = 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(&target);
        for (unsigned i = 0; i < sizeof(target); ++i)
            hash = hash * 0x1003Fu + bytes[i];

        uintptr_t node = 0;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(buckets +
                                                       (hash % bucketCount) * 4),
                               &node, sizeof(node), nullptr))
            return 0;
        for (unsigned step = 0; node && step < 1024; ++step)
        {
            Guid128 found{};
            if (ReadTargetKeyGuid(node, &found) && SameGuid(found, target))
                return node;
            uintptr_t next = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(node + 0x18), &next,
                                   sizeof(next), nullptr) || next == node)
                break;
            node = next;
        }
        return 0;
    }

    unsigned ResidentSlotFor(const Guid128& target)
    {
        const uintptr_t key = TargetKeyFor(target);
        uintptr_t object = 0;
        uintptr_t vtable = 0;
        if (!key || !ReadProcessMemory(GetCurrentProcess(),
                                       reinterpret_cast<void*>(key + 0x14),
                                       &object, sizeof(object), nullptr))
            return 0xFFFF;

        if (object && ReadProcessMemory(GetCurrentProcess(),
                                       reinterpret_cast<void*>(object), &vtable,
                                       sizeof(vtable), nullptr))
            for (const RetailActorVtable& actor : kRetailActorVtables)
                if (vtable == actor.vtable)
                    return ActorSlotNamed(actor.actorClass);

        // A freshly streamed SpawnInfo target has no constructed runtime
        // actor at node+0x14 yet. Its reflected PC class signature is stored
        // directly in the target-key packet at node+0x28. This is not a
        // weaker name guess: the values below come from the same retail
        // RegisterClass records as the vtables and the full dump confirms the
        // synthetic Twitcher keys carry Zombie signature 04B57466 here.
        uint32_t signature = 0;
        if (ReadProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<void*>(key + 0x28), &signature,
                              sizeof(signature), nullptr))
            for (const RetailActorVtable& actor : kRetailActorVtables)
                if (signature == actor.classSignature)
                    return ActorSlotNamed(actor.actorClass);
        return 0xFFFF;
    }

    bool ResolveStreamManager(void** managerOut)
    {
        if (managerOut)
            *managerOut = nullptr;
        uintptr_t manager = 0;
        uintptr_t vtable = 0;
        uintptr_t confirmation = 0;
        if (!managerOut ||
            !ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(kStreamManagerGlobal),
                               &manager, sizeof(manager), nullptr) || !manager ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(manager),
                               &vtable, sizeof(vtable), nullptr) ||
            vtable != kStreamManagerVtable ||
            !ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(kStreamManagerGlobal),
                               &confirmation, sizeof(confirmation), nullptr) ||
            confirmation != manager)
            return false;
        *managerOut = reinterpret_cast<void*>(manager);
        return true;
    }

    int StreamStateFor(void* managerObject, uint32_t streamId)
    {
        const uintptr_t stream = StreamObjectFor(managerObject, streamId);
        int state = -1;
        if (!stream || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(stream + 0x1C), &state,
                                          sizeof(state), nullptr))
            return -1;
        return state;
    }

    uintptr_t StreamObjectFor(void* managerObject, uint32_t streamId)
    {
        const uintptr_t manager = reinterpret_cast<uintptr_t>(managerObject);
        uintptr_t buckets = 0;
        unsigned bucketCount = 0;
        if (!manager || !streamId ||
            !ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(manager + 0x54),
                               &buckets, sizeof(buckets), nullptr) || !buckets ||
            !ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(manager + 0x58),
                               &bucketCount, sizeof(bucketCount), nullptr) ||
            !bucketCount)
            return 0;

        uintptr_t node = 0;
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(buckets +
                                   (streamId % bucketCount) * sizeof(uintptr_t)),
                               &node, sizeof(node), nullptr))
            return 0;
        for (unsigned step = 0; node && step < 4096; ++step)
        {
            uint32_t found = 0;
            uintptr_t stream = 0;
            uintptr_t next = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(node), &found,
                                   sizeof(found), nullptr) ||
                !ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(node + 4), &stream,
                                   sizeof(stream), nullptr) ||
                !ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(node + 8), &next,
                                   sizeof(next), nullptr))
                return 0;
            if (found == streamId)
                return stream;
            if (next == node)
                break;
            node = next;
        }
        return 0;
    }

    unsigned ActiveGlobalTargetStreamCount()
    {
        void* manager = nullptr;
        if (!ResolveStreamManager(&manager))
            return 0;
        unsigned active = 0;
        for (unsigned i = 0; i < gGlobalTargetHandleCount; ++i)
            if (StreamStateFor(manager, gGlobalTargetHandles[i]) == 3)
                ++active;
        return active;
    }

    bool EnsureGlobalEnemyCharactersPinned(void* manager)
    {
        if (gGlobalCharactersPinned)
            return true;
        if (!gEnemyRandomizerGlobalStreaming ||
            !gEnemyRandomizerGlobalTemplates || !manager)
            return false;
        if (InterlockedCompareExchange(&gGlobalStreamPhase, 0, 0) !=
                kGlobalStreamTargetsActivated ||
            !gGlobalTargetHandleCount ||
            ActiveGlobalTargetStreamCount() != gGlobalTargetHandleCount ||
            GetTickCount64() - gGlobalStreamRequestedTick <
                kGlobalCharacterPreloadSettleMs ||
            gGlobalCharacterPreloadAttempts >= kGlobalCharacterPreloadAttemptLimit)
            return false;

        // The settle window above says the sidecars are done, not that the
        // loading screen is.  Requesting a retail archive natively while the
        // level loader is still running deadlocks it, so require real evidence
        // of gameplay: the loading-screen archive has been released, or a
        // FuncSpawn has already handled an event, which can only happen in a
        // running world.
        const bool loadingScreenReleased =
            gLoadingScreenHandle &&
            StreamStateFor(manager, gLoadingScreenHandle) != 3;

        // The level archive is loaded and FuncSpawn construction has gone
        // quiet: the world exists, nothing is still being built, and the
        // loader is no longer in the phase where it waited on a stream that
        // waited on it.  This lands during the loading screen, so no encounter
        // is spent on unlocking the pin.
        const LONG lastSpawner = InterlockedCompareExchange(&gLastSpawnerTick,
                                                            0, 0);
        const ULONGLONG now = GetTickCount64() & 0x7FFFFFFF;
        const bool worldBuilt =
            gPinAtWorldBuilt && gLevelArchiveHandle &&
            StreamStateFor(manager, gLevelArchiveHandle) == 3 &&
            gSpawners > 0 && lastSpawner &&
            now >= static_cast<ULONGLONG>(lastSpawner) + kSpawnerQuietMs;

        if (!worldBuilt && !loadingScreenReleased &&
            !InterlockedCompareExchange(&gSpawnHandlerEntered, 0, 0))
            return false;
        ++gGlobalCharacterPreloadAttempts;

        bool completed = false;
        gGlobalCharacterHandleCount = 0;
        gGlobalCharacterRequiredCount = 0;
        gGlobalCharacterIdentityCount = 0;
        gGlobalCharacterFailedIndex = 0xFFFFFFFFu;
        gGlobalAudioHandleCount = 0;
        gGlobalAudioNativeCount = 0;
        gGlobalAudioRequiredCount = 0;
        gGlobalAudioFailedIndex = 0xFFFFFFFFu;
        gDispatchingGlobalStreams = true;
        __try
        {
            const auto load = reinterpret_cast<StreamLoadFn>(kStreamManagerLoad);
            for (unsigned i = 0; i < kGlobalEnemyCharacterStreamCount; ++i)
            {
                const GlobalEnemyCharacterStream& stream =
                    kGlobalEnemyCharacterStreams[i];
                if (!CharacterStreamRequiredByPolicy(stream))
                    continue;
                ++gGlobalCharacterRequiredCount;

                // Measured against the game's own archive requests: every
                // retail load (text_assets, frontend, global_assets, the
                // chapter archive) is flags 3, priority 0, dependencies null
                // and context null.  The identity and context arguments belong
                // to the loose-file branch.  Passing them together with flags 3
                // registered the archive root - filter 0 found the stream at
                // last - but routed the request through the wrong branch, where
                // it stayed at state 0 forever.  A retail character archive is
                // a TOC entry, so its path is the whole request.  stream.guid
                // remains the expected root for verification only.
                // The native branch is for retail TOC entries.  An archive
                // we ship ourselves is a loose file no matter that it carries
                // a root of its own, so it has to take the custom branch -
                // sent down the native one it simply returns zero, which is
                // what kept our second exploder asset at characters=0/1.
                const bool shippedByUs =
                    PathMentions(stream.path, "ds1_randomizer_assets");
                const uint32_t rootGuid = stream.guid;
                const char* base = std::strrchr(stream.path, '\\');
                const bool isSuperExploder =
                    base && _stricmp(base + 1, "sup_expl") == 0;
                // Three shapes, and the third is the one that was missing.
                // A retail TOC entry is just a path.  A custom archive with
                // no root of its own is a path on the custom branch.  An
                // archive we ship that does have a root needs the same call
                // the enemy-pool sidecars already use and which the log shows
                // working - custom flags plus the root and the game
                // directory.  Sending it down the native branch instead is
                // what left our second exploder asset at characters=0/1 for
                // forty-eight attempts.
                const uint32_t handle =
                    isSuperExploder && gSuperExploderHandle
                        ? gSuperExploderHandle
                        : shippedByUs
                        ? load(manager, stream.path, 0.0f,
                               kStreamLoadRetainedCustom, &rootGuid, gGameRoot,
                               0)
                        : (stream.guid
                               ? load(manager, stream.path, 0.0f,
                                      kStreamLoadRetainedNative, nullptr,
                                      nullptr, 0)
                               : load(manager, stream.path, 0.0f,
                                      kStreamLoadRetainedCustom, nullptr,
                                      nullptr, 0));
                if (!handle)
                {
                    gGlobalCharacterFailedIndex = i;
                    break;
                }
                if (stream.guid)
                    ++gGlobalCharacterIdentityCount;
                gGlobalCharacterHandles[gGlobalCharacterHandleCount++] = handle;
                if (isSuperExploder)
                {
                    gSuperExploderHandle = handle;
                    Log("super exploder archive retained after world build: "
                        "handle=%08X state=%d root=%08X",
                        handle, StreamStateFor(manager, handle), stream.guid);
                }
            }
            if (gGlobalCharacterHandleCount == gGlobalCharacterRequiredCount)
            {
                // swarm.str stays out of the counts: failing it only costs the
                // swarm, which the guard then skips. Native first, see below.
                gSwarmCompanionHandle = 0;
                if (const GlobalEnemyCharacterStream* swarm = SwarmCompanionStream())
                {
                    gSwarmCompanionHandle = load(manager, swarm->path, 0.0f,
                                                 kStreamLoadRetainedNative, nullptr, nullptr, 0);
                    if (!gSwarmCompanionHandle)
                        gSwarmCompanionHandle = load(manager, swarm->path, 0.0f,
                                                     kStreamLoadRetainedCustom, nullptr, nullptr, 0);
                }
                // Character packages contain animation-event BNKs, but DS1's
                // voiced attacks, movement and deaths are held in separate
                // audio_char_streams SBKs.  These are retail TOC entries with
                // no SimGroup root, so use the path lookup branch and retain
                // every returned lease for the process lifetime.
                for (unsigned i = 0; i < kGlobalEnemyAudioStreamCount; ++i)
                {
                    const GlobalEnemyAudioStream& stream =
                        kGlobalEnemyAudioStreams[i];
                    if (!AudioStreamRequiredByPolicy(stream))
                        continue;
                    ++gGlobalAudioRequiredCount;
                    // A retail archive takes the native branch, path only,
                    // like the character archives. The custom branch without
                    // an id left the stream at state 3 but never put its
                    // bank into the 96 MiB sound bank heap (0x1D350000 live,
                    // 2026-10-22): lurker_audio, hunter_audio and the rest
                    // were missing, and the level got that empty stream for
                    // its own request. Ours is a loose file we ship, so it
                    // needs the shape the enemy-pool sidecars use: the
                    // explicit id and the game directory.
                    const bool ours = PathMentions(stream.path,
                                                   "ds1_randomizer_assets");
                    const uint32_t bankGuid = stream.bankGuid;
                    uint32_t handle = ours
                        ? load(manager, stream.path, 0.0f,
                               kStreamLoadRetainedCustom, &bankGuid,
                               gGameRoot, 0)
                        : load(manager, stream.path, 0.0f,
                               kStreamLoadRetainedNative, nullptr, nullptr, 0);
                    if (handle && !ours)
                        ++gGlobalAudioNativeCount;
                    if (!handle && !ours)
                        handle = load(manager, stream.path, 0.0f,
                                      kStreamLoadRetainedCustom, nullptr, nullptr, 0);
                    if (!handle)
                    {
                        gGlobalAudioFailedIndex = i;
                        break;
                    }
                    gGlobalAudioHandles[gGlobalAudioHandleCount++] = handle;
                }
            }
            completed = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            completed = false;
        }
        gDispatchingGlobalStreams = false;

        // "None required" is not a miss.  A run can need a character archive
        // and no audio archive at all - our own second exploder asset is
        // exactly that shape - and the old test read audio=0/0 as a permanent
        // failure, so the phase deferred until its attempt budget ran out and
        // then reported a failure for work that had nothing left to do.  Only
        // a genuine shortfall counts now, plus the case where nothing at all
        // was asked for, which still means the pool is not ready.
        const bool charactersReady =
            gGlobalCharacterHandleCount == gGlobalCharacterRequiredCount;
        const bool audioReady =
            gGlobalAudioHandleCount == gGlobalAudioRequiredCount;
        const bool nothingRequired =
            !gGlobalCharacterRequiredCount && !gGlobalAudioRequiredCount;
        if (!completed || nothingRequired || !charactersReady || !audioReady)
        {
            // A retail archive requested before its dependency graph exists
            // simply returns zero.  That is a "not yet", not a defect, so keep
            // the phase alive and let a later StreamManager call try again.
            // Only an exhausted attempt budget is a real failure.
            const bool exhausted =
                gGlobalCharacterPreloadAttempts >=
                    kGlobalCharacterPreloadAttemptLimit;
            if (!exhausted)
            {
                if (gGlobalCharacterPreloadAttempts <= 3 ||
                    gGlobalCharacterPreloadAttempts % 16 == 0)
                    Log("randomizer persistent resource preload deferred (attempt %u): characters=%u/%u audio=%u/%u",
                        gGlobalCharacterPreloadAttempts,
                        gGlobalCharacterHandleCount,
                        gGlobalCharacterRequiredCount, gGlobalAudioHandleCount,
                        gGlobalAudioRequiredCount);
                return false;
            }
            InterlockedExchange(&gGlobalStreamPhase, kGlobalStreamFailed);
            Log("randomizer persistent resource preload failed: characters=%u/%u audio=%u/%u completed=%d",
                gGlobalCharacterHandleCount, gGlobalCharacterRequiredCount,
                gGlobalAudioHandleCount, gGlobalAudioRequiredCount,
                completed ? 1 : 0);
            if (gGlobalCharacterFailedIndex < kGlobalEnemyCharacterStreamCount)
                Log("randomizer persistent character preload failed archive: %s",
                    kGlobalEnemyCharacterStreams[gGlobalCharacterFailedIndex].path);
            if (gGlobalAudioFailedIndex < kGlobalEnemyAudioStreamCount)
            {
                const GlobalEnemyAudioStream& failed =
                    kGlobalEnemyAudioStreams[gGlobalAudioFailedIndex];
                Log("randomizer persistent audio preload failed archive: %s (SBK=%08X families=%s)",
                    failed.path, failed.bankGuid,
                    failed.families ? failed.families : "unknown");
            }
            return false;
        }

        // Deliberately does not touch the stream phase.  Target sidecars are
        // loose archives whose SimManager keys only register while the level
        // is being constructed; the retail character archives can only be
        // pinned once that construction is under way.  Coupling the two made
        // publication miss its window entirely.  Residency is still enforced,
        // but at the point the game enforces it: PackageReadyFor requires the
        // pin before any candidate is eligible.
        gGlobalCharactersPinned = true;
        // Restart the readiness deadline.  It measures how long the pool has
        // had to become usable, but the character archives are only requested
        // here - and the pin deliberately waits for a live world, which can be
        // long after the sidecars were published.  Leaving the old clock
        // running failed a pool whose archives were still at state 0 in the
        // very second the deadline expired.
        gGlobalStreamRequestedTick = GetTickCount64();
        Log("randomizer pin unlocked by: world built=%s, loading screen "
            "released=%s, handler seen=%ld",
            (gLevelArchiveHandle &&
             StreamStateFor(manager, gLevelArchiveHandle) == 3) ? "yes" : "no",
            (gLoadingScreenHandle &&
             StreamStateFor(manager, gLoadingScreenHandle) != 3) ? "yes" : "no",
            InterlockedCompareExchange(&gSpawnHandlerEntered, 0, 0));
        Log("randomizer persistent resource preload requested: %u policy character (%u package identities) + %u audio archives; leases retained across save/checkpoint reloads",
            gGlobalCharacterHandleCount, gGlobalCharacterIdentityCount,
            gGlobalAudioHandleCount);
        Log("randomizer voice archives: %u of %u on the native branch, the rest on the custom one",
            gGlobalAudioNativeCount, gGlobalAudioHandleCount);
        // The root hash printed here is the value SimManager filter 0 looks up
        // for every spawn target that references this package.
        for (unsigned i = 0, handle = 0;
             i < kGlobalEnemyCharacterStreamCount &&
             handle < gGlobalCharacterHandleCount; ++i)
        {
            const GlobalEnemyCharacterStream& stream =
                kGlobalEnemyCharacterStreams[i];
            if (!CharacterStreamRequiredByPolicy(stream))
                continue;
            const char* base = std::strrchr(stream.path, '\\');
            // The handle's own state only says the stream exists.  What
            // decides a spawn is whether the root resolves to a loaded stream,
            // which is the lookup filter 0 performs, so log that too.
            const uint32_t rootStream =
                ResourceStreamIdFor(manager, stream.guid);
            const int archiveState =
                StreamStateFor(manager, gGlobalCharacterHandles[handle]);
            gGlobalCharacterLent[i] = !CharacterArchiveOwned(base ? base + 1 : stream.path);
            Log("randomizer character archive: %s id=%08X state=%d | root=%08X "
                "-> stream=%08X state=%d, %s",
                base ? base + 1 : stream.path, gGlobalCharacterHandles[handle],
                archiveState, stream.guid, rootStream,
                rootStream ? StreamStateFor(manager, rootStream) : -1,
                gGlobalCharacterLent[i] ? "the level's, lent" : "ours");
            if (base && _stricmp(base + 1, "pregnant") == 0 && SwarmCompanionStream())
                Log("randomizer character archive: swarm (for the pregnant) id=%08X state=%d",
                    gSwarmCompanionHandle,
                    gSwarmCompanionHandle ? StreamStateFor(manager, gSwarmCompanionHandle) : -1);
            if (base && _stricmp(base + 1, "sup_expl") == 0)
            {
                const bool resident = archiveState == 3 && rootStream != 0;
                if (resident != gSuperExploderResident)
                    Log("super exploder asset is %s",
                        resident ? "resident - exploders may now be renamed"
                                 : "NOT resident - exploders stay as they are");
                gSuperExploderResident = resident;
            }
            ++handle;
        }
        return true;
    }

    void ProgressGlobalEnemyStreaming()
    {
        if (!gEnemyRandomizerGlobalStreaming || !gEnemyRandomizerGlobalTemplates)
            return;

        LONG phase = InterlockedCompareExchange(&gGlobalStreamPhase, 0, 0);
        if (phase == kGlobalStreamFailed)
            return;

        if (gGlobalStreamAwaitingLevel)
        {
            if (gSpawners <= gGlobalPoolLostSpawnerMark || !LiveSpawnerCount())
                return;
            gGlobalStreamAwaitingLevel = false;
            Log("randomizer reload recovery: %ld new FuncSpawn objects since the "
                "pool was lost; restreaming into the new world",
                gSpawners - gGlobalPoolLostSpawnerMark);
        }

        // Synthetic SpawnInfo targets belong to a live gameplay world.  The
        // persistent character packages above are safe in frontend/loading
        // states, but publishing target sidecars there races SimManager
        // teardown and previously caused the main-menu reload crash.
        if (phase != kGlobalStreamReady && !LiveSpawnerCount() &&
            !gRestoreDrivenPublish)
            return;

        // The character pin has its own gate and must be attempted on every
        // pass.  It used to sit at the very bottom of this function, which was
        // only reached while no phase branch applied; once target publication
        // runs from the idle phase, that fall-through never happens again.
        if (!gGlobalCharactersPinned)
        {
            void* pinManager = nullptr;
            if (ResolveStreamManager(&pinManager))
                EnsureGlobalEnemyCharactersPinned(pinManager);
        }

        if (phase == kGlobalStreamReady)
        {
            RefreshGlobalTargetReadiness();
            unsigned requiredVariants = 0;
            unsigned registeredVariants = 0;
            char lost[256]{};
            size_t lostUsed = 0;
            for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
            {
                const GlobalEnemyTarget& candidate = kGlobalEnemyTargets[i];
                if (!PolicyUsesGlobalVariant(candidate) || CharacterArchiveLentAtPin(candidate.variant))
                    continue;
                bool first = true;
                for (unsigned previous = 0; previous < i; ++previous)
                    if (SameGlobalVariant(candidate,
                                          kGlobalEnemyTargets[previous]))
                    {
                        first = false;
                        break;
                    }
                if (!first)
                    continue;
                ++requiredVariants;
                bool registered = false;
                for (unsigned probe = i; probe < kGlobalEnemyTargetCount; ++probe)
                    if (SameGlobalVariant(candidate,
                                          kGlobalEnemyTargets[probe]) &&
                        gGlobalEnemyTargetReady[probe])
                    {
                        registered = true;
                        break;
                    }
                if (registered)
                    ++registeredVariants;
                else if (lostUsed + 48 < sizeof(lost))
                    lostUsed += std::snprintf(lost + lostUsed, sizeof(lost) - lostUsed, "%s%s/%s",
                                              lostUsed ? ", " : "", candidate.actorClass, candidate.variant);
            }
            if (registeredVariants &&
                registeredVariants >= gGlobalReadyVariantBaseline)
                return;

            // Checkpoint/save reloads tear down level-owned synthetic target
            // keys even though our old StreamManager handles remain nonzero.
            // Treat those handles as stale and replay both stages through the
            // stock game-thread stream path.
            Log("randomizer global pool lost after reload: %u/%u policy variants remain (gone: %s); restreaming",
                registeredVariants, requiredVariants, lost[0] ? lost : "none listed");
            gGlobalEnemyPoolLoaded = false;
            gGlobalReadyVariantBaseline = 0;
            ClearGlobalTargetReadiness();
            gGlobalTargetHandleCount = 0;
            gGlobalCharacterFailedIndex = 0xFFFFFFFFu;
            gGlobalStreamRequestedTick = 0;
            // The character leases do not survive a world change either.  After
            // a checkpoint reload the target keys came back but every variant
            // whose archive we had pinned dropped out, while the two whose
            // archives the level itself owns stayed - so the pin has to run
            // again.  Clearing the handler-seen evidence with it keeps the new
            // pin behind a real in-game trigger, which is what stops a native
            // request from deadlocking the loading screen.
            gGlobalCharactersPinned = false;
            gGlobalCharacterPreloadAttempts = 0;
            // The super exploder archive is leased the same way and was the
            // one handle nobody cleared.  A stale handle here is not inert:
            // it gets handed back to the stream manager, which then unlinks a
            // node that is already gone - three reproducible crash sites, all
            // of them list surgery around 0x0042C090, all on a checkpoint
            // restart or a room change.
            gSuperExploderHandle = 0;
            gSuperExploderResident = false;
            gSuperExploderReloads = 0;
            // The variant view table is keyed on pointers from
            // the old world; clearing it here is what keeps a
            // recycled AnimView from inheriting a rate.
            ResetVariantRuntime();
            InterlockedExchange(&gSpawnHandlerEntered, 0);
            for (ResidencySample& sample : gCharacterResidency)
                sample = ResidencySample{};
            phase = gGlobalCharactersPinned ? kGlobalStreamCharactersRequested
                                            : kGlobalStreamIdle;
            InterlockedExchange(&gGlobalStreamPhase, phase);
            // Deliberately no cooldown and no waiting for a later pass.  A
            // synthetic target key only registers while the world is being
            // constructed, and after a death the constructors run in the same
            // burst that makes this loss visible.  The old one-second wait
            // outlived that burst, which is why the pool never came back after
            // a checkpoint restart and the randomizer silently fell back to
            // replacing level-local Zombies with level-local Zombies.
            // LiveSpawnerCount above already proved the new world exists, so
            // fall through and republish in this very call.
            gGlobalStreamAwaitingLevel = true;
            gGlobalPoolLostSpawnerMark = gSpawners;
            gGlobalPoolLostTick = GetTickCount64();
            return;
        }

        // Character archives are asynchronous.  Publishing actor target
        // sidecars in the same call can register their keys before the
        // referenced character objects exist, leaving a permanent null
        // runtime object.  Dispatch targets only from a later game-thread
        // callback, after the character requests have had time to settle.
        if (phase == kGlobalStreamIdle ||
            phase == kGlobalStreamCharactersRequested)
        {
            const ULONGLONG elapsed = gGlobalStreamRequestedTick ?
                GetTickCount64() - gGlobalStreamRequestedTick : 0;

            // Publication no longer waits for the character pin.  A sidecar
            // key registers only while the level is under construction, and
            // that window closes long before the pin can succeed.
            if (gEnemyRandomizerDiagnostics)
                Log("randomizer publishing target sidecars %llu ms after level start (characters pinned: %s)",
                    elapsed, gGlobalCharactersPinned ? "yes" : "not yet");

            void* manager = nullptr;
            if (!ResolveStreamManager(&manager))
            {
                InterlockedExchange(&gGlobalStreamPhase, kGlobalStreamFailed);
                Log("randomizer global target stream failed: StreamManager identity unavailable");
                return;
            }

            bool completed = false;
            gGlobalTargetHandleCount = 0;
            std::memset(gGlobalTargetHandles, 0,
                        sizeof(gGlobalTargetHandles));
            gGlobalStreamRequestedTick = GetTickCount64();
            InterlockedExchange(&gGlobalStreamPhase,
                                kGlobalStreamTargetsActivated);
            gDispatchingGlobalStreams = true;
            __try
            {
                const auto load = reinterpret_cast<StreamLoadFn>(kStreamManagerLoad);

                // Custom character resources only join the world's resource
                // registry while it is being constructed. The trimmed
                // Super Exploder archive is safe here because shared HKX,
                // VFX and BNK entries were removed from it.
                if (gSuperExploderWanted)
                {
                    constexpr uint32_t superRoot = 0xD5100010u;
                    const uint32_t handle = load(
                        manager, "ds1_randomizer_assets\\sup_expl", 0.0f,
                        kStreamLoadRetainedCustom, &superRoot, gGameRoot, 0);
                    if (handle)
                    {
                        gSuperExploderHandle = handle;
                        gSuperExploderLastRootStream = 0xFFFFFFFFu;
                        gSuperExploderLastRootState = -2;
                        Log("super exploder archive requested during world "
                            "construction: handle=%08X state=%d root=%08X",
                            handle, StreamStateFor(manager, handle), superRoot);
                    }
                    else
                        Log("super exploder early archive request returned zero; "
                            "the post-build retry remains armed");
                }

                // Only the donor archives an enabled feature can actually
                // select.  All eleven used to be pinned unconditionally - about
                // twelve megabytes held for the whole session - even with the
                // enemy randomizer off, when the super exploder needs two of
                // them.  In a 32-bit process that already fails thirty-two byte
                // allocations on a room change, that is address space spent on
                // targets nothing can choose.
                bool wanted[kGlobalEnemyTargetStreamCount]{};
                unsigned skipped = 0;
                gGlobalTargetRequiredCount = 0;
                for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
                {
                    const GlobalEnemyTarget& target = kGlobalEnemyTargets[i];
                    if (!GlobalTargetSelectable(target))
                        continue;
                    // The donor index is the low byte of the target's first
                    // GUID word, and stream i carries donor i.
                    const unsigned donor = target.target.value[0] & 0xFFu;
                    if (donor < kGlobalEnemyTargetStreamCount)
                        wanted[donor] = true;
                }
                for (unsigned i = 0; i < kGlobalEnemyTargetStreamCount; ++i)
                {
                    if (!wanted[i])
                    {
                        ++skipped;
                        continue;
                    }
                    ++gGlobalTargetRequiredCount;
                    const GlobalEnemyTargetStream& targetStream =
                        kGlobalEnemyTargetStreams[i];
                    const uint32_t streamGuid = targetStream.guid;
                    const uint32_t handle = load(
                        manager, targetStream.path, 0.0f,
                        kStreamLoadRetainedCustom, &streamGuid, gGameRoot, 0);
                    if (!handle)
                        break;
                    gGlobalTargetHandles[gGlobalTargetHandleCount++] = handle;
                }
                if (skipped)
                    Log("randomizer global targets: %u of %u donor archive(s) "
                        "skipped, nothing enabled can select them",
                        skipped, kGlobalEnemyTargetStreamCount);
                completed = true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                completed = false;
            }
            gDispatchingGlobalStreams = false;
            // Against what we set out to register, not against the whole
            // table.  Since the donor archives became demand-driven the two
            // differ, and comparing with the table made every run report a
            // failure and skip ProgressGlobalEnemyStreaming entirely.
            if (!completed ||
                gGlobalTargetHandleCount != gGlobalTargetRequiredCount)
            {
                InterlockedExchange(&gGlobalStreamPhase, kGlobalStreamFailed);
                Log("randomizer global target stream failed: target_streams=%u/%u completed=%d",
                    gGlobalTargetHandleCount, gGlobalTargetRequiredCount,
                    completed ? 1 : 0);
                return;
            }
            Log("randomizer global target archives registered after %llu ms character warm-up: %u streams",
                elapsed, gGlobalTargetHandleCount);
            ProgressGlobalEnemyStreaming();
            return;
        }

        // Registration in SimManager is the actual readiness contract. A
        // nonzero StreamManager handle alone only means the request exists.
        const unsigned registered = RefreshGlobalTargetReadiness();
        const unsigned activeTargetStreams = ActiveGlobalTargetStreamCount();
        static unsigned lastActiveTargetStreams = 0xFFFFFFFFu;
        if (gEnemyRandomizerDiagnostics &&
            activeTargetStreams != lastActiveTargetStreams)
        {
            lastActiveTargetStreams = activeTargetStreams;
            Log("randomizer target stream activation: %u/%u at state 3",
                activeTargetStreams, gGlobalTargetHandleCount);
            void* streamManager = nullptr;
            if (ResolveStreamManager(&streamManager))
                for (unsigned i = 0; i < gGlobalTargetHandleCount; ++i)
                    Log("randomizer target stream[%u]: id=%08X state=%d",
                        i, gGlobalTargetHandles[i],
                        StreamStateFor(streamManager, gGlobalTargetHandles[i]));
        }

        // Dependent child actors and passive providers are intentionally part
        // of the sidecar but are not necessarily published as independent
        // SimManager spawn targets. Readiness is therefore one resident target
        // per candidate class/variant allowed by the current policy, not a
        // mechanically impossible 30/30 entity count. CandidateFor still
        // verifies the exact selected key before every substitution.
        unsigned requiredVariants = 0;
        unsigned registeredVariants = 0;
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
        {
            const GlobalEnemyTarget& candidate = kGlobalEnemyTargets[i];
            if (!PolicyUsesGlobalVariant(candidate) || CharacterArchiveLentAtPin(candidate.variant))
                continue;
            bool first = true;
            for (unsigned previous = 0; previous < i; ++previous)
                if (SameGlobalVariant(candidate, kGlobalEnemyTargets[previous]))
                {
                    first = false;
                    break;
                }
            if (!first)
                continue;
            ++requiredVariants;
            for (unsigned probe = i; probe < kGlobalEnemyTargetCount; ++probe)
                if (SameGlobalVariant(candidate, kGlobalEnemyTargets[probe]) &&
                    gGlobalEnemyTargetReady[probe])
                {
                    ++registeredVariants;
                    break;
                }
        }
        if (requiredVariants && registeredVariants == requiredVariants &&
            activeTargetStreams == gGlobalTargetHandleCount)
        {
            gGlobalEnemyPoolLoaded = true;
            gPoolWasReadyOnce = true;
            gGlobalReadyVariantBaseline = registeredVariants;
            LogEveryTargetOnce();
            InterlockedExchange(&gGlobalStreamPhase, kGlobalStreamReady);
            LogGlobalPolicyVariantStatus();
            Log("randomizer global stream ready: %u/%u targets registered, "
                "%u/%u policy variants ready (%u character archives, %u audio archives, %u target streams)",
                registered, kGlobalEnemyTargetCount, registeredVariants,
                requiredVariants, gGlobalCharacterHandleCount,
                gGlobalAudioHandleCount,
                gGlobalTargetHandleCount);
            return;
        }

        if (phase == kGlobalStreamTargetsActivated)
        {
            const ULONGLONG elapsed = GetTickCount64() - gGlobalStreamRequestedTick;
            // Published into a world that was left before the pool got ready (a save
            // loaded, then straight back to the menu and a New Game, 2026-10-22): those
            // streams are gone, the pin waits for all of them to be active and the
            // deadline waits for the pin, so nothing ever moved again. Publish anew.
            void* streamManager = nullptr;
            unsigned gone = 0;
            if (ResolveStreamManager(&streamManager))
                for (unsigned i = 0; i < gGlobalTargetHandleCount; ++i)
                    gone += StreamStateFor(streamManager, gGlobalTargetHandles[i]) < 0;
            if (gone && !gGlobalCharactersPinned)
            {
                Log("randomizer global stream: %u of %u target streams went with their world before the pool "
                    "was ready; publishing again into the next one", gone, gGlobalTargetHandleCount);
                gGlobalEnemyPoolLoaded = false;
                gGlobalReadyVariantBaseline = 0;
                ClearGlobalTargetReadiness();
                gGlobalTargetHandleCount = 0;
                gGlobalStreamRequestedTick = 0;
                gGlobalCharacterPreloadAttempts = 0;
                gGlobalStreamAwaitingLevel = true;
                gGlobalPoolLostSpawnerMark = gSpawners;
                gGlobalPoolLostTick = GetTickCount64();
                InterlockedExchange(&gGlobalStreamPhase, kGlobalStreamIdle);
                return;
            }
            // Readiness cannot be reached before the character archives are
            // pinned, and the pin deliberately waits for a running world.  The
            // deadline must not run against that wait: it exists to catch
            // sidecars that never register, not a precondition that has not
            // been allowed to happen yet.  Nothing evaluated this branch until
            // the heartbeat existed, which is why the deadline never fired
            // before and killed the pool on the first level as soon as it did.
            if (elapsed >= kGlobalStreamReadyTimeoutMs && gGlobalCharactersPinned)
            {
                LogGlobalPolicyVariantStatus();
                if (registeredVariants &&
                    activeTargetStreams == gGlobalTargetHandleCount)
                {
                    // CandidateFor checks the exact SimManager target key on
                    // every selection. A missing variant therefore reduces
                    // variety but cannot produce a stale or invalid spawn.
                    gGlobalEnemyPoolLoaded = true;
                    gPoolWasReadyOnce = true;
                    gGlobalReadyVariantBaseline = registeredVariants;
                    LogEveryTargetOnce();
                    InterlockedExchange(&gGlobalStreamPhase, kGlobalStreamReady);
                    Log("randomizer global stream ready (partial): %u/%u targets "
                        "registered, %u/%u policy variants ready within %llu ms "
                        "(characters=%u audio=%u target_streams=%u)",
                        registered, kGlobalEnemyTargetCount, registeredVariants,
                        requiredVariants, elapsed, gGlobalCharacterHandleCount,
                        gGlobalAudioHandleCount,
                        gGlobalTargetHandleCount);
                }
                else if (!registered)
                {
                    // Nothing registered at all usually means we published into
                    // a world that was being torn down, not that the sidecars
                    // are broken.  Latching a permanent failure there kills the
                    // pool for the rest of the session - exactly what made a
                    // checkpoint restart drop back to vanilla enemies.  Wait
                    // for the next world instead; a real defect fails again.
                    gGlobalEnemyPoolLoaded = false;
                    gGlobalReadyVariantBaseline = 0;
                    ClearGlobalTargetReadiness();
                    gGlobalTargetHandleCount = 0;
                    gGlobalStreamRequestedTick = 0;
                    gGlobalStreamAwaitingLevel = true;
                    gGlobalPoolLostSpawnerMark = gSpawners;
                    gGlobalPoolLostTick = GetTickCount64();
                    InterlockedExchange(&gGlobalStreamPhase,
                                        kGlobalStreamCharactersRequested);
                    Log("randomizer global stream: no target registered within "
                        "%llu ms; waiting for the next world instead of failing",
                        elapsed);
                    return;
                }
                else
                {
                    InterlockedExchange(&gGlobalStreamPhase, kGlobalStreamFailed);
                    Log("randomizer global stream failed: %u/%u targets and %u/%u "
                        "policy variants registered within %llu ms "
                        "(characters=%u audio=%u target_streams=%u)",
                        registered, kGlobalEnemyTargetCount, registeredVariants,
                        requiredVariants, elapsed, gGlobalCharacterHandleCount,
                        gGlobalAudioHandleCount,
                        gGlobalTargetHandleCount);
                }
            }
            return;
        }

        void* manager = nullptr;
        if (!ResolveStreamManager(&manager))
        {
            InterlockedExchange(&gGlobalStreamPhase, kGlobalStreamFailed);
            Log("randomizer global stream failed: StreamManager identity unavailable");
            return;
        }
        EnsureGlobalEnemyCharactersPinned(manager);
    }

    bool PickResidentTarget(const Guid128& original, unsigned expectedSlot,
                            uintptr_t current, Guid128* replacement)
    {
        if (!replacement || expectedSlot >= gActorCount)
            return false;
        Guid128 choices[128]{};
        unsigned count = 0;
        const unsigned seenCount = SeenSpawnerSnapshotCount();
        for (unsigned i = 0; i < seenCount && count < _countof(choices); ++i)
        {
            const uintptr_t spawner = gSeen[i];
            if (!StillASpawner(spawner))
                continue;
            Guid128 target{};
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(spawner + kTargetGuid),
                                   &target, sizeof(target), nullptr) ||
                SameGuid(target, original) || ResidentSlotFor(target) != expectedSlot)
                continue;
            bool duplicate = false;
            for (unsigned known = 0; known < count; ++known)
                if (SameGuid(choices[known], target))
                    duplicate = true;
            if (!duplicate)
                choices[count++] = target;
        }
        if (!count)
        {
            if (gEnemyRandomizerDiagnostics)
                Log("Randomizer: no resident %s target available before cross-family spawn",
                    gActorNames[expectedSlot]);
            return false;
        }

        static thread_local uint32_t state = 0xC001D00Du;
        state ^= static_cast<uint32_t>(current);
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        *replacement = choices[state % count];
        return true;
    }

    // FuncSpawn objects are reconstructed on a checkpoint reload.  A learned
    // target UID survives that rebuild, but its remembered owner does not.
    // The old implementation walked every historical FuncSpawn for each
    // candidate selection. That meant up to 192 * 512 ReadProcessMemory calls
    // on a single encounter. The most recently verified owner is enough:
    // candidates whose owner has gone with the stream are ignored, and the
    // native fallback below still retries the authored target should a stale
    // registry entry slip through.
    bool TargetOwnerIsLive(const LearnedTarget& candidate)
    {
        if (!StillASpawner(candidate.ownerSpawner))
            return false;
        Guid128 liveTarget{};
        return ReadProcessMemory(GetCurrentProcess(),
                                 reinterpret_cast<void*>(candidate.ownerSpawner + kTargetGuid),
                                 &liveTarget, sizeof(liveTarget), nullptr) &&
               SameGuid(liveTarget, candidate.target);
    }

    // DS1's v1 SimG header is "GmiS" and its first entity UID is exactly
    // 0x34 bytes after that marker in the live object.  This scan happens once
    // on the loader worker after a level has created spawners, never on the
    // game thread and never per enemy spawn.  It establishes that a global
    // target is genuinely resident before the experimental substitution may
    // refer to it.
    bool IsLoadedSimGRoot(const Guid128& root)
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        uintptr_t address = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        const uintptr_t limit = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
        MEMORY_BASIC_INFORMATION region{};
        while (address < limit &&
               VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)))
        {
            const DWORD protect = region.Protect & 0xFFu;
            const bool readable = region.State == MEM_COMMIT &&
                !(region.Protect & PAGE_GUARD) && protect != PAGE_NOACCESS &&
                (protect == PAGE_READONLY || protect == PAGE_READWRITE ||
                 protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READ ||
                 protect == PAGE_EXECUTE_READWRITE || protect == PAGE_EXECUTE_WRITECOPY);
            const size_t size = region.RegionSize;
            if (readable && size >= 0x44 && size <= 0x8000000)
            {
                __try
                {
                    const BYTE* bytes = reinterpret_cast<const BYTE*>(region.BaseAddress);
                    for (size_t i = 0; i + 0x44 <= size; ++i)
                    {
                        if (bytes[i] != 'G' || bytes[i + 1] != 'm' ||
                            bytes[i + 2] != 'i' || bytes[i + 3] != 'S')
                            continue;
                        if (std::memcmp(bytes + i + 0x34, &root, sizeof(root)) == 0)
                            return true;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    // A stream could be released between VirtualQuery and the
                    // byte scan.  Treat that as unavailable and keep looking.
                }
            }
            const uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + size;
            if (next <= address)
                break;
            address = next;
        }
        return false;
    }

    bool AlternateGlobalInstanceFor(const SpawnCompatibilitySource& source,
                                    const Guid128& original,
                                    const Guid128& failed,
                                    Guid128* replacement)
    {
        if (!replacement)
            return false;

        const GlobalEnemyTarget* failedMeta = nullptr;
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
            if (SameGuid(kGlobalEnemyTargets[i].target, failed))
            {
                failedMeta = &kGlobalEnemyTargets[i];
                break;
            }
        if (!failedMeta || !failedMeta->actorClass || !failedMeta->variant)
            return false;

        // A second independently authored entity of the same variant is the
        // safest retry.  It exercises a different SpawnInfo/root object while
        // preserving the class, character archive and context contract.
        for (unsigned i = 0; i < kGlobalEnemyTargetCount; ++i)
        {
            const GlobalEnemyTarget& candidate = kGlobalEnemyTargets[i];
            if (SameGuid(candidate.target, failed) ||
                SameGuid(candidate.target, original) ||
                !candidate.actorClass || !candidate.variant ||
                std::strcmp(candidate.actorClass, failedMeta->actorClass) != 0 ||
                std::strcmp(candidate.variant, failedMeta->variant) != 0 ||
                candidate.ventMode != failedMeta->ventMode ||
                !gGlobalEnemyTargetReady[i] ||
                !(CompatibleReplacement(source, candidate.actorClass,
                                        candidate.variant) ||
                  DirectReplacement(source, candidate)))
                continue;
            if (!CandidateRuntimeReady(candidate))
            {
                gGlobalEnemyTargetReady[i] = false;
                continue;
            }
            *replacement = candidate.target;
            return true;
        }
        return false;
    }

    void LogFactoryObservation(const char* label, unsigned expectedSlot,
                               unsigned createdIndex, unsigned invocationCount,
                               unsigned seenCount, const unsigned* seenClasses)
    {
        if (!gEnemyRandomizerDiagnostics)
            return;
        Log("Randomizer: %s factory observation: expected=%s last=%s calls=%u unique=%u",
            label,
            expectedSlot < gActorCount ? gActorNames[expectedSlot] : "unknown",
            createdIndex < gActorCount ? gActorNames[createdIndex] : "no actor",
            invocationCount,
            seenCount);
        for (unsigned i = 0; i < seenCount && i < 8; ++i)
        {
            const unsigned slot = seenClasses[i];
            Log("Randomizer:   observed[%u]=%s", i,
                slot < gActorCount ? gActorNames[slot] : "unknown");
        }
    }

    // The queued counter is the honest answer to whether any of this had an
    // effect; an impression of how many enemies showed up is not. Reported
    // whenever it moves, so playing normally is enough to see it.
    void ReportTotals()
    {
        LogDeathMessageTotals();
        LogStoreGateTotals();
        LogFirstWeaponTotals();
        LogScriptedTotals();
        LogLogRandomizerTotals();
        LogPregnantSwarmTotals();
        LogLootAmountTotals();
        LogWatchdogTotals();
        static long lastReplaced = -1, lastFallbacks = -1;
        const long replacedTotal = InterlockedCompareExchange(&gRandomizerReplacements, 0, 0);
        const long fallbackTotal = InterlockedCompareExchange(&gRandomizerFallbacks, 0, 0);
        if ((gEnemyRandomizer || replacedTotal || fallbackTotal) &&
            (replacedTotal != lastReplaced || fallbackTotal != lastFallbacks))
        {
            lastReplaced = replacedTotal;
            lastFallbacks = fallbackTotal;
            Log("randomizer totals: %ld enemies replaced, %ld fell back to the authored enemy",
                replacedTotal, fallbackTotal);
        }
        static unsigned lastQueued = 0xFFFFFFFF, lastSpawned = 0xFFFFFFFF;
        unsigned queued = 0, spawned = 0, unlimited = 0, live = 0;
        const unsigned seenCount = SeenSpawnerSnapshotCount();
        for (unsigned i = 0; i < seenCount; ++i)
        {
            if (!StillASpawner(gSeen[i]))
                continue;
            ++live;
            queued += *reinterpret_cast<unsigned*>(gSeen[i] + kQueuedCount);
            spawned += *reinterpret_cast<unsigned*>(gSeen[i] + kNumSpawned);
            if (*reinterpret_cast<unsigned*>(gSeen[i] + kMaxSpawn) == 0xFFFFFFFFu)
                ++unlimited;
        }
        if (queued == lastQueued && spawned == lastSpawned)
            return;
        lastQueued = queued;
        lastSpawned = spawned;
        Log("    totals: %u retries queued, %u enemies spawned, %u of %u "
            "spawners uncapped, %ld multiplied, %ld left alone",
            queued, spawned, unlimited, live, gForced, gSkipped);
    }

    void EnableRetry()
    {
        const unsigned seenCount = SeenSpawnerSnapshotCount();
        for (unsigned i = 0; i < seenCount; ++i)
        {
            const uintptr_t spawner = gSeen[i];
            if (!StillASpawner(spawner))
            {
                gSeen[i] = 0;           // gone with its area
                continue;
            }
            auto* options = reinterpret_cast<unsigned*>(spawner + kOptions);
            const unsigned want = kRetryOption | kRetryPath;
            if ((*options & want) == want)
                continue;
            *options |= want;
            InterlockedIncrement(&gPatched);
        }
    }

    // EAX is the new object. Dead Space 3 keeps the authored spawn cap and the
    // retry counters inside FuncSpawn, and the same numbers have to be located
    // here before anything can be scaled, so the addresses are kept for a
    // second look once the level has filled them in.
    void FuncSpawnAllocated_Hook(safetyhook::Context& ctx)
    {
        if (!ctx.eax)
            return;
        // Constructing a FuncSpawn is by definition the game thread.  Record it
        // so the allocator heartbeat can tell it apart from loading threads.
        gGameThreadId = GetCurrentThreadId();
        PatchShelfList();
        InterlockedExchange(&gLastSpawnerTick,
                            static_cast<LONG>(GetTickCount64() & 0x7FFFFFFF));
        const long line = InterlockedIncrement(&gSpawners);
        const LONG write = InterlockedIncrement(&gSeenWriteCursor) - 1;
        const unsigned index = static_cast<unsigned>(write) %
                               kSeenSpawnerCapacity;
        InterlockedExchangePointer(
            reinterpret_cast<PVOID volatile*>(&gSeen[index]),
            reinterpret_cast<PVOID>(ctx.eax));
        LONG count = InterlockedCompareExchange(&gSeenCount, 0, 0);
        while (count < static_cast<LONG>(kSeenSpawnerCapacity))
        {
            const LONG observed = InterlockedCompareExchange(
                &gSeenCount, count + 1, count);
            if (observed == count)
                break;
            count = observed;
        }
        if (line <= 12)
            Log("FuncSpawn #%ld allocated at %08X", line,
                static_cast<unsigned>(ctx.eax));

        // The first global StreamManager callback can finish before the
        // startup worker has discovered the actor class table. In that case
        // the character/audio leases are retained, but no later stock stream
        // call is guaranteed to advance target-sidecar publication. A
        // FuncSpawn allocation is a reliable game-thread boundary during
        // level construction and occurs well before its trigger fires, so it
        // can safely resume the already-requested asynchronous stream state.
        // Do not recurse when loading the sidecars themselves constructs
        // additional level objects.
        if (!gDispatchingGlobalStreams && gEnemyRandomizerGlobalStreaming &&
            gEnemyRandomizerGlobalTemplates && gActorCount)
            ProgressGlobalEnemyStreaming();
    }

    // Called once a level is running, when the objects are filled in.
    void ReportSpawnerFields(uintptr_t spawner)
    {
        char line[512]{};
        int used = 0;
        for (unsigned off = 0; off < kFuncSpawnSize; off += 4)
        {
            unsigned value = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(spawner + off),
                                   &value, sizeof(value), nullptr))
                continue;
            // Only the small numbers are interesting: counts and caps look
            // like this, pointers and floats do not.
            if (value && value <= 64)
            {
                used += std::snprintf(line + used, sizeof(line) - used,
                                      "%s+0x%02X=%u", used ? " " : "", off, value);
                if (used >= static_cast<int>(sizeof(line)) - 32)
                    break;
            }
        }
        Log("  spawner %08X small fields: %s", static_cast<unsigned>(spawner),
            used ? line : "(none)");
    }

    // The full object, taken while it is still alive. Reading it later from
    // outside is useless: by then the level has moved on and every dword comes
    // back as zero, which looks like a finding and is not one.
    // The four authored properties the message handler fills in at load time,
    // plus the two guids. If one of these names the enemy a spawner produces,
    // per-type scaling becomes possible the way it works in Dead Space 3.
    void ReportSpawnerIdentity(uintptr_t spawner)
    {
        char line[400]{};
        int used = 0;
        for (unsigned off : { 0x4Cu, 0x54u, 0x64u, 0x74u })
        {
            unsigned value = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(spawner + off),
                                   &value, sizeof(value), nullptr))
                continue;
            char text[64]{};
            const char* shown = "";
            if (value > 0x10000)
            {
                // Either a pointer to the characters or a small buffer that
                // starts with them; try the pointer first.
                if (ReadProcessMemory(GetCurrentProcess(),
                                      reinterpret_cast<void*>(value), text,
                                      sizeof(text) - 1, nullptr))
                {
                    bool druckbar = text[0] >= 32 && text[0] < 127;
                    if (druckbar)
                        shown = text;
                }
            }
            used += std::snprintf(line + used, sizeof(line) - used,
                                  "%s+0x%02X=%08X%s%s", used ? "  " : "", off,
                                  value, *shown ? " -> " : "", shown);
            if (used >= static_cast<int>(sizeof(line)) - 80)
                break;
        }
        Log("  identity %08X: %s", static_cast<unsigned>(spawner), line);

        unsigned char guid[16]{};
        if (ReadProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<void*>(spawner + 0x20), guid, 16,
                              nullptr))
            Log("    guid +0x20: %02X%02X%02X%02X-%02X%02X%02X%02X \"%.8s\"",
                guid[3], guid[2], guid[1], guid[0], guid[7], guid[6], guid[5],
                guid[4], guid + 8);
    }

    void DumpSpawner(uintptr_t spawner)
    {
        Log("  full dump of %08X:", static_cast<unsigned>(spawner));
        for (unsigned off = 0; off < kFuncSpawnSize; off += 16)
        {
            unsigned w[4]{};
            bool ok = true;
            for (unsigned i = 0; i < 4; ++i)
                if (!ReadProcessMemory(GetCurrentProcess(),
                                       reinterpret_cast<void*>(spawner + off + i * 4),
                                       &w[i], sizeof(unsigned), nullptr))
                    ok = false;
            if (!ok)
                break;
            Log("    +0x%03X  %08X %08X %08X %08X", off, w[0], w[1], w[2], w[3]);
        }
    }

    void ResetResolverPathTrace()
    {
        gWatchingResolverManager = 0;
        gWatchingResolverFilterCount = 0;
        gWatchingResolverManagerMask = 0;
        gResolverPassedPreflight = false;
        gResolverPassedFilters = false;
        gResolverRejectedFilter = -1;
        gResolverCallback = 0;
        gResolverEnteredFactory = false;
        gResolverFactoryObject = 0;
        gResolverBypassedFilter = -1;
        gFilterSawReference = false;
        std::memset(gFilterReference, 0, sizeof(gFilterReference));
        gFilterStreamId = 0;
        gFilterState = -1;
    }

    safetyhook::MidHook gRestoreProbe;
    safetyhook::MidHook gFindTargetKeyProbe;

    void FindTargetKeyProbe_Hook(safetyhook::Context& ctx)
    {
        if (!ctx.edi)
            return;
        Guid128 wanted{};
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(ctx.edi), &wanted,
                               sizeof(wanted), nullptr))
            return;
        if ((wanted.value[0] & 0xFFFF0000u) != 0xD5110000u)
            return;
        if (!gPoolWasReadyOnce || gGlobalEnemyPoolLoaded)
            return;
        static volatile LONG lines = 0;
        if (InterlockedIncrement(&lines) > 200)
            return;
        Log("key lookup for %08X-%08X-%08X-%08X (%s, pool %s)",
            wanted.value[0], wanted.value[1], wanted.value[2], wanted.value[3],
            gWatching ? "our replacement" : "OUTSIDE our handler",
            gGlobalEnemyPoolLoaded ? "ready" : "NOT ready");
    }

    void RestoreProbe_Hook(safetyhook::Context&)
    {
        WatchdogReset("checkpoint restore");
        static volatile LONG restores = 0;
        static thread_local bool inside = false;
        const LONG line = InterlockedIncrement(&restores);
        if (gSpawnResolverDiagnostics && line <= 200)
            Log("restore #%ld: pool %s, %u/%u target streams active",
                line,
                gGlobalEnemyPoolLoaded ? "ready" : "NOT ready",
                ActiveGlobalTargetStreamCount(), gGlobalTargetHandleCount);

        // A checkpoint reload rebuilds the enemies that were alive when the
        // checkpoint was set, and it resolves their SpawnInfo target while
        // doing so.  For a randomized enemy that target is one of ours, and
        // measured against this very probe we published it about half a second
        // too late - the enemy could not be resolved and simply vanished.
        // Waiting for new FuncSpawn objects was what cost that half second;
        // this message is the earliest honest signal that a world is being
        // rebuilt. Publication is still gated below because the same message
        // also fires while the old world is being removed.
        if (inside || !gEnemyRandomizerGlobalTemplates ||
            !gEnemyRandomizerGlobalStreaming || gGlobalEnemyPoolLoaded ||
            !gActorCount)
            return;

        // Restore messages also fire while the old level is being removed.
        // Loading retained sidecars in that window races SimManager teardown
        // and can stall or terminate the checkpoint reload.  The heartbeat
        // will publish after the replacement level and new spawners exist.
        void* streamManager = nullptr;
        const int levelState =
            ResolveStreamManager(&streamManager) && gLevelArchiveHandle
                ? StreamStateFor(streamManager, gLevelArchiveHandle)
                : -1;
        if (levelState != 3)
        {
            gGlobalStreamAwaitingLevel = true;
            gGlobalPoolLostSpawnerMark = gSpawners;
            gGlobalPoolLostTick = GetTickCount64();
            static LONG deferredLines = 0;
            if (InterlockedIncrement(&deferredLines) <= 16)
                Log("restore: target publication deferred because level stream "
                    "state is %d", levelState);
            return;
        }

        inside = true;
        // The first pass notices the loss and parks; the second publishes.
        // Doing both here keeps it inside this one restore message instead of
        // costing another round trip through the heartbeat.
        gRestoreDrivenPublish = true;
        for (unsigned pass = 0; pass < 2 && !gGlobalTargetHandleCount; ++pass)
        {
            gGlobalStreamAwaitingLevel = false;
            gGlobalPoolLostSpawnerMark = 0;
            ProgressGlobalEnemyStreaming();
        }
        gRestoreDrivenPublish = false;
        if (gGlobalTargetHandleCount)
            Log("restore: published %u target streams from the restore message",
                gGlobalTargetHandleCount);
        inside = false;
    }

    void ResolverPreflightPassed_Hook(safetyhook::Context&)
    {
        if (gWatching && gSpawnResolverDiagnostics && gWatchingResolverKey)
            gResolverPassedPreflight = true;
    }

    // EAX points at the four words of the resolved "Animated" reference. A
    // null pointer or an all-zero reference means the filter passes without
    // consulting the StreamManager at all.
    void FilterAnimatedReference_Hook(safetyhook::Context& ctx)
    {
        if (!gWatching || !gSpawnResolverDiagnostics || !gWatchingResolverKey)
            return;
        gFilterSawReference = true;
        std::memset(gFilterReference, 0, sizeof(gFilterReference));
        if (ctx.eax)
            ReadProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<void*>(ctx.eax), gFilterReference,
                              sizeof(gFilterReference), nullptr);
    }

    // EAX is the stream id the StreamManager associates with that reference.
    // Zero means the resource is not published by any known stream.
    void FilterAnimatedStream_Hook(safetyhook::Context& ctx)
    {
        if (!gWatching || !gSpawnResolverDiagnostics || !gWatchingResolverKey)
            return;
        gFilterStreamId = ctx.eax;
    }

    // EAX is the stream state the filter compares against 3.
    void FilterAnimatedState_Hook(safetyhook::Context& ctx)
    {
        if (!gWatching || !gSpawnResolverDiagnostics || !gWatchingResolverKey)
            return;
        gFilterState = static_cast<int>(ctx.eax);
    }

    void ResolverFilterResult_Hook(safetyhook::Context& ctx)
    {
        const bool rejected = (ctx.eax & 0xFFu) == 0;
        if (rejected && gWatching && gSpawnResolverDiagnostics &&
            gWatchingResolverKey)
            gResolverRejectedFilter = static_cast<int>(ctx.esi);
    }

    void ResolverFiltersPassed_Hook(safetyhook::Context&)
    {
        if (gWatching && gSpawnResolverDiagnostics && gWatchingResolverKey)
            gResolverPassedFilters = true;
    }

    void ResolverCallbackResult_Hook(safetyhook::Context& ctx)
    {
        if (gWatching && gSpawnResolverDiagnostics && gWatchingResolverKey)
            gResolverCallback = ctx.eax;
    }

    void ResolverFactoryEnter_Hook(safetyhook::Context&)
    {
        if (gWatching && gSpawnResolverDiagnostics && gWatchingResolverKey)
            gResolverEnteredFactory = true;
    }

    void ResolverFactoryResult_Hook(safetyhook::Context& ctx)
    {
        if (gWatching && gSpawnResolverDiagnostics && gWatchingResolverKey)
        {
            gResolverFactoryObject = ctx.eax;
            gLastResolverFactoryObject = ctx.eax;
        }
    }

    void SpawnResolverCall_Hook(safetyhook::Context& ctx)
    {
        // At the call instruction the compiler has already prepared all four
        // arguments, but EAX still holds the exact target-key returned by
        // 0x006F7DC0. Capture it before SimManager turns it into a runtime
        // spawn-target object. This is observational only.
        gWatchingResolverKey = 0;
        ResetResolverPathTrace();
        if (!gWatching || !gSpawnResolverDiagnostics ||
            ctx.ebx < kHandlerBaseOffset)
            return;
        const uintptr_t spawner = ctx.ebx - kHandlerBaseOffset;
        if (StillASpawner(spawner))
        {
            gWatchingResolverKey = ctx.eax;
            // The four stdcall arguments are already on the stack at the call
            // instruction.  The first one is SimManager.
            ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(ctx.esp),
                              &gWatchingResolverManager,
                              sizeof(gWatchingResolverManager), nullptr);
            if (gWatchingResolverManager)
            {
                ReadProcessMemory(GetCurrentProcess(),
                                  reinterpret_cast<void*>(gWatchingResolverManager + 0x1C),
                                  &gWatchingResolverFilterCount,
                                  sizeof(gWatchingResolverFilterCount), nullptr);
                ReadProcessMemory(GetCurrentProcess(),
                                  reinterpret_cast<void*>(gWatchingResolverManager + 0x20),
                                  &gWatchingResolverManagerMask,
                                  sizeof(gWatchingResolverManagerMask), nullptr);
            }
        }
    }

    void SpawnResolverReturn_Hook(safetyhook::Context& ctx)
    {
        // The handler wrapper sets this only around an authentic native
        // FuncSpawn call.  Thus this recorder observes real resolver results,
        // not unrelated SimManager traffic elsewhere in the game.
        if (!gWatching || !gSpawnResolverDiagnostics || !gWatchingResolverKey ||
            ctx.ebx < kHandlerBaseOffset)
            return;
        const uintptr_t spawner = ctx.ebx - kHandlerBaseOffset;
        if (!StillASpawner(spawner))
            return;
        Guid128 source{}, target{};
        std::memcpy(&source, reinterpret_cast<const void*>(spawner + kSourceGuid),
                    sizeof(source));
        std::memcpy(&target, reinterpret_cast<const void*>(spawner + kTargetGuid),
                    sizeof(target));
        RecordResolver(source, target, gWatchingResolverKey, ctx.eax);
        static volatile LONG pathLines = 0;
        const LONG pathLine = InterlockedIncrement(&pathLines);
        if (pathLine <= 600)
            Log("resolver path: target=%08X-%08X-%08X-%08X manager=%08X "
                "filters=%u mask=%08X preflight=%s rejectedFilter=%d bypassedFilter=%d "
                "filtersPassed=%s callback=%08X factoryEntered=%s "
                "factoryObject=%08X final=%08X",
                target.value[0], target.value[1], target.value[2], target.value[3],
                static_cast<unsigned>(gWatchingResolverManager),
                gWatchingResolverFilterCount, gWatchingResolverManagerMask,
                gResolverPassedPreflight ? "yes" : "no", gResolverRejectedFilter,
                gResolverBypassedFilter,
                gResolverPassedFilters ? "yes" : "no",
                static_cast<unsigned>(gResolverCallback),
                gResolverEnteredFactory ? "yes" : "no",
                static_cast<unsigned>(gResolverFactoryObject),
                static_cast<unsigned>(ctx.eax));
        if (pathLine <= 128)
            Log("resolver filter0: reached=%s animated=%08X-%08X-%08X-%08X "
                "streamId=%08X streamState=%d",
                gFilterSawReference ? "yes" : "no", gFilterReference[0],
                gFilterReference[1], gFilterReference[2], gFilterReference[3],
                static_cast<unsigned>(gFilterStreamId), gFilterState);
        gWatchingResolverKey = 0;
        ResetResolverPathTrace();
    }

    // Runs on the game's own thread, which is the only safe place to call back
    // into the game. Re-entry is blocked with a flag: the extra calls go
    // through this hook again and would otherwise multiply without end.
    // One per enemy class, told apart by the slot the hook lives in. Only
    // records while the spawn hook is watching, so enemy creation elsewhere in
    // the level costs nothing but a thread-local read.
    template <unsigned Slot>
    void EnemyFactory_Hook(safetyhook::Context&)
    {
        if (!gWatching)
            return;
        ++gFactoryInvocationCount;
        gCreatedIndex = Slot;
        for (unsigned i = 0; i < gSeenClassCount; ++i)
            if (gSeenClasses[i] == Slot)
                return;
        if (gSeenClassCount < 8)
            gSeenClasses[gSeenClassCount++] = Slot;
    }

    using FactoryHookFn = void (*)(safetyhook::Context&);
    FactoryHookFn EnemyHookFor(unsigned slot)
    {
        static const FactoryHookFn table[] = {
            &EnemyFactory_Hook<0>,  &EnemyFactory_Hook<1>,  &EnemyFactory_Hook<2>,
            &EnemyFactory_Hook<3>,  &EnemyFactory_Hook<4>,  &EnemyFactory_Hook<5>,
            &EnemyFactory_Hook<6>,  &EnemyFactory_Hook<7>,  &EnemyFactory_Hook<8>,
            &EnemyFactory_Hook<9>,  &EnemyFactory_Hook<10>, &EnemyFactory_Hook<11>,
            &EnemyFactory_Hook<12>, &EnemyFactory_Hook<13>, &EnemyFactory_Hook<14>,
            &EnemyFactory_Hook<15>, &EnemyFactory_Hook<16>, &EnemyFactory_Hook<17>,
            &EnemyFactory_Hook<18>, &EnemyFactory_Hook<19>, &EnemyFactory_Hook<20>,
            &EnemyFactory_Hook<21>, &EnemyFactory_Hook<22>, &EnemyFactory_Hook<23>,
            &EnemyFactory_Hook<24>, &EnemyFactory_Hook<25>, &EnemyFactory_Hook<26>,
            &EnemyFactory_Hook<27>, &EnemyFactory_Hook<28>, &EnemyFactory_Hook<29>,
            &EnemyFactory_Hook<30>, &EnemyFactory_Hook<31>,
        };
        return slot < kMaxActorClasses ? table[slot] : nullptr;
    }

    // Where the player stands.  The class table's Player entry is not a
    // constructor - it is a component accessor - so hooking it and taking ECX
    // captured whatever happened to be in that register.  The game itself
    // resolves the player through a manager: read the object out of the list
    // at 0x00F64A4C, undo the multiple-inheritance adjustment of 0x324, and
    // the class query for id 0x383225A1 returns that very pointer.  Every step
    // is null-checked here because the game's own version is not, which is
    // what makes it crash while a level is being torn down.
    constexpr uintptr_t kPlayerManagerGlobal = 0x00F64A4C;
    constexpr int kPlayerBaseAdjust = 0x324;
    constexpr unsigned kPlayerMatrix = 0x90;   // 4x4, rows of four floats
    constexpr unsigned kPlayerPosition = 0xC0; // fourth row, w = 1

    bool ReadPointer(uintptr_t at, uintptr_t* out)
    {
        return at && ReadProcessMemory(GetCurrentProcess(),
                                       reinterpret_cast<const void*>(at), out,
                                       sizeof(*out), nullptr) && *out;
    }

    uintptr_t PlayerObject()
    {
        uintptr_t manager = 0, node = 0, object = 0;
        if (!ReadPointer(kPlayerManagerGlobal, &manager) ||
            !ReadPointer(manager + 4, &node) || !ReadPointer(node, &object) ||
            object <= static_cast<uintptr_t>(kPlayerBaseAdjust))
            return 0;
        const uintptr_t player = object - kPlayerBaseAdjust;
        uintptr_t vtable = 0;
        if (!ReadPointer(player, &vtable) || vtable < 0x00401000 ||
            vtable > 0x00FF0000)
            return 0;
        return player;
    }

    // out12 keeps the caller's convention: three axes, then the position.
    bool PlayerTransform(float* out12)
    {
        const uintptr_t player = PlayerObject();
        if (!player)
            return false;
        float rows[16]{};
        if (!ReadProcessMemory(GetCurrentProcess(),
                               reinterpret_cast<void*>(player + kPlayerMatrix),
                               rows, sizeof(rows), nullptr))
            return false;
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            const float* row = rows + axis * 4;
            const float length = row[0] * row[0] + row[1] * row[1] +
                                 row[2] * row[2];
            if (length < 0.90f || length > 1.10f)
                return false;
            out12[axis * 3 + 0] = row[0];
            out12[axis * 3 + 1] = row[1];
            out12[axis * 3 + 2] = row[2];
        }
        const float* position = rows + 12;   // player + kPlayerPosition
        out12[9] = position[0];
        out12[10] = position[1];
        out12[11] = position[2];
        return true;
    }

    // ---------------------------------------------------------------------
    // The main-menu crash, fixed where it happens.
    //
    // 0x0050ED30 is a helper that looks the player up through the same chain
    // PlayerObject() uses, and it checks none of it.  While the world is torn
    // down for the frontend the list is empty, [[0x00F64A4C]+4] dereferences
    // to null, and the next read faults at 0x0050ED4F.  Five crash dumps, all
    // identical, none with a line of ours on the stack.
    //
    // All three real callers - 0x005F2150, 0x005F4D10 and 0x00477A90 - guard
    // their own pointer, one of them even passes ECX = 0 deliberately, and
    // every one of them does `test eax, eax` on the result.  A null return is
    // therefore an expected answer, not an invention: the guard only turns a
    // fault into the answer the callers already handle.  Takes this in ECX and
    // no stack argument; the function ends on a bare `ret`.
    // ---------------------------------------------------------------------
    constexpr uintptr_t kPlayerQueryHelper = 0x0050ED30;
    safetyhook::InlineHook gPlayerQueryHelperHook;
    volatile LONG gPlayerQueryGuarded = 0;

    uintptr_t __fastcall PlayerQueryHelper_Hook(void* self, void*)
    {
        uintptr_t manager = 0, node = 0, object = 0;
        if (!ReadPointer(kPlayerManagerGlobal, &manager) ||
            !ReadPointer(manager + 4, &node) ||
            !ReadPointer(node, &object))
        {
            const LONG count = InterlockedIncrement(&gPlayerQueryGuarded);
            if (count <= 3 || (count % 512) == 0)
                Log("player query guard: no player in the world, answered 0 "
                    "instead of faulting (%ld so far)", count);
            return 0;
        }
        return gPlayerQueryHelperHook.thiscall<uintptr_t>(self);
    }

    void LogPlayerPlacement()
    {
        float m[12]{};
        if (!PlayerTransform(m))
        {
            Log("placement: no player transform yet");
            return;
        }
        // Rows 0..2 are the axes, row 3 the position.  The forward axis is the
        // one the game orients the character by; log all three so the useful
        // one is obvious rather than assumed.
        Log("placement: position %.2f %.2f %.2f", m[9], m[10], m[11]);
        Log("placement: axes X %.2f %.2f %.2f | Y %.2f %.2f %.2f | Z %.2f %.2f %.2f",
            m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8]);
        // The same yaw an [ExtraSpawns] rule takes as its seventh field, so a
        // placement can be copied out of the log without any conversion.
        const double yaw = std::atan2(-static_cast<double>(m[2]),
                                      static_cast<double>(m[0])) *
                           180.0 / 3.14159265358979;
        Log("placement: for a rule -> %.1f, %.1f, %.1f, %.0f  (x, y, z, angle)",
            m[9], m[10], m[11], yaw);

        // The spawners around you, nearest first, by their authored name.
        // Choosing a trigger should not require reading a GUID out of a
        // catalogue: walk into the room, press the key, pick a name.
        struct Nearby { double distance; uintptr_t spawner; };
        Nearby best[8]{};
        unsigned found = 0;
        const unsigned seen = SeenSpawnerSnapshotCount();
        for (unsigned i = 0; i < seen; ++i)
        {
            const uintptr_t spawner = gSeen[i];
            if (!spawner || !StillASpawner(spawner))
                continue;
            float where[3]{};
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(spawner + 0xB0),
                                   where, sizeof(where), nullptr))
                continue;
            const double dx = where[0] - m[9];
            const double dy = where[1] - m[10];
            const double dz = where[2] - m[11];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            unsigned slot = found < 8 ? found : 8;
            for (unsigned probe = 0; probe < found && probe < 8; ++probe)
                if (distance < best[probe].distance)
                {
                    slot = probe;
                    break;
                }
            if (slot >= 8)
                continue;
            for (unsigned move = found < 8 ? found : 7; move > slot; --move)
                best[move] = best[move - 1];
            best[slot] = { distance, spawner };
            if (found < 8)
                ++found;
        }
        Log("placement: %u spawner(s) nearby", found);
        for (unsigned i = 0; i < found; ++i)
        {
            Guid128 source{};
            std::memcpy(&source,
                        reinterpret_cast<const void*>(best[i].spawner + kSourceGuid),
                        sizeof(source));
            float where[3]{};
            ReadProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<void*>(best[i].spawner + 0xB0),
                              where, sizeof(where), nullptr);
            // The spawner's own orientation tells which of several vents in a
            // room this one is.  Its second axis is the surface normal, the
            // same convention the offline catalogue reads off a VentMarker, so
            // a vertical component near zero means a wall.
            float axes[9]{};
            ReadProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<void*>(best[i].spawner + 0x70),
                              axes, sizeof(axes), nullptr);
            const float vertical = axes[4];
            const char* facing = "angled";
            if (vertical > 0.75f)
                facing = "floor";
            else if (vertical < -0.75f)
                facing = "ceiling";
            else if (vertical > -0.35f && vertical < 0.35f)
                facing = "wall";
            unsigned cap = 0;
            ReadProcessMemory(GetCurrentProcess(),
                              reinterpret_cast<void*>(best[i].spawner + kMaxSpawn),
                              &cap, sizeof(cap), nullptr);
            char capText[24]{};
            if (cap == 0xFFFFFFFFu)
                std::strncpy(capText, "unbegrenzt", sizeof(capText) - 1);
            else
                std::snprintf(capText, sizeof(capText), "max %u", cap);
            // The offline name table does not cover every spawner in the
            // game. An unnamed one is still usable: the rule key accepts a
            // GUID in exactly this notation, so print it instead of leaving
            // the reader with nothing to copy.
            const char* name = SpawnerName(source);
            char label[48]{};
            if (std::strcmp(name, "unbenannt") == 0)
                std::snprintf(label, sizeof(label), "%08X-%08X-%08X-%08X",
                              source.value[0], source.value[1],
                              source.value[2], source.value[3]);
            else
                std::strncpy(label, name, sizeof(label) - 1);
            Log("  %5.1f m  %-38s  %-8s %-11s  %.1f %.1f %.1f",
                best[i].distance, label, facing, capText,
                where[0], where[1], where[2]);
        }
    }

#include "world_editor.inc"
#include "god_mode.inc"

    // ---------------------------------------------------------------------
    // Cheats page, second half.
    //
    // Handing the player a weapon means writing wherever the game remembers
    // which ones you own, and nothing here guesses where that is.  Both
    // candidates are captured by the world editor's constructor hooks, so
    // this prints them instead: read the state with three weapons owned and
    // again with four, and the bytes that mean "owned" fall out of the
    // difference.  That is how the store path and the playing-dead flag were
    // found, and it beats writing into a structure we have not seen.
    // ---------------------------------------------------------------------
    uintptr_t LiveEditorObject(const char* className)
    {
        unsigned wanted = kEditorClassCount;
        for (unsigned i = 0; i < kEditorClassCount; ++i)
            if (std::strcmp(kEditorClasses[i].name, className) == 0)
                wanted = i;
        if (wanted == kEditorClassCount)
            return 0;
        LONG count = InterlockedCompareExchange(&gEditorCount, 0, 0);
        if (count > static_cast<LONG>(kEditorRegistryCapacity))
            count = static_cast<LONG>(kEditorRegistryCapacity);
        // The per-class slot first: the ring loses rare objects.  It
        // holds a one-based ring index, so 0 means nothing was seen.
        const unsigned newest = gNewestEditorObject[wanted];
        if (newest && newest <= kEditorRegistryCapacity)
        {
            const uintptr_t object =
                ResolveEditorObject(gEditorObjects[newest - 1]);
            if (object)
                return object;
        }
        // Newest first; a candidate counts only if it carries this
        // class's vtable, which is also how a freed pointer drops out.
        for (LONG i = count - 1; i >= 0; --i)
        {
            if (gEditorObjects[i].classIndex != wanted)
                continue;
            const uintptr_t object = ResolveEditorObject(gEditorObjects[i]);
            if (object)
                return object;
        }
        return 0;
    }


    // ---------------------------------------------------------------------
    // Vacuum, decided by us instead of by the level.
    //
    // The game asks one question per frame, at 0x0052B5B0: it takes the
    // player's position, asks the field list whether any volume there carries
    // scene bit 0, and returns 0 for "no air".  The fallback it uses when no
    // field answers - player+0xC6B - is written once in the player
    // constructor, to 1, and never again.  So a field is the only thing that
    // can ever produce vacuum in this game.
    //
    // Rather than author fields, this hooks the caller at 0x0052B627, where
    // the answer is already in al and has not been used yet, and clears it
    // when the player stands in a zone we picked.  No archive changes, no
    // field objects, and it works in every room because the game consults
    // this one place.
    // ---------------------------------------------------------------------
    constexpr uintptr_t kAirCheckResult = 0x0052B627;
    safetyhook::MidHook gAirCheckHook;
    unsigned gVacuumForcedFrames = 0;


    // ---------------------------------------------------------------------
    // Vacuum boxes.
    //
    // A ZoneRender's +0x1D0 is a bounding sphere the spatial system computes
    // from the zone's contents - neither the class constructor nor its base
    // writes it, and one zone in chapter 1 has a radius of 1094 - so it was
    // never meant as a room boundary.  Using it made the air flip on and off
    // metres into the next room.
    //
    // A box with real coordinates does not guess.  Read the corners with F10
    // in two opposite corners of the room and write them down; the test is
    // exact and works in rooms that have no zone of their own.
    // ---------------------------------------------------------------------
    struct VacuumBox
    {
        char name[48]{};
        float low[3]{};
        float high[3]{};
    };
    constexpr unsigned kMaxVacuumBoxes = 64;
    VacuumBox gVacuumBoxes[kMaxVacuumBoxes]{};
    unsigned gVacuumBoxCount = 0;

    void LoadVacuumBoxes()
    {
        gVacuumBoxCount = 0;
        char section[4096]{};
        const DWORD used = GetPrivateProfileSectionA(
            "VacuumBoxes", section, sizeof(section) - 2, gIniPath);
        const char* cursor = section;
        while (used && *cursor && gVacuumBoxCount < kMaxVacuumBoxes)
        {
            const char* equals = std::strchr(cursor, '=');
            if (equals)
            {
                VacuumBox box;
                const size_t keyLength = static_cast<size_t>(equals - cursor);
                if (keyLength && keyLength < sizeof(box.name))
                    std::memcpy(box.name, cursor, keyLength);
                char value[160]{};
                std::strncpy(value, equals + 1, sizeof(value) - 1);
                float numbers[6]{};
                unsigned found = 0;
                char* token = value;
                while (found < 6 && token && *token)
                {
                    numbers[found++] = static_cast<float>(std::atof(token));
                    char* next = std::strchr(token, ',');
                    if (!next)
                        break;
                    token = next + 1;
                }
                if (found == 6)
                {
                    for (unsigned axis = 0; axis < 3; ++axis)
                    {
                        const float a = numbers[axis];
                        const float b = numbers[axis + 3];
                        box.low[axis] = a < b ? a : b;
                        box.high[axis] = a < b ? b : a;
                    }
                    gVacuumBoxes[gVacuumBoxCount++] = box;
                    Log("vacuum box: %s from %.1f %.1f %.1f to %.1f %.1f %.1f",
                        box.name, box.low[0], box.low[1], box.low[2],
                        box.high[0], box.high[1], box.high[2]);
                }
                else
                    Log("vacuum box: %s ignored, needs six numbers "
                        "(x1,y1,z1,x2,y2,z2)", box.name);
            }
            cursor += std::strlen(cursor) + 1;
        }
    }

    bool PlayerInsideVacuumBox(const float* me)
    {
        for (unsigned i = 0; i < gVacuumBoxCount; ++i)
        {
            const VacuumBox& box = gVacuumBoxes[i];
            if (me[9] < box.low[0] || me[9] > box.high[0] ||
                me[10] < box.low[1] || me[10] > box.high[1] ||
                me[11] < box.low[2] || me[11] > box.high[2])
                continue;
            return true;
        }
        return false;
    }

    // Zone spheres overlap generously - a big hall's sphere reaches well
    // into the rooms around it - so asking "is any listed zone in range" made
    // the air flip on and off metres into the neighbouring room.  The zone a
    // player is actually in is the *smallest* one containing them: the most
    // specific hull wins, and a room's own zone is always tighter than the
    // hall's.  That gives exactly one zone at a time and a crisp edge.
    Guid128 gVacuumCurrentZone{};
    float gVacuumCurrentRadius = 0.0f;

    bool PlayerInsideVacuumZone()
    {
        static ULONGLONG lastCheck = 0;
        static bool cached = false;
        const ULONGLONG now = GetTickCount64();
        if (now - lastCheck < 100)
            return cached;
        lastCheck = now;
        cached = false;
        if (!gVacuumZoneRuleCount && !gVacuumBoxCount)
            return false;
        float me[12]{};
        if (!PlayerTransform(me))
            return false;
        // Exact first: a box means what it says.
        if (PlayerInsideVacuumBox(me))
        {
            cached = true;
            return true;
        }
        const unsigned zoneClass = ZoneRenderClassIndex();
        if (zoneClass >= kEditorClassCount)
            return false;

        Guid128 best{};
        float bestRadius = 0.0f;
        bool haveBest = false;
        const unsigned count = EditorRegistryCount();
        for (unsigned i = 0; i < count; ++i)
        {
            if (gEditorObjects[i].classIndex != zoneClass)
                continue;
            const uintptr_t object = ResolveEditorObject(gEditorObjects[i]);
            if (!object)
                continue;
            float sphere[4]{};
            Guid128 guid{};
            if (!ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(object + kZoneSphereOffset),
                    sphere, sizeof(sphere), nullptr) ||
                !ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(object + 0x20),
                    &guid, sizeof(guid), nullptr))
                continue;
            if (!(sphere[3] > 0.0f))
                continue;
            const double dx = me[9] - sphere[0];
            const double dy = me[10] - sphere[1];
            const double dz = me[11] - sphere[2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) > sphere[3])
                continue;
            if (haveBest && sphere[3] >= bestRadius)
                continue;
            haveBest = true;
            bestRadius = sphere[3];
            best = guid;
        }
        if (!haveBest)
            return false;
        if (gVacuumZoneDiagnostics && !SameGuid(best, gVacuumCurrentZone))
            Log("vacuum: now in zone %08X-%08X-%08X-%08X (radius %.1f) - %s",
                best.value[0], best.value[1], best.value[2], best.value[3],
                bestRadius, FindVacuumZoneRule(best) ? "listed" : "not listed");
        gVacuumCurrentZone = best;
        gVacuumCurrentRadius = bestRadius;
        cached = FindVacuumZoneRule(best) != nullptr;
        return cached;
    }

    void AirCheck_Hook(safetyhook::Context& ctx)
    {
        // Once per frame on the game thread: the place a suit change asked
        // for in the overlay is carried out.
        TryApplyPendingSuitChange();
        if (!gVacuumZones)
            return;
        // Already no air; nothing to add.
        if ((ctx.eax & 0xFFu) == 0)
            return;
        if (!PlayerInsideVacuumZone())
            return;
        ctx.eax &= ~0xFFu;
        if (gVacuumZoneDiagnostics && (gVacuumForcedFrames++ % 300) == 0)
            Log("vacuum: air denied by rule (frame %u)", gVacuumForcedFrames);
    }

    void InstallAirCheckHook()
    {
        // The suit changer borrows this per-frame site even without vacuum
        // zones; the hook itself only changes the answer when they are on.
        if (!gVacuumZones && !gSuitChanger)
            return;
        // 38 86 6C 0C 00 00 is `cmp byte ptr [esi+0xC6C], al`, the first use
        // of the answer.  Refuse to patch anything else.
        static const unsigned char expected[] = {0x38, 0x86, 0x6C, 0x0C,
                                                 0x00, 0x00};
        if (std::memcmp(reinterpret_cast<const void*>(kAirCheckResult),
                        expected, sizeof(expected)) != 0)
        {
            Log("vacuum: air check NOT hooked; %08X is not the measured "
                "compare", static_cast<unsigned>(kAirCheckResult));
            return;
        }
        gAirCheckHook = VerifiedMid(
            reinterpret_cast<void*>(kAirCheckResult), &AirCheck_Hook);
        Log("vacuum: air check %s at %08X, %u zone rule(s), %u box(es)",
            gAirCheckHook ? "hooked" : "NOT hooked",
            static_cast<unsigned>(kAirCheckResult), gVacuumZoneRuleCount,
            gVacuumBoxCount);
    }

    void LogObjectWords(const char* label, uintptr_t object, unsigned bytes)
    {
        if (!object)
        {
            Log("cheats: %s not captured yet", label);
            return;
        }
        __try
        {
            Log("cheats: %s at %08X, vtable %08X", label,
                static_cast<unsigned>(object),
                static_cast<unsigned>(
                    *reinterpret_cast<const uintptr_t*>(object)));
            for (unsigned offset = 0; offset < bytes; offset += 16)
            {
                const auto* words =
                    reinterpret_cast<const unsigned*>(object + offset);
                Log("   +%04X  %08X %08X %08X %08X", offset,
                    words[0], words[1], words[2], words[3]);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("cheats: %s at %08X became unreadable", label,
                static_cast<unsigned>(object));
        }
    }

    void FormatCheatMenuRow(unsigned row, char* text, size_t capacity)
    {
        if (row < kCheatWeaponCount)
            std::snprintf(text, capacity, "Give weapon: %-14s",
                          kCheatWeapons[row]);
        else if (row == kCheatWeaponCount)
            std::snprintf(text, capacity,
                          "Log inventory and WeaponManager");
        else if (row == kCheatWeaponCount + 1)
            std::snprintf(text, capacity,
                          "Search memory for the Line Gun (keep the store open)");
        else if (row == kCheatWeaponCount + 2)
            std::snprintf(text, capacity,
                          "Log the nearest enemy");
        else if (row == kCheatWeaponCount + 3)
            std::snprintf(text, capacity,
                          "Replace every Line Gun with a Ripper");
        else if (row == kCheatWeaponCount + 4)
            std::snprintf(text, capacity,
                          "Log the store (21 item slots)");
        else if (row == kCheatWeaponCount + 5)
            std::snprintf(text, capacity,
                          "Read or write the nearest light");
        else if (row == kCheatRowSuitPick)
        {
            unsigned current = 0;
            const bool known = CurrentSuitIndex(&current);
            std::snprintf(text, capacity,
                          "Suit: < level %u >  ENTER applies (now: %s%u)  %s",
                          gSuitMenuIndex + 1, known ? "level " : "?",
                          known ? current + 1 : 0u, gSuitChangeFeedback);
        }
        else if (row == kCheatRowSuitRandom)
            std::snprintf(text, capacity, "Suit: random (never the one worn)");
        else
            std::snprintf(text, capacity,
                          "Remove all weapons (step 1: logs the player's weapon registries)");
    }

    void CycleSuitMenu(int delta)
    {
        gSuitMenuIndex = static_cast<unsigned>(
            (static_cast<int>(gSuitMenuIndex) + delta + static_cast<int>(kSuitChangerCount)) %
            static_cast<int>(kSuitChangerCount));
    }

    // Three attempts to change the shop have each patched a container the
    // shop turned out not to read, and reasoning about which one it uses
    // has not converged.  So ask the process instead: walk every committed
    // page and report each place the Line Gun's item GUID is stored.  With
    // the shop open on screen, one of those addresses is what it displays.
    // A manager object starts with its class descriptor (the weapon
    // registries showed 00C6F128 at +0), so an instance of a class we
    // never caught at construction can still be found by that dword.
    // Hits inside the executable image are the class table itself.
    void ScanForDescriptor(uint32_t descriptor, const char* label,
                           unsigned dumpBytes)
    {
        MEMORY_BASIC_INFORMATION info{};
        uintptr_t address = 0x00010000;
        unsigned hits = 0;
        while (address < 0x7FFF0000u && hits < 6)
        {
            if (!VirtualQuery(reinterpret_cast<void*>(address), &info,
                              sizeof(info)))
                break;
            const uintptr_t base =
                reinterpret_cast<uintptr_t>(info.BaseAddress);
            const uintptr_t size = static_cast<uintptr_t>(info.RegionSize);
            const DWORD readable = PAGE_READWRITE | PAGE_WRITECOPY |
                                   PAGE_EXECUTE_READWRITE;
            if (info.State == MEM_COMMIT && (info.Protect & readable) &&
                !(info.Protect & PAGE_GUARD) &&
                (base < 0x00400000u || base >= 0x01000000u))
            {
                __try
                {
                    for (uintptr_t at = base; at + 4 <= base + size; at += 4)
                    {
                        if (*reinterpret_cast<const uint32_t*>(at) != descriptor)
                            continue;
                        ++hits;
                        Log("cheats: %s instance candidate #%u at %08X",
                            label, hits, static_cast<unsigned>(at));
                        LogObjectWords(label, at, dumpBytes);
                        // The inventory (13.09. log): +0xC holds 0x28 and the
                        // list heads point just before the object, so the
                        // 4 KB around it are the slots. Every item GUID in
                        // there (suffix "WRED E042") is named with its offset.
                        if (descriptor == 0x00C85278u &&
                            *reinterpret_cast<const uint32_t*>(at + 0xC) == 0x28u)
                        {
                            const uintptr_t from = at - 0x800;
                            LogObjectWords("inventory neighbourhood", from, 0x1000);
                            for (uintptr_t g = from; g + 16 <= from + 0x1000; g += 4)
                            {
                                const auto* w = reinterpret_cast<const uint32_t*>(g);
                                if (w[2] == 0x44455257u && w[3] == 0x34303245u)
                                    Log("cheats: item guid %08X-%08X-%08X-%08X at %08X (object%+d)",
                                        w[0], w[1], w[2], w[3], static_cast<unsigned>(g),
                                        static_cast<int>(g - at));
                            }
                        }
                        if (hits >= 6)
                            break;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }
            address = base + size;
        }
        Log("cheats: %s descriptor %08X: %u candidate(s)", label, descriptor, hits);
    }

    // Every weapon item id looks like 36C8BCxx-xxxxxxxx-44455257-34303245
    // (kItemLineGun, kItemRipper). The owned copies live in writable heap
    // next to their counts; this lists each copy with 0x40 bytes of context
    // so the owning container can be read off the log.
    void ScanForItemGuids()
    {
        MEMORY_BASIC_INFORMATION info{};
        uintptr_t address = 0x00010000;
        unsigned hits = 0;
        while (address < 0x7FFF0000u && hits < 80)
        {
            if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)))
                break;
            const uintptr_t base = reinterpret_cast<uintptr_t>(info.BaseAddress);
            const uintptr_t size = static_cast<uintptr_t>(info.RegionSize);
            if (info.State == MEM_COMMIT && (info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) &&
                !(info.Protect & PAGE_GUARD) && (base < 0x00400000u || base >= 0x01000000u))
            {
                __try
                {
                    for (uintptr_t at = base; at + 16 <= base + size; at += 4)
                    {
                        const auto* w = reinterpret_cast<const uint32_t*>(at);
                        if ((w[0] & 0xFFFFFF00u) != 0x36C8BC00u || w[2] != 0x44455257u || w[3] != 0x34303245u)
                            continue;
                        ++hits;
                        Log("cheats: item id %08X-%08X-WRED-E042 copy #%u at %08X (region %08X, %s)",
                            w[0], w[1], hits, static_cast<unsigned>(at), static_cast<unsigned>(base),
                            (info.Type & MEM_PRIVATE) ? "heap" : "mapped");
                        LogObjectWords("around item id", at - 0x20, 0x60);
                        if (hits >= 80)
                            break;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }
            address = base + size;
        }
        Log("cheats: item id scan done, %u copies", hits);
    }

    void ScanForGuid(const Guid128& needle, const char* label,
                     const Guid128* replacement = nullptr)
    {
        MEMORY_BASIC_INFORMATION info{};
        uintptr_t address = 0x00010000;
        unsigned hits = 0;
        unsigned regions = 0;
        while (address < 0x7FFF0000u && hits < 48)
        {
            if (!VirtualQuery(reinterpret_cast<void*>(address), &info,
                              sizeof(info)))
                break;
            const uintptr_t base =
                reinterpret_cast<uintptr_t>(info.BaseAddress);
            const uintptr_t size = static_cast<uintptr_t>(info.RegionSize);
            const DWORD readable = PAGE_READONLY | PAGE_READWRITE |
                                   PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                   PAGE_EXECUTE_READWRITE;
            if (info.State == MEM_COMMIT && (info.Protect & readable) &&
                !(info.Protect & PAGE_GUARD))
            {
                ++regions;
                __try
                {
                    for (uintptr_t at = base; at + 16 <= base + size;
                         at += 4)
                    {
                        const auto* words =
                            reinterpret_cast<const uint32_t*>(at);
                        if (words[0] != needle.value[0] ||
                            words[1] != needle.value[1] ||
                            words[2] != needle.value[2] ||
                            words[3] != needle.value[3])
                            continue;
                        ++hits;
                        // The ShipStore constructor hook never fires, so
                        // the object cannot be asked for directly.  But if
                        // this hit is one of its 21 item slots, then the
                        // object begins exactly that many bytes earlier -
                        // and an object begins with a vtable.  Trying all
                        // 21 offsets finds the store from its contents.
                        for (unsigned slot = 0;
                             slot < kShipStoreItemSlotCount; ++slot)
                        {
                            const unsigned offset =
                                kShipStoreItemSlots[slot];
                            if (at < offset)
                                continue;
                            const uintptr_t candidate = at - offset;
                            __try
                            {
                                const uintptr_t vtable =
                                    *reinterpret_cast<const uintptr_t*>(
                                        candidate);
                                if (vtable < 0x00C00000u ||
                                    vtable >= 0x00E00000u ||
                                    (vtable & 3) != 0)
                                    continue;
                                Log("cheats: %08X could be a store at "
                                    "%08X (slot +%03X, vtable %08X)",
                                    static_cast<unsigned>(at),
                                    static_cast<unsigned>(candidate),
                                    offset,
                                    static_cast<unsigned>(vtable));
                            }
                            __except (EXCEPTION_EXECUTE_HANDLER)
                            {
                            }
                        }
                        bool wrote = false;
                        if (replacement)
                        {
                            // Some of the copies live on read-only pages -
                            // those are the loaded archive, the authored
                            // source the store is built from - so the page
                            // has to be opened and closed again.
                            DWORD previous = 0;
                            if (VirtualProtect(reinterpret_cast<void*>(at),
                                               16, PAGE_READWRITE,
                                               &previous))
                            {
                                std::memcpy(reinterpret_cast<void*>(at),
                                            replacement, 16);
                                DWORD restored = 0;
                                VirtualProtect(reinterpret_cast<void*>(at),
                                               16, previous, &restored);
                                wrote = true;
                            }
                        }
                        Log("cheats: %s %s at %08X (region %08X, "
                            "protect %03X)", label,
                            replacement ? (wrote ? "replaced" : "LOCKED")
                                        : "found",
                            static_cast<unsigned>(at),
                            static_cast<unsigned>(base),
                            static_cast<unsigned>(info.Protect));
                        if (hits >= 48)
                            break;
                    }
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                }
            }
            if (!size)
                break;
            address = base + size;
        }
        Log("cheats: %s search done, %u hit(s) in %u region(s)", label,
            hits, regions);
        std::snprintf(gCheckpointMenuFeedback,
                      sizeof(gCheckpointMenuFeedback),
                      "%u hits are in the log", hits);
    }

    // Dumps the Zombie standing closest to the player.  Stand next to the
    // corpse in chapter 1's hallway B and read it, then next to any
    // ordinary slasher and read that: the difference is the state that
    // makes one of them lie there and wait.
    // The nearest live object of a class, by distance to the player.
    uintptr_t NearestEditorObject(const char* className, double* outDistance,
                                  float* outAt)
    {
        float me[12]{};
        if (!PlayerTransform(me))
            return 0;
        unsigned wanted = kEditorClassCount;
        for (unsigned i = 0; i < kEditorClassCount; ++i)
            if (std::strcmp(kEditorClasses[i].name, className) == 0)
                wanted = i;
        if (wanted == kEditorClassCount)
            return 0;
        uintptr_t nearest = 0;
        double best = 1.0e30;
        const unsigned count = EditorRegistryCount();
        for (unsigned i = 0; i < count; ++i)
        {
            if (gEditorObjects[i].classIndex != wanted)
                continue;
            float where[12]{};
            if (!EditorTransform(gEditorObjects[i], where))
                continue;
            const double dx = where[9] - me[9];
            const double dy = where[10] - me[10];
            const double dz = where[11] - me[11];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance >= best)
                continue;
            best = distance;
            nearest = ResolveEditorObject(gEditorObjects[i]);
            if (outAt)
            {
                outAt[0] = where[9];
                outAt[1] = where[10];
                outAt[2] = where[11];
            }
        }
        if (outDistance)
            *outDistance = best;
        return nearest;
    }

    // Reads the nearest light and, when the probe is armed in the INI,
    // writes one value into it.  With the INI reloading live this is a
    // sweep: change the offset, save, press again, look at the room.
    void ProbeNearestLight()
    {
        double distance = 0.0;
        float at[3]{};
        const uintptr_t light = NearestEditorObject("AnimateLight",
                                                    &distance, at);
        if (!light)
        {
            Log("cheats: no live AnimateLight nearby");
            std::snprintf(gCheckpointMenuFeedback,
                          sizeof(gCheckpointMenuFeedback),
                          "No light captured");
            return;
        }
        Log("cheats: nearest AnimateLight %.1f m away at %08X "
            "(%.1f %.1f %.1f)", distance,
            static_cast<unsigned>(light), at[0], at[1], at[2]);
        for (unsigned offset = 0; offset < 0x430; offset += 4)
        {
            unsigned raw = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(light + offset),
                                   &raw, sizeof(raw), nullptr) || !raw)
                continue;
            float value = 0.0f;
            std::memcpy(&value, &raw, sizeof(value));
            if (value > 1e-4f && value < 1e6f)
                Log("   +%03X  %08X   %.4f", offset, raw, value);
            else
                Log("   +%03X  %08X", offset, raw);
        }
        if (gZoneProbeOffset && gZoneProbeOffset < 0x430)
        {
            SIZE_T written = 0;
            if (WriteProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<void*>(light + gZoneProbeOffset),
                    &gZoneProbeValue, sizeof(gZoneProbeValue), &written))
                Log("   wrote %08X to +%03X of this light",
                    gZoneProbeValue, gZoneProbeOffset);
        }
        std::snprintf(gCheckpointMenuFeedback,
                      sizeof(gCheckpointMenuFeedback),
                      "Light %.1f m away is in the log", distance);
    }

    void LogNearestZombie()
    {
        float me[12]{};
        if (!PlayerTransform(me))
        {
            Log("cheats: no player transform yet");
            return;
        }
        unsigned zombieClass = kEditorClassCount;
        for (unsigned i = 0; i < kEditorClassCount; ++i)
            if (std::strcmp(kEditorClasses[i].name, "Zombie") == 0)
                zombieClass = i;
        uintptr_t nearest = 0;
        double best = 1.0e30;
        float bestAt[3]{};
        const unsigned count = EditorRegistryCount();
        unsigned live = 0;
        for (unsigned i = 0; i < count; ++i)
        {
            if (gEditorObjects[i].classIndex != zombieClass)
                continue;
            float where[12]{};
            if (!EditorTransform(gEditorObjects[i], where))
                continue;
            ++live;
            const double dx = where[9] - me[9];
            const double dy = where[10] - me[10];
            const double dz = where[11] - me[11];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (distance >= best)
                continue;
            best = distance;
            nearest = ResolveEditorObject(gEditorObjects[i]);
            bestAt[0] = where[9];
            bestAt[1] = where[10];
            bestAt[2] = where[11];
        }
        if (!nearest)
        {
            Log("cheats: no live Zombie in the registry");
            std::snprintf(gCheckpointMenuFeedback,
                          sizeof(gCheckpointMenuFeedback),
                          "No enemy in range");
            return;
        }
        Log("cheats: nearest Zombie %.1f m away at %.1f %.1f %.1f "
            "(%u live of %u entries)", best, bestAt[0], bestAt[1],
            bestAt[2], live, count);
        LogObjectWords("Zombie", nearest, 0xA90);
        std::snprintf(gCheckpointMenuFeedback,
                      sizeof(gCheckpointMenuFeedback),
                      "Enemy %.1f m away is in the log", best);
    }

    // The Line Gun and the Ripper, read out of the global item dictionary.
    constexpr Guid128 kItemLineGun{{
        0x36C8BC49u, 0x5F163CD0u, 0x44455257u, 0x34303245u}};
    constexpr Guid128 kItemRipper{{
        0x36C8BC4Fu, 0x6F9078E2u, 0x44455257u, 0x34303245u}};


    struct KnownItem
    {
        Guid128 id;
        const char* name;
    };
    // Read out of the global item dictionary in global_assets.
    constexpr KnownItem kKnownItems[] = {
        {{{0x36C8BC4Fu, 0x07EADE56u, 0x44455257u, 0x34303245u}}, "Plasma Cutter"},
        {{{0x36C8BC49u, 0x5F163CD0u, 0x44455257u, 0x34303245u}}, "Line Gun"},
        {{{0x36C8BC4Fu, 0x67BB5F38u, 0x44455257u, 0x34303245u}}, "Pulse Rifle"},
        {{{0x36C8BC4Fu, 0x6F9078E2u, 0x44455257u, 0x34303245u}}, "Ripper"},
        {{{0x36C8BC3Fu, 0xF55615C8u, 0x44455257u, 0x34303245u}}, "Contact Beam"},
        {{{0x36C8BC40u, 0x0C67B69Au, 0x44455257u, 0x34303245u}}, "Flamethrower"},
        {{{0x4BC78C36u, 0x9D651322u, 0x544E4543u, 0x53574F4Bu}}, "Plasma Cutter Battery"},
        {{{0x4BC78C36u, 0x9D67757Cu, 0x544E4543u, 0x53574F4Bu}}, "Pulse Rifle Bullet"},
        {{{0x4BC78CECu, 0xDFF02CCFu, 0x544E4543u, 0x53574F4Bu}}, "Line Gun Rack"},
        {{{0x30C87377u, 0xE3BC55EAu, 0x4C454D50u, 0x4B534544u}}, "Contact Beam Ammo"},
        {{{0x30C8692Du, 0x79E101DCu, 0x4E484F4Au, 0x324E4F53u}}, "Ripper Ammo"},
        {{{0x30C8692Du, 0x6DBBF822u, 0x4E484F4Au, 0x324E4F53u}}, "Flamethrower Fuel"},
        {{{0x30C87D90u, 0xE7570C3Eu, 0x4C454D50u, 0x4B534544u}}, "Force Gun Ammo"},
    };

    const char* KnownItemName(const Guid128& id)
    {
        for (const KnownItem& item : kKnownItems)
            if (SameGuid(item.id, id))
                return item.name;
        return "";
    }

    void LogShipStore()
    {
        const uintptr_t store = LiveEditorObject("ShipStore");
        if (!store)
        {
            Log("cheats: no live ShipStore captured");
            std::snprintf(gCheckpointMenuFeedback,
                          sizeof(gCheckpointMenuFeedback),
                          "No store captured; press this while standing in one");
            return;
        }
        Log("cheats: ShipStore at %08X", static_cast<unsigned>(store));
        unsigned filled = 0;
        for (unsigned i = 0; i < kShipStoreItemSlotCount; ++i)
        {
            const unsigned offset = kShipStoreItemSlots[i];
            Guid128 id{};
            __try
            {
                std::memcpy(&id, reinterpret_cast<const void*>(store + offset),
                            sizeof(id));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("   +%03X  unreadable", offset);
                continue;
            }
            if (!id.value[0] && !id.value[1] && !id.value[2] && !id.value[3])
                continue;
            ++filled;
            Log("   +%03X  %08X-%08X-%08X-%08X  %s", offset,
                id.value[0], id.value[1], id.value[2], id.value[3],
                KnownItemName(id));
        }
        Log("cheats: %u of %u store slots filled", filled,
            kShipStoreItemSlotCount);
        std::snprintf(gCheckpointMenuFeedback,
                      sizeof(gCheckpointMenuFeedback),
                      "%u filled store slots are in the log", filled);
    }

    void ApplyCheatMenuRow(unsigned row)
    {
        if (row == kCheatRowWeaponWipe)
        {
            // The inventory manager is built before our hooks, so it is
            // never captured. The player itself carries the three slot
            // registries (m_handle, TAG, r_weapon at +0xC9C, +0xCF0, +0xD44;
            // four 12 byte slots each, the same structure the slot guard
            // protects). First step: read them, so the removal can be built
            // on the real layout instead of a guess.
            const uintptr_t player = PlayerObject();
            if (!player)
            {
                std::snprintf(gCheckpointMenuFeedback, sizeof(gCheckpointMenuFeedback),
                              "No player yet; load a level first");
                return;
            }
            Log("weapon wipe: player at %08X; registries follow (m_handle +0xC9C, TAG +0xCF0, r_weapon +0xD44)",
                static_cast<unsigned>(player));
            LogObjectWords("player+0xC9C", player + 0xC9C, 0x100);
            // The registries held no weapons (13.09.), so the managers are
            // looked up by their class descriptor at +0 instead.
            ScanForItemGuids();
            std::snprintf(gCheckpointMenuFeedback, sizeof(gCheckpointMenuFeedback),
                          "Inventory candidates written to the log; send DeadSpace1Patch.log");
            return;
        }
        if (row == kCheatRowSuitPick || row == kCheatRowSuitRandom)
        {
            RequestSuitChange(gSuitMenuIndex, row == kCheatRowSuitRandom);
            std::snprintf(gCheckpointMenuFeedback, sizeof(gCheckpointMenuFeedback),
                          "%s", gSuitChangeFeedback);
            return;
        }
        if (row == kCheatWeaponCount + 5)
        {
            ProbeNearestLight();
            return;
        }
        if (row == kCheatWeaponCount + 4)
        {
            LogShipStore();
            return;
        }
        if (row == kCheatWeaponCount + 3)
        {
            // Fifteen copies of the Line Gun's id exist while the shop is
            // open, and reasoning about which one it reads has failed four
            // times.  Overwrite all of them and look at the screen: that
            // settles whether the shop can be changed at runtime at all,
            // and only then is it worth narrowing down which copy matters.
            ScanForGuid(kItemLineGun, "Line Gun", &kItemRipper);
            return;
        }
        if (row == kCheatWeaponCount + 2)
        {
            LogNearestZombie();
            return;
        }
        if (row == kCheatWeaponCount + 1)
        {
            ScanForGuid(kItemLineGun, "Line Gun");
            return;
        }
        const uintptr_t inventory = LiveEditorObject("PlayerMX9Inventory");
        const uintptr_t weapons = LiveEditorObject("WeaponManager");
        if (row < kCheatWeaponCount)
            Log("cheats: '%s' requested; reading the state first",
                kCheatWeapons[row]);
        LogObjectWords("PlayerMX9Inventory", inventory, 0x1E0);
        LogObjectWords("WeaponManager", weapons, 0x200);
        std::snprintf(gCheckpointMenuFeedback, sizeof(gCheckpointMenuFeedback),
                      inventory || weapons ?
                      "State is in the log; writing waits until the field is known" :
                      "Inventory not captured yet; load a level first");
    }

#include "level_order.inc"

    void HookEnemyFactories(uintptr_t table)
    {
        for (int step = -400; step <= 400 && gActorCount < kMaxActorClasses; ++step)
        {
            const uintptr_t entry = table + step * 16;
            unsigned factory = 0, name = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(entry + 4), &factory,
                                   sizeof(factory), nullptr) ||
                !ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(entry + 8), &name,
                                   sizeof(name), nullptr))
                continue;
            if (factory < 0x00401000 || factory >= 0x00BC9000)
                continue;
            char text[40]{};
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(name), text,
                                   sizeof(text) - 1, nullptr))
                continue;
            // The Player entry is deliberately not hooked any more. Its
            // function is a component accessor, not a constructor, so ECX
            // there is not the object; the player is resolved through the
            // manager instead. See PlayerObject().
            if (std::strcmp(text, "Player") == 0)
                continue;
            bool gesucht = false;
            for (unsigned i = 0; i < kWantedCount; ++i)
                if (std::strcmp(text, kWanted[i]) == 0)
                    gesucht = true;
            if (!gesucht)
                continue;
            bool schon = false;
            for (unsigned i = 0; i < gActorCount; ++i)
                if (std::strcmp(gActorNames[i], text) == 0)
                    schon = true;
            if (schon)
                continue;
            const unsigned slot = gActorCount;
            gEnemyFactories[slot] = VerifiedMid(
                reinterpret_cast<void*>(factory), EnemyHookFor(slot));
            if (!gEnemyFactories[slot])
                continue;
            std::strncpy(gActorNames[slot], text, 39);
            ++gActorCount;
        }
        Log("enemy classes hooked: %u of %u known", gActorCount, kWantedCount);
        for (unsigned i = 0; i < gActorCount; ++i)
            Log("   %2u  %s", i, gActorNames[i]);
    }

    // The only reliable game-thread tick available.  The background worker
    // must never call into the game, FuncSpawn constructors stop once a level
    // is built, and the spawn handler must not dispatch stream work.  Without
    // a heartbeat the streaming state machine is only ever advanced by an
    // authored trigger, which is why the first encounter after a level start
    // stayed authored and why a checkpoint reload lost the global pool for
    // good: the reload cooldown outlived the last constructor.
    void GlobalStreamHeartbeat()
    {
        static thread_local bool inside = false;
        static ULONGLONG last = 0;
        if (inside || !gEnemyRandomizerGlobalStreaming ||
            !gEnemyRandomizerGlobalTemplates || !gActorCount)
            return;
        // Allocations happen on loading threads too.  Only the thread that
        // builds FuncSpawn objects may call back into the game.
        if (!gGameThreadId || GetCurrentThreadId() != gGameThreadId)
            return;
        const ULONGLONG now = GetTickCount64();
        if (now - last < 250)
            return;
        last = now;

        // Stop as soon as the world is being torn down.  Reading stream state
        // is harmless, but the progress step calls the game's own loader, and
        // doing that while the level is going away is the same mistake that
        // produced the loading-screen deadlocks.  The level archive leaving
        // state 3 after it once reached it is the signal; the latch resets so
        // the next level starts normally.
        static bool levelWasReady = false;
        void* manager = nullptr;
        if (ResolveStreamManager(&manager) && gLevelArchiveHandle)
        {
            const bool ready = StreamStateFor(manager, gLevelArchiveHandle) == 3;
            if (ready)
                levelWasReady = true;
            else if (levelWasReady)
            {
                levelWasReady = false;
                Log("randomizer heartbeat stopped: the level archive is no "
                    "longer resident, so the world is being torn down");
                return;
            }
            else
                return;
        }

        inside = true;
        RefreshSuperExploderResidency(manager);
        LogCharacterResidencyChanges();
        ProgressGlobalEnemyStreaming();
        inside = false;
    }

    void Allocator_Hook(safetyhook::Context& ctx)
    {
        static volatile LONG beat = 0;
        if ((InterlockedIncrement(&beat) & 0x3FF) == 0)
            GlobalStreamHeartbeat();
        if (!gWatching)
            return;
        const unsigned size = *reinterpret_cast<unsigned*>(ctx.esp + 4);
        // One spawn triggers hundreds of allocations, nearly all of them small
        // pieces of the entity rather than the entity itself.
        if (size < 0x200)
            return;
        for (unsigned i = 0; i < gSizeCount; ++i)
            if (gSizes[i].size == size)
            {
                ++gSizes[i].count;
                return;
            }
        if (gSizeCount < 48)
            gSizes[gSizeCount++] = { size, 1 };
    }

    void LoadClassTable(uintptr_t anyEntry)
    {
        for (int step = -400; step <= 400 && gClassCount < 400; ++step)
        {
            const uintptr_t entry = anyEntry + step * 16;
            unsigned name = 0, size = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(entry + 8), &name,
                                   sizeof(name), nullptr) ||
                !ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(entry + 12), &size,
                                   sizeof(size), nullptr))
                continue;
            if (name < 0x00400000 || name >= 0x01000000 || !size || size > 0x8000)
                continue;
            char text[40]{};
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(name), text,
                                   sizeof(text) - 1, nullptr))
                continue;
            if (text[0] < 'A' || text[0] > 'z')
                continue;
            std::strncpy(gClassNames[gClassCount], text, 39);
            gClasses[gClassCount] = { size, gClassNames[gClassCount] };
            ++gClassCount;
        }
        Log("class table: %u classes read", gClassCount);
    }

    // The table is built on the heap at load time, so its address moves. It is
    // located through the one thing that does not: the address of the string
    // "FuncSpawn" in the image, which every copy of that entry points at.
    constexpr uintptr_t kFuncSpawnName = 0x00C5BF14;

    uintptr_t FindClassTable()
    {
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        auto address = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        const auto limit = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
        MEMORY_BASIC_INFORMATION region{};
        while (address < limit && VirtualQuery(reinterpret_cast<void*>(address),
                                               &region, sizeof(region)))
        {
            const bool usable = region.State == MEM_COMMIT &&
                                !(region.Protect & PAGE_GUARD) &&
                                region.Protect != PAGE_NOACCESS &&
                                (region.Protect & (PAGE_READWRITE | PAGE_READONLY |
                                                   PAGE_WRITECOPY));
            const bool inImage = reinterpret_cast<uintptr_t>(region.BaseAddress) >= 0x00400000 &&
                                 reinterpret_cast<uintptr_t>(region.BaseAddress) < 0x01000000;
            if (usable && !inImage && region.RegionSize <= 0x8000000)
            {
                __try
                {
                    auto* words = reinterpret_cast<uintptr_t*>(region.BaseAddress);
                    const size_t count = region.RegionSize / sizeof(uintptr_t);
                    for (size_t i = 0; i < count; ++i)
                        if (words[i] == kFuncSpawnName)
                            return reinterpret_cast<uintptr_t>(words + i) - 8;
                }
                __except (EXCEPTION_EXECUTE_HANDLER)
                {
                    // Freed underneath us between the query and the read.
                }
            }
            address = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
        }
        return 0;
    }

    void __fastcall SpawnHandler_Hook(void* self, void*, void* message)
    {
        static thread_local bool inside = false;
        static bool reportedPendingGlobalPool = false;
        const uintptr_t base = reinterpret_cast<uintptr_t>(self);

        // A re-entrant call is one of ours; let it through untouched.
        // Calls passed through untouched never carry another call's death message.
        if (inside || !base || base < kHandlerBaseOffset || !message)
        {
            const TransplantState passThrough = ArmTransplant(false, Guid128{});
            gSpawnHandler.thiscall<void>(self, message);
            RestoreTransplant(passThrough);
            return;
        }

        const uintptr_t spawner = base - kHandlerBaseOffset;
        if (!StillASpawner(spawner))
        {
            const TransplantState passThrough = ArmTransplant(false, Guid128{});
            gSpawnHandler.thiscall<void>(self, message);
            RestoreTransplant(passThrough);
            return;
        }

        // A FuncSpawn event is a proven game-thread boundary in a live world.
        // The initial UniformManager call happens during level construction,
        // before all dependencies required by a non-default suit exist.  Keep
        // the stock start request there and perform a pending seeded change
        // only after both this boundary and the exact crash dependency agree
        // that the runtime is ready.
        TryApplyPendingStartingSuit();
        TryApplyPendingSuitChange();

        // A FuncSpawn only handles events in a running world, never while the
        // loading screen is up.  This is the fallback evidence the character
        // pin needs when the loading-screen archive stays resident.
        InterlockedExchange(&gSpawnHandlerEntered, 1);

        // Never dispatch new StreamManager work from inside FuncSpawn event
        // handling.  Once target requests have already returned, however,
        // this game-thread boundary is the last guaranteed opportunity to
        // refresh SimManager registration before the first authored trigger.
        // ProgressGlobalEnemyStreaming performs no load calls in the
        // TargetsActivated phase; it only validates the published keys and
        // promotes the pool to Ready (or fails closed after its timeout).
        const LONG streamPhase = InterlockedCompareExchange(
            &gGlobalStreamPhase, 0, 0);
        if (!inside && streamPhase == kGlobalStreamTargetsActivated)
        {
            ProgressGlobalEnemyStreaming();
            if (gEnemyRandomizerDiagnostics && !gGlobalEnemyPoolLoaded &&
                !reportedPendingGlobalPool)
            {
                reportedPendingGlobalPool = true;
                const ULONGLONG elapsed = gGlobalStreamRequestedTick ?
                    GetTickCount64() - gGlobalStreamRequestedTick : 0;
                Log("Randomizer: first trigger reached with global pool still pending after %llu ms",
                    elapsed);
                LogGlobalPolicyVariantStatus();
            }
        }

        const unsigned incoming = *reinterpret_cast<unsigned*>(message);
        const unsigned trigger = *reinterpret_cast<unsigned*>(spawner + kTriggerMessage);
        if (!trigger || incoming != trigger)
        {
            const TransplantState passThrough = ArmTransplant(false, Guid128{});
            gSpawnHandler.thiscall<void>(self, message);
            RestoreTransplant(passThrough);
            return;      // not this spawner's cue
        }

        // Context V1 can choose a pre-catalogued, resident global target on
        // the first firing. Legacy mode still learns local templates from
        // stock calls. In both modes only +0x50 changes for this one retail
        // call; source UID, trigger, transform, vent selection and FuncSpawn
        // bookkeeping remain the original object's data.
        Guid128 originalTarget{};
        Guid128 replacementTarget{};
        Guid128 sourceEntity{};
        std::memcpy(&originalTarget,
                    reinterpret_cast<const void*>(spawner + kTargetGuid),
                    sizeof(originalTarget));
        std::memcpy(&sourceEntity,
                    reinterpret_cast<const void*>(spawner + kSourceGuid),
                    sizeof(sourceEntity));
        unsigned originalSlot = LearnedSlotFor(originalTarget);
        if (originalSlot >= gActorCount)
            originalSlot = ResidentSlotFor(originalTarget);
        // An exact Context V1 record already carries the authored actor-class
        // contract.  Use it on the first firing instead of requiring the
        // stock factory to spawn (and teach us) the original enemy first.
        // This is what lets a newly loaded encounter randomize immediately,
        // rather than only after a checkpoint restart.
        if (originalSlot >= gActorCount && gEnemyRandomizerContextV1)
        {
            const SpawnCompatibilitySource* source =
                CompatibilitySourceFor(sourceEntity, originalTarget);
            if (source && source->actorClass)
            {
                originalSlot = ActorSlotNamed(source->actorClass);
                if (gEnemyRandomizerDiagnostics && originalSlot < gActorCount)
                    Log("Randomizer: inferred first-fire actor class %s from exact context table",
                        source->actorClass);
            }
        }
        unsigned expectedSlot = ExpectedReplacementSlot(originalSlot);
        bool replaced = false;
        bool replacementAttempted = false;
        bool placedDirectly = false;   // the bound vent donor stands where the authored one stood
        SequenceRecast recast{};       // the scripted spawner's sequences, pointed at the donor
        const SpawnCompatibilitySource* contextSource = CompatibilitySourceFor(sourceEntity, originalTarget);
        const bool scriptedSource = contextSource && (contextSource->context & kSpawnContextScripted) != 0;
        bool enhancedExploderAttempted = false;
        bool localBindingPatched = false;
        uint32_t originalOwnerContext = 0;

        auto bindReplacement = [&](const Guid128& target) -> bool
        {
            const uintptr_t donorSpawner = LiveSpawnerForTarget(target, spawner);
            if (!donorSpawner)
            {
                if (gEnemyRandomizerDiagnostics)
                    Log("Randomizer: target %08X-%08X-%08X-%08X has no live donor FuncSpawn owner",
                        target.value[0], target.value[1], target.value[2],
                        target.value[3]);
                return false;
            }

            uint32_t donorOwnerContext = 0;
            if (!ReadProcessMemory(GetCurrentProcess(),
                                   reinterpret_cast<void*>(donorSpawner + kOwnerContext),
                                   &donorOwnerContext, sizeof(donorOwnerContext), nullptr))
                return false;
            // Only the first bind of a trigger may record the authored owner.
            // A second bind - the deliberate extra spawn after a replacement -
            // would otherwise memorise the donor token of the first one, and
            // restoring afterwards would leave a foreign owner in the spawner.
            if (!replaced)
                std::memcpy(&originalOwnerContext,
                            reinterpret_cast<const void*>(spawner + kOwnerContext),
                            sizeof(originalOwnerContext));
            // Target UID and group-local owner form one binding. Borrow both
            // from the synthetic donor for the single untouched retail call;
            // mixing a sidecar target with the chapter owner is rejected by
            // filter 0 and bypassing that rejection creates an unlinked actor.
            std::memcpy(reinterpret_cast<void*>(spawner + kOwnerContext),
                        &donorOwnerContext, sizeof(donorOwnerContext));
            std::memcpy(reinterpret_cast<void*>(spawner + kTargetGuid),
                        &target, sizeof(target));
            replaced = true;
            if (gEnemyRandomizerDiagnostics)
                Log("Randomizer: using donor FuncSpawn %08X binding; owner %08X -> %08X",
                    static_cast<unsigned>(donorSpawner), originalOwnerContext,
                    donorOwnerContext);
            return true;
        };

        auto restoreTarget = [&]()
        {
            if (replaced)
            {
                std::memcpy(reinterpret_cast<void*>(spawner + kTargetGuid),
                            &originalTarget, sizeof(originalTarget));
                std::memcpy(reinterpret_cast<void*>(spawner + kOwnerContext),
                            &originalOwnerContext,
                            sizeof(originalOwnerContext));
                replaced = false;
            }
        };

        // One row per variant that stands in for a base creature.  This used
        // to be an Exploder-only branch; the only creature-specific things
        // left are the class name and the model name, and both come from the
        // table now.
        VariantSubstitution* substitution = nullptr;
        const KnownVariant* substituteWith = nullptr;
        const bool rulesHoldBack = SpawnerRulesHoldBack(
            sourceEntity, originalTarget, originalSlot < gActorCount ? gActorNames[originalSlot] : nullptr);
        for (unsigned i = 0; !rulesHoldBack && !scriptedSource && i < gVariantSubstitutionCount; ++i)
        {
            VariantSubstitution& row = gVariantSubstitutions[i];
            if (originalSlot != ActorSlotNamed(row.variant->base))
                continue;
            if (!ChooseVariantSpawn(row, SourceChapter(CompatibilitySourceFor(sourceEntity, originalTarget)),
                                    sourceEntity, originalTarget))
                break;
            substitution = &row;
            substituteWith = row.variant;
            break;
        }
        const unsigned exploderSlot = ActorSlotNamed("Exploder");
        if (substituteWith)
        {
            const char* const variantModel = substituteWith->model;
            const unsigned variantOrdinal = substitution->ordinal;
            const SpawnCompatibilitySource* source =
                CompatibilitySourceFor(sourceEntity, originalTarget);
            // The vent catalog only holds Exploder targets today, so any
            // other base falls back to the spawn context alone.
            const int catalogVent = originalSlot == exploderSlot
                ? ExploderVentModeFor(originalTarget) : -1;
            const int wantVent = source
                ? ((source->context & kSpawnContextAnyVent) != 0 ? 1 : 0)
                : catalogVent;
            bool modeMatched = false;
            Log("variant spawn %s #%u: source %s target %08X-%08X mode %s (%s)",
                variantModel, variantOrdinal, SpawnerName(sourceEntity),
                originalTarget.value[0], originalTarget.value[1],
                wantVent == 1 ? "vent" : wantVent == 0 ? "direct" : "unknown",
                source ? "exact context" :
                    catalogVent >= 0 ? "offline target catalog" : "unresolved");
            // Every sup_expl donor emerges from a vent, because the only
            // directly placed one lives in enemy pool 2 and that pool ships
            // 327 entities of chapter scripting for it.  A direct spawn is
            // served by asking for a vent donor and writing its arrival kind
            // back to the constructor default once the component has decoded;
            // see ApplyVariantSpawnPlacement.
            const bool sup = std::strcmp(variantModel, "sup_expl") == 0;
            const bool placeDirectly = sup && wantVent == 0;
            const int askFor = placeDirectly ? 1 : wantVent;
            if (wantVent < 0)
                Log("variant spawn %s #%u: unresolved authored spawn mode; "
                    "authored %s retained", variantModel, variantOrdinal,
                    substituteWith->base);
            else if (ExtraSpawnTargetFor(variantModel, spawner,
                                         &replacementTarget,
                                         askFor, &modeMatched, -1, false))
            {
                expectedSlot = originalSlot;
                replacementAttempted = bindReplacement(replacementTarget);
                enhancedExploderAttempted = replacementAttempted;
                // Arm the conversion only once the donor is actually bound,
                // so a failed binding never leaves it waiting for a component
                // that will not come.
                gPendingDirectPlacement = placeDirectly && replacementAttempted;
                placedDirectly = gPendingDirectPlacement;
                Log("variant spawn %s #%u: %s target %08X-%08X (%s donor%s)",
                    variantModel, variantOrdinal,
                    replacementAttempted ? "using authored" :
                                           "donor binding failed for",
                    replacementTarget.value[0], replacementTarget.value[1],
                    modeMatched ? "matching" : "fallback",
                    placeDirectly ? ", placed directly" : "");
            }
            else if (wantVent >= 0)
                Log("variant spawn %s #%u: no usable target; authored %s "
                    "retained", variantModel, variantOrdinal,
                    substituteWith->base);
        }

        if (!enhancedExploderAttempted && gEnemyRandomizer &&
            gEnemyRandomizerChance &&
            originalSlot < gActorCount)
        {
            const uint32_t chanceRoll = StableRandomizerHash(
                sourceEntity, originalTarget, 0x43484E43u);
            if ((chanceRoll % 100u) < gEnemyRandomizerChance)
            {
                // Same-class variants still need their complete authored
                // SpawnInfo packet.  Rewriting only RagdollComponent leaves
                // Zombie/CadaverousNPC/Grappler data from the Slasher and
                // therefore still constructs a Slasher.
                bool candidatePlaced = false;
                if (!localBindingPatched &&
                    CandidateFor(originalTarget, originalSlot, sourceEntity,
                                 &expectedSlot, spawner, &replacementTarget,
                                 &candidatePlaced, &recast))
                {
                    replacementAttempted = bindReplacement(replacementTarget);
                    if (replacementAttempted)
                        ApplySequenceRecast(&recast, replacementTarget);
                    gPendingDirectPlacement = candidatePlaced && replacementAttempted;
                    placedDirectly = gPendingDirectPlacement;
                    if (replacementAttempted && gEnemyRandomizerDiagnostics)
                        Log("Randomizer: replacing %s with live %s target %08X-%08X-%08X-%08X",
                            gActorNames[originalSlot], gActorNames[expectedSlot],
                            replacementTarget.value[0], replacementTarget.value[1],
                            replacementTarget.value[2], replacementTarget.value[3]);
                }
            }
        }

        // Watch the game's own call rather than making one of our own.
        gCreatedIndex = 0xFFFF;
        gSeenClassCount = 0;
        gFactoryInvocationCount = 0;
        gLastResolverFactoryObject = 0;
        // The replacement dies with the source target's message, not the donor's
        // (death_message_transplant.inc); armed for this one retail call.
        TransplantState outerTransplant = ArmTransplant(replacementAttempted, originalTarget);
        const WatchCaptureState primaryCapture = WatchCaptureBegin();
        gWatching = true;
        gSpawnHandler.thiscall<void>(self, message);
        gWatching = false;
        const uintptr_t primaryActor = WatchCaptureEnd(primaryCapture);
        gPendingDirectPlacement = false;   // armed for this call only
        void* const primaryComponent = RestoreTransplant(outerTransplant);

        if (originalSlot == exploderSlot)
            LogExploderVisualResources(gLastResolverFactoryObject,
                                       enhancedExploderAttempted);
            LogExploderStreamBindings(gLastResolverFactoryObject,
                                      enhancedExploderAttempted);

        if (enhancedExploderAttempted && gSpawnResolverDiagnostics)
        {
            const char* modelName = nullptr;
            char modelText[32]{};
            if (gLastResolverFactoryObject &&
                ReadProcessMemory(GetCurrentProcess(),
                    reinterpret_cast<const void*>(
                        gLastResolverFactoryObject + kEntityModelName),
                    &modelName, sizeof(modelName), nullptr) && modelName)
                ReadProcessMemory(GetCurrentProcess(), modelName, modelText,
                                  sizeof(modelText) - 1, nullptr);
            Log("super exploder factory object: object=%08X model_ptr=%08X "
                "model='%s'", static_cast<unsigned>(
                    gLastResolverFactoryObject),
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(modelName)),
                modelText[0] ? modelText : "unavailable");
        }

        // A resident SimG is necessary but not sufficient: the root entity
        // UID may be a container rather than the particular SpawnInfo target
        // FuncSpawn resolves.  An experimental global candidate that made no
        // recognised actor and did not advance the stock counter is rejected
        // immediately, then the untouched original request is retried.  This
        // prevents a bad catalogue entry from silently deleting an encounter.
        bool replacementAccepted = replaced && gCreatedIndex == expectedSlot &&
            gSeenClassCount == 1 && gFactoryInvocationCount == 1;
        // What the watchdog follows: the accepted replacement's actor and health component.
        uintptr_t acceptedActor = replacementAccepted ? primaryActor : 0;
        void* acceptedComponent = replacementAccepted ? primaryComponent : nullptr;
        if (replaced && !replacementAccepted)
        {
            // The rejected actor stays next to the authored one replayed below.
            if (gCreatedIndex < gActorCount)
                RevokeTransplant(primaryComponent, "replacement rejected");
            const unsigned failedCreated = gCreatedIndex;
            const unsigned failedSeenCount = gSeenClassCount;
            const unsigned failedInvocationCount = gFactoryInvocationCount;
            unsigned failedSeen[8]{};
            std::memcpy(failedSeen, gSeenClasses, sizeof(failedSeen));
            LogFactoryObservation("primary replacement", expectedSlot,
                                  failedCreated, failedInvocationCount,
                                  failedSeenCount, failedSeen);
            bool failedSawExpected = false;
            for (unsigned i = 0; i < failedSeenCount && i < 8; ++i)
                if (failedSeen[i] == expectedSlot)
                    failedSawExpected = true;

            Guid128 alternate{};
            const SpawnCompatibilitySource* source =
                CompatibilitySourceFor(sourceEntity, originalTarget);
            const bool haveAlternate = gEnemyRandomizerRetryFailedSpawns &&
                !failedSawExpected && source &&
                AlternateGlobalInstanceFor(*source, originalTarget,
                                           replacementTarget, &alternate) &&
                (!recast.applied || DonorIsFree(alternate));
            if (haveAlternate)
            {
                restoreTarget();
                replacementTarget = alternate;
                if (!bindReplacement(replacementTarget))
                    replacementAccepted = false;
                else if (recast.applied)
                    ApplySequenceRecast(&recast, replacementTarget);
                gCreatedIndex = 0xFFFF;
                gSeenClassCount = 0;
                gFactoryInvocationCount = 0;
                if (gEnemyRandomizerDiagnostics)
                    Log("Randomizer: retrying alternate instance %08X-%08X-%08X-%08X",
                        replacementTarget.value[0], replacementTarget.value[1],
                        replacementTarget.value[2], replacementTarget.value[3]);
                if (replaced)
                {
                    outerTransplant = ArmTransplant(true, originalTarget);
                    gPendingDirectPlacement = placedDirectly;
                    const WatchCaptureState alternateCapture = WatchCaptureBegin();
                    gWatching = true;
                    gSpawnHandler.thiscall<void>(self, message);
                    gWatching = false;
                    const uintptr_t alternateActor = WatchCaptureEnd(alternateCapture);
                    gPendingDirectPlacement = false;
                    void* const alternateComponent = RestoreTransplant(outerTransplant);
                    replacementAccepted = gCreatedIndex == expectedSlot &&
                        gSeenClassCount == 1 && gFactoryInvocationCount == 1;
                    if (replacementAccepted)
                    {
                        acceptedActor = alternateActor;
                        acceptedComponent = alternateComponent;
                    }
                    if (!replacementAccepted && gCreatedIndex < gActorCount)
                        RevokeTransplant(alternateComponent, "alternate replacement rejected");
                }
                if (!replacementAccepted)
                    LogFactoryObservation("alternate replacement", expectedSlot,
                                          gCreatedIndex, gFactoryInvocationCount,
                                          gSeenClassCount,
                                          gSeenClasses);
                else if (gEnemyRandomizerDiagnostics)
                    Log("Randomizer: alternate instance accepted");
            }

            if (!replacementAccepted)
            {
                // A failed experimental target must never consume the stock
                // event and silently remove the authored enemy.  The INI
                // retry switch controls only the optional second replacement
                // candidate; the untouched authored target is always replayed
                // once as the final fail-safe.
                RevertSequenceRecast(&recast, "replacement rejected");
                restoreTarget();
                gCreatedIndex = 0xFFFF;
                gSeenClassCount = 0;
                gFactoryInvocationCount = 0;
                outerTransplant = ArmTransplant(false, originalTarget);
                gWatching = true;
                gSpawnHandler.thiscall<void>(self, message);
                gWatching = false;
                RestoreTransplant(outerTransplant);
                InterlockedIncrement(&gRandomizerFallbacks);
                if (gEnemyRandomizerDiagnostics)
                    Log("Randomizer: replacement rejected; authored FuncSpawn replayed as %s (alternate retry %s)",
                        gCreatedIndex < gActorCount ? gActorNames[gCreatedIndex] : "no actor",
                        gEnemyRandomizerRetryFailedSpawns ? "enabled" : "disabled");
            }
        }

        if (acceptedActor)
            WatchdogAdd(acceptedActor, reinterpret_cast<uintptr_t>(acceptedComponent), originalTarget,
                        replacementTarget, sourceEntity, "replacement");
        if (replacementAccepted)
            VerifySequenceRecast(recast, acceptedActor, originalOwnerContext, SpawnerName(sourceEntity));

        // A target which made exactly one recognised actor in a stock call is
        // a usable runtime template.  For a replacement, record the selected
        // target again only when its class contract was upheld.
        if (gCreatedIndex < gActorCount && gSeenClassCount == 1 &&
            gFactoryInvocationCount == 1)
        {
            const Guid128& observed = replaced ? replacementTarget : originalTarget;
            Guid128 source{};
            std::memcpy(&source, reinterpret_cast<const void*>(spawner + kSourceGuid),
                        sizeof(source));
            // Record the authored target even when the randomizer temporarily
            // substituted a candidate.  The offline report therefore stays a
            // faithful map of what this level was designed to spawn.
            // The catalog describes the authored level data.  A successful
            // temporary replacement must therefore not relabel the original
            // Pregnant source as a Zombie in the offline report.
            RecordSpawnCatalog(source, originalTarget,
                               replaced ? originalSlot : gCreatedIndex);
            RememberTarget(observed, gCreatedIndex, spawner);
            if (localBindingPatched && gCreatedIndex == expectedSlot)
                InterlockedIncrement(&gRandomizerReplacements);
            if (replacementAttempted && replaced)
            {
                if (gCreatedIndex == expectedSlot)
                    InterlockedIncrement(&gRandomizerReplacements);
                else
                {
                    InterlockedIncrement(&gRandomizerFallbacks);
                    if (gEnemyRandomizerDiagnostics)
                        Log("Randomizer: target class mismatch (%s -> %s); keeping it out of future pools",
                            originalSlot < gActorCount ? gActorNames[originalSlot] : "unknown",
                            gActorNames[gCreatedIndex]);
                }
            }
        }
        else if (replacementAttempted && replaced)
        {
            InterlockedIncrement(&gRandomizerFallbacks);
            if (gEnemyRandomizerDiagnostics)
                Log("Randomizer: replacement made no single recognised actor; authored UID restored");
        }

        // The multiplier below re-enters the stock handler.  If this was a
        // verified same-family replacement, keep the temporary UID for those
        // copies too so one authored trigger does not produce a mixture of
        // random and original templates.  Any ambiguous result is restored
        // before it can affect another call.
        const bool keepReplacementForCopies = replaced &&
            gCreatedIndex == expectedSlot && gSeenClassCount == 1 &&
            gFactoryInvocationCount == 1;
        if (replaced && !keepReplacementForCopies)
            restoreTarget();

        // Deliberate extra spawns.  Everything above only ever redirects an
        // authored trigger; this adds an enemy the level never had.  The same
        // proven machinery is used: borrow a donor's target and owner, let the
        // retail handler make the actor, restore immediately.  Direct-mode
        // targets are the right ones here - a Guardian is placed, it does not
        // climb out of anything - so the vent-mode restriction does not apply.
        // "any" turns this into a seeded sprinkle over every known spawner;
        // a concrete GUID keeps it to one place.  The roll uses the same stable
        // hash as the randomizer, so a checkpoint reload reproduces it.
        // Pick the rule that applies here.  A named spawner beats "any", so a
        // general sprinkle can be overridden for one specific place.
        const ExtraSpawnRule* rule = nullptr;
        for (unsigned i = 0; i < gExtraSpawnRuleCount; ++i)
        {
            const ExtraSpawnRule& candidate = gExtraSpawnRules[i];
            if (candidate.anySource)
            {
                if (!rule)
                    rule = &candidate;
                continue;
            }
            if (SameGuid(sourceEntity, candidate.source))
            {
                rule = &candidate;
                break;
            }
        }
        ExtraSpawnRule legacy;
        if (!rule && gExtraSpawnSourceValid && gExtraSpawnVariant[0])
        {
            legacy.source = gExtraSpawnSource;
            legacy.anySource = gExtraSpawnAnySource;
            std::strncpy(legacy.variant, gExtraSpawnVariant,
                         sizeof(legacy.variant) - 1);
            legacy.count = gExtraSpawnCount;
            legacy.chance = gExtraSpawnChancePercent;
            if (legacy.anySource || SameGuid(sourceEntity, legacy.source))
                rule = &legacy;
        }
        // An "any" rule fires at every spawner the level triggers, and most
        // of those are not fights: measured in chapter 1, Kendra, Hammond,
        // a PDA and a door blocker each brought a necromorph along.  The
        // compatibility catalogue only knows spawners that produce enemies,
        // so its silence is the filter.  A rule naming one spawner still
        // applies anywhere - that is a deliberate choice, not a sprinkle.
        const SpawnCompatibilitySource* here =
            CompatibilitySourceFor(sourceEntity, originalTarget);
        if (rule && rule->anySource && (!here || (here->context & kSpawnContextScripted)))
        {
            if (gExtraSpawnDiagnostics)
                Log("extra spawn: %08X is %s, skipped", static_cast<unsigned>(spawner),
                    here ? "a scripted spawner" : "not an enemy spawner");
            rule = nullptr;
        }
        const bool extraChanceHit =
            rule && (rule->chance >= 100 ||
                     (StableRandomizerHash(sourceEntity, originalTarget,
                                           0x45585452u) % 100) < rule->chance);
        if (rule && extraChanceHit)
        {
            // The extra spawns overwrite the primary call's observation and
            // restore the borrowed target; the copies further down need both.
            const unsigned primaryCreated = gCreatedIndex;
            const unsigned primarySeenCount = gSeenClassCount;
            unsigned primarySeen[8]{};
            std::memcpy(primarySeen, gSeenClasses, sizeof(primarySeen));
            // Does this place emerge from a shaft or is it a plain placement?
            // The authored contract knows; without one we do not guess.
            int wantVent = -1;
            if (here)
                wantVent = (here->context & kSpawnContextAnyVent) != 0 ? 1 : 0;
            for (unsigned n = 0; n < rule->count; ++n)
            {
                Guid128 extra{};
                bool modeMatched = true;
                if (!ExtraSpawnTargetFor(rule->variant, spawner, &extra,
                                         wantVent, &modeMatched,
                                         rule->wantMode))
                {
                    Log("extra spawn: no usable %s target (key, donor or "
                        "character archive missing)", rule->variant);
                    break;
                }
                if (!modeMatched && !rule->hasPosition)
                    Log("extra spawn warning: %s has no %s donor, so it will "
                        "not %s here - give the rule its own position "
                        "(variant,count,percent,x,y,z)",
                        rule->variant, wantVent == 1 ? "vent" : "direct",
                        wantVent == 1 ? "climb out of the shaft"
                                      : "stand where it should");
                if (!bindReplacement(extra))
                    break;
                // The authored spawn cap at +0xBC is not always -1: this
                // spawner allows exactly one enemy, so after the authored or
                // replaced one the handler refuses every further call and
                // returns before it even reaches the resolver - which is why
                // the extra spawn produced nothing and left no resolver line
                // at all.  Lift the cap for the one call and put it back.
                unsigned savedCap = 0;
                unsigned savedCount = 0;
                std::memcpy(&savedCap,
                            reinterpret_cast<const void*>(spawner + kMaxSpawn),
                            sizeof(savedCap));
                std::memcpy(&savedCount,
                            reinterpret_cast<const void*>(spawner + kNumSpawned),
                            sizeof(savedCount));
                const unsigned unlimited = 0xFFFFFFFFu;
                std::memcpy(reinterpret_cast<void*>(spawner + kMaxSpawn),
                            &unlimited, sizeof(unlimited));
                // An own position for the one call.  The borrowed spawner is
                // still the authored one afterwards, so this is put back like
                // the cap is.
                float savedPosition[3]{};
                std::memcpy(savedPosition,
                            reinterpret_cast<const void*>(spawner + 0xB0),
                            sizeof(savedPosition));
                if (rule->hasPosition)
                    std::memcpy(reinterpret_cast<void*>(spawner + 0xB0),
                                rule->position, sizeof(rule->position));

                // Facing.  The spawner's 4x4 at +0x70 holds the three axes in
                // its first rows; the fourth stays untouched.  A yaw around
                // the vertical axis is the whole of it: measured against the
                // player's own matrix, X is (cos, 0, -sin) and Z is
                // (sin, 0, cos).
                float savedAxes[12]{};
                std::memcpy(savedAxes,
                            reinterpret_cast<const void*>(spawner + 0x70),
                            sizeof(savedAxes));
                if (rule->hasYaw)
                {
                    const double radians = rule->yaw * 3.14159265358979 / 180.0;
                    const float c = static_cast<float>(std::cos(radians));
                    const float sn = static_cast<float>(std::sin(radians));
                    const float axes[12] = { c, 0.0f, -sn, 0.0f,
                                             0.0f, 1.0f, 0.0f, 0.0f,
                                             sn, 0.0f, c, 0.0f };
                    std::memcpy(reinterpret_cast<void*>(spawner + 0x70), axes,
                                sizeof(axes));
                }
                // Watch the call the way a replacement is watched.  The old log
                // line only proved that we asked; it said nothing about whether
                // an actor actually appeared, which is exactly the question
                // when nothing is visible in the room.
                const unsigned wantSlot = ActorSlotNamed("Guardian");
                gCreatedIndex = 0xFFFF;
                gSeenClassCount = 0;
                gFactoryInvocationCount = 0;
                inside = true;
                outerTransplant = ArmTransplant(true, Guid128{});
                gWatching = true;
                gSpawnHandler.thiscall<void>(self, message);
                gWatching = false;
                RestoreTransplant(outerTransplant);
                inside = false;
                restoreTarget();
                if (rule->hasPosition)
                    std::memcpy(reinterpret_cast<void*>(spawner + 0xB0),
                                savedPosition, sizeof(savedPosition));
                if (rule->hasYaw)
                    std::memcpy(reinterpret_cast<void*>(spawner + 0x70),
                                savedAxes, sizeof(savedAxes));
                std::memcpy(reinterpret_cast<void*>(spawner + kMaxSpawn),
                            &savedCap, sizeof(savedCap));
                std::memcpy(reinterpret_cast<void*>(spawner + kNumSpawned),
                            &savedCount, sizeof(savedCount));
                Log("extra spawn result: created=%s calls=%u classes=%u "
                    "(cap was %08X, count %08X)",
                    gCreatedIndex < gActorCount ? gActorNames[gCreatedIndex]
                                                : "nothing",
                    gFactoryInvocationCount, gSeenClassCount, savedCap,
                    savedCount);
                (void)wantSlot;
                const float* where =
                    rule->hasPosition ? rule->position : savedPosition;
                Log("extra spawn: %s #%u at [%s] position %.1f %.1f %.1f "
                    "facing %.0f deg (%s, %s, %s donor)",
                    rule->variant, n + 1, SpawnerName(sourceEntity),
                    where[0], where[1], where[2],
                    rule->hasYaw ? rule->yaw : 0.0f,
                    rule->hasPosition ? "own position" : "spawner position",
                    rule->hasYaw ? "own facing" : "spawner facing",
                    modeMatched ? "matching" : "mismatched");
            }
            gCreatedIndex = primaryCreated;
            gSeenClassCount = primarySeenCount;
            std::memcpy(gSeenClasses, primarySeen, sizeof(primarySeen));
            if (keepReplacementForCopies && !replaced &&
                !bindReplacement(replacementTarget) && gEnemyRandomizerDiagnostics)
                Log("Randomizer: replacement could not be bound again for the copies; they use the authored target");
        }

        if (gCreatedIndex < gActorCount && !gDumped[gCreatedIndex])
        {
            gDumped[gCreatedIndex] = true;
            Log("=== first %s spawner ===", gActorNames[gCreatedIndex]);
            DumpSpawner(spawner);
        }

        // Nothing of an enemy class appeared: corpses, props or scenery.
        if (gCreatedIndex >= gActorCount)
        {
            InterlockedIncrement(&gSkipped);
            restoreTarget();
            return;
        }
        // A scripted actor is one of a kind: a copy is a second actor for its sequences.
        if (scriptedSource)
        {
            InterlockedIncrement(&gSkipped);
            restoreTarget();
            return;
        }
        const unsigned total = gActorPercent[gCreatedIndex];
        if (total <= 1)
        {
            InterlockedIncrement(&gSkipped);
            restoreTarget();
            return;
        }

        // Repeating a message repeats everything else it sets off, so a
        // trigger that also builds something we are told to leave at one is
        // left alone entirely.
        for (unsigned i = 0; i < gSeenClassCount; ++i)
        {
            const unsigned slot = gSeenClasses[i];
            if (slot < gActorCount && gActorPercent[slot] <= 1)
            {
                InterlockedIncrement(&gSkipped);
                restoreTarget();
                return;
            }
        }

        // A capped spawner (FuncSpawn +0xBC, 1 on about a third of them) refuses every call after
        // its own enemy, so each copy lifts the cap for its one call; cap and count go back after.
        unsigned savedCap = 0, savedCount = 0;
        std::memcpy(&savedCap, reinterpret_cast<const void*>(spawner + kMaxSpawn), sizeof(savedCap));
        std::memcpy(&savedCount, reinterpret_cast<const void*>(spawner + kNumSpawned), sizeof(savedCount));
        const unsigned unlimited = 0xFFFFFFFFu;
        inside = true;
        for (unsigned i = 1; i < total; ++i)
        {
            outerTransplant = ArmTransplant(replaced, originalTarget);
            gPendingDirectPlacement = replaced && placedDirectly;
            const WatchCaptureState copyCapture = WatchCaptureBegin();
            std::memcpy(reinterpret_cast<void*>(spawner + kMaxSpawn), &unlimited, sizeof(unlimited));
            gSpawnHandler.thiscall<void>(self, message);
            std::memcpy(reinterpret_cast<void*>(spawner + kMaxSpawn), &savedCap, sizeof(savedCap));
            const uintptr_t copyActor = WatchCaptureEnd(copyCapture);
            gPendingDirectPlacement = false;
            void* const copyComponent = RestoreTransplant(outerTransplant);
            if (replaced)
                WatchdogAdd(copyActor, reinterpret_cast<uintptr_t>(copyComponent), originalTarget,
                            replacementTarget, sourceEntity, "copy");
        }
        inside = false;
        std::memcpy(reinterpret_cast<void*>(spawner + kNumSpawned), &savedCount, sizeof(savedCount));
        restoreTarget();
        InterlockedIncrement(&gForced);
        static long lines = 0;
        if (InterlockedIncrement(&lines) <= 400)
            Log("  spawner %08X produced %s, now %u of them%s",
                static_cast<unsigned>(spawner), gActorNames[gCreatedIndex], total,
                savedCap != 0xFFFFFFFFu ? " (its cap lifted for the copies)" : "");
    }

    // The level watch below is an endless loop, so everything written after
    // it is dead code - which is exactly where the hotkey check ended up.
    // Poll it while waiting instead.
    void PollPlacementHotkey()
    {
        // The shelf is built when the store opens, long after the level was
        // constructed, so the constructor hook alone would never see it. This
        // thread only reads and writes memory, so it is safe to run it here;
        // the scan throttles itself to once every two seconds.
        if (gProgressionRandomizer)
            PatchShelfList();
        // Saving the INI now takes effect without a restart.  Checked
        // by last-write time, so an unchanged file costs one file stat.
        {
            static ULONGLONG lastCheck = 0;
            const ULONGLONG now = GetTickCount64();
            if (now - lastCheck >= 1000)
            {
                lastCheck = now;
                if (IniChanged())
                    ReloadLiveSettings();
            }
        }
        PollNoClip();
        PollGodMode();
        PollSuitDebtSave();
        if (gPlacementHotkey)
        {
            const bool down =
                (GetAsyncKeyState(gPlacementHotkey) & 0x8000) != 0;
            static bool wasDown = false;
            if (down && !wasDown)
            {
                LogPlayerPlacement();
                LogCurrentVacuumZone();
            }
            wasDown = down;
        }
        if (gWorldEditor && gZoneProbeHotkey)
        {
            const bool down =
                (GetAsyncKeyState(gZoneProbeHotkey) & 0x8000) != 0;
            static bool wasDown = false;
            if (down && !wasDown)
                LogZoneProbe();
            wasDown = down;
        }
        if (gWorldEditor && gWorldEditorHotkey)
        {
            const bool down =
                (GetAsyncKeyState(gWorldEditorHotkey) & 0x8000) != 0;
            static bool wasDown = false;
            if (down && !wasDown)
                LogWorldEditorNearby();
            wasDown = down;
        }
    }

    // Memory reads only, so a short slice cannot disturb the game. 100 ms is
    // fast enough that a normal key press is never missed.
    void SleepPollingHotkey(unsigned milliseconds)
    {
        for (unsigned slept = 0; slept < milliseconds; slept += 100)
        {
            Sleep(100);
            PollPlacementHotkey();
        }
    }

    // Which build runs and with which settings: the installed ASI and the repo build drifted apart
    // before (2026-09-13), and three INI copies disagreed.
    void LogBuildAndSettings()
    {
        Log("DS2008 Randomizer v%s", kRandomizerVersion);
        char plugin[MAX_PATH]{};
        GetModuleFileNameA(gPlugin, plugin, MAX_PATH);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        const bool known = GetFileAttributesExA(plugin, GetFileExInfoStandard, &info) != 0;
        Log("build %s %s, %s (%lu bytes)", __DATE__, __TIME__, plugin,
            known ? static_cast<unsigned long>(info.nFileSizeLow) : 0ul);
        static char section[32768];
        const DWORD length = GetPrivateProfileSectionA("DeadSpace1Patch", section, sizeof(section), gIniPath);
        unsigned keys = 0;
        for (const char* entry = section; length && *entry; entry += std::strlen(entry) + 1)
        {
            Log("ini %s", entry);
            ++keys;
        }
        Log("ini %s: %u key(s) in [DeadSpace1Patch]", gIniPath, keys);
    }

    DWORD WINAPI Start(LPVOID)
    {
        LoadSettings();
        LogBuildAndSettings();
        gGame = GetModuleHandleA(nullptr);
        char exe[MAX_PATH]{};
        GetModuleFileNameA(gGame, exe, MAX_PATH);
        Log("DeadSpace1Patch attached to %s (base %08X)", exe,
            reinterpret_cast<unsigned>(gGame));
        ExeIsRetail();

        // SecuROM decrypts during startup, so give it a moment before judging.
        for (unsigned attempt = 1; attempt <= 20; ++attempt)
        {
            if (CodeLooksDecrypted(gGame))
            {
                Log("code is readable after %u attempt(s); signature scanning "
                    "is possible from here", attempt);
                InstallCrashCapture();
                InstallMenuLabel();
                InstallGodMode();
                InstallProgressionHooks();
                InstallFirstWeapon();
                InstallStoreGate();
                InstallSuitDebt();
                InstallLimbHooks();
                InstallCrateHooks();
                InstallLogHooks();
                InstallCorpseHooks();
                InstallExploderHooks();
                InstallSuperExploderAudioHook();
                InstallDeathMessageTransplant();
                InstallLootAmountHooks();
                InstallWatchdog();
                InstallScriptedCast();
                InstallPregnantSwarmGuard();
                InstallVariantRuntime();
                InstallSlotListGuard();
                InstallVacuumZoneHooks();
                InstallWorldEditorHooks();
                InstallMovementTeleportHook();
                InstallLevelOrderHook();
                LoadVacuumBoxes();
                InstallAirCheckHook();
                if (gPlayerQueryGuard)
                {
                    gPlayerQueryHelperHook = VerifiedInline(
                        reinterpret_cast<void*>(kPlayerQueryHelper),
                        reinterpret_cast<void*>(&PlayerQueryHelper_Hook));
                    Log("player query guard: %s at %08X",
                        gPlayerQueryHelperHook ? "active" : "NOT installed",
                        static_cast<unsigned>(kPlayerQueryHelper));
                }
                gFuncSpawnCtor = VerifiedMid(
                    reinterpret_cast<void*>(kFuncSpawnAllocated),
                    &FuncSpawnAllocated_Hook);
                gAllocator = VerifiedMid(
                    reinterpret_cast<void*>(kAllocator), &Allocator_Hook);
                Log("allocator watch %s at %08X",
                    gAllocator ? "active" : "NOT installed",
                    static_cast<unsigned>(kAllocator));
                gSpawnHandler = VerifiedInline(
                    reinterpret_cast<void*>(kSpawnHandler),
                    reinterpret_cast<void*>(&SpawnHandler_Hook));
                if (gEnemyRandomizerGlobalStreaming &&
                    gEnemyRandomizerGlobalTemplates)
                    gStreamLoadHook = VerifiedInline(
                        reinterpret_cast<void*>(kStreamManagerLoad),
                        reinterpret_cast<void*>(&StreamLoad_Hook));
                if (gSpawnResolverDiagnostics)
                    gSpawnResolverCall = VerifiedMid(
                        reinterpret_cast<void*>(kSpawnResolverCall), &SpawnResolverCall_Hook);
                if (gSpawnResolverDiagnostics)
                    gSpawnResolver = VerifiedMid(
                        reinterpret_cast<void*>(kSpawnResolverReturn),
                        &SpawnResolverReturn_Hook);
                // The restore handler publishes the pool's targets on a checkpoint reload, so the
                // pool needs it; it used to come only with SpawnResolverDiagnostics.
                if (gSpawnResolverDiagnostics ||
                    (gEnemyRandomizerGlobalTemplates && gEnemyRandomizerGlobalStreaming))
                {
                    gRestoreProbe = VerifiedMid(
                        reinterpret_cast<void*>(kRestoreHandler),
                        &RestoreProbe_Hook);
                    Log("restore handler hook %s at %08X",
                        gRestoreProbe ? "active" : "NOT installed",
                        static_cast<unsigned>(kRestoreHandler));
                }
                if (gSpawnResolverDiagnostics)
                    gResolverFilterResult = VerifiedMid(
                        reinterpret_cast<void*>(kResolverFilterResult),
                        &ResolverFilterResult_Hook);
                if (gSpawnResolverDiagnostics)
                {
                    gResolverPreflightPassed = VerifiedMid(
                        reinterpret_cast<void*>(kResolverPreflightPassed),
                        &ResolverPreflightPassed_Hook);
                    gResolverFiltersPassed = VerifiedMid(
                        reinterpret_cast<void*>(kResolverFiltersPassed),
                        &ResolverFiltersPassed_Hook);
                    gResolverCallbackResult = VerifiedMid(
                        reinterpret_cast<void*>(kResolverCallbackResult),
                        &ResolverCallbackResult_Hook);
                    gResolverFactoryEnter = VerifiedMid(
                        reinterpret_cast<void*>(kResolverFactoryEnter),
                        &ResolverFactoryEnter_Hook);
                    gResolverFactoryResult = VerifiedMid(
                        reinterpret_cast<void*>(kResolverFactoryResult),
                        &ResolverFactoryResult_Hook);
                    gFilterAnimatedReference = VerifiedMid(
                        reinterpret_cast<void*>(kFilterAnimatedReference),
                        &FilterAnimatedReference_Hook);
                    gFilterAnimatedStream = VerifiedMid(
                        reinterpret_cast<void*>(kFilterAnimatedStream),
                        &FilterAnimatedStream_Hook);
                    gFilterAnimatedState = VerifiedMid(
                        reinterpret_cast<void*>(kFilterAnimatedState),
                        &FilterAnimatedState_Hook);
                    gFindTargetKeyProbe = VerifiedMid(
                        reinterpret_cast<void*>(kFindTargetKey),
                        &FindTargetKeyProbe_Hook);
                    Log("key-lookup probe %s at %08X",
                        gFindTargetKeyProbe ? "active" : "NOT installed",
                        static_cast<unsigned>(kFindTargetKey));
                    Log("filter0 probes: reference=%s stream=%s state=%s at "
                        "%08X/%08X/%08X",
                        gFilterAnimatedReference ? "on" : "OFF",
                        gFilterAnimatedStream ? "on" : "OFF",
                        gFilterAnimatedState ? "on" : "OFF",
                        static_cast<unsigned>(kFilterAnimatedReference),
                        static_cast<unsigned>(kFilterAnimatedStream),
                        static_cast<unsigned>(kFilterAnimatedState));
                }
                Log("spawn handler hook %s at %08X, %u extra spawns per trigger",
                    gSpawnHandler ? "active" : "NOT installed",
                    static_cast<unsigned>(kSpawnHandler), gExtraSpawns);
                if (gEnemyRandomizerGlobalStreaming &&
                    gEnemyRandomizerGlobalTemplates)
                    Log("early global-stream hook %s at %08X",
                        gStreamLoadHook ? "active" : "NOT installed",
                        static_cast<unsigned>(kStreamManagerLoad));
                if (gSpawnResolverDiagnostics)
                    Log("resolver call recorder %s at %08X",
                        gSpawnResolverCall ? "active" : "NOT installed",
                        static_cast<unsigned>(kSpawnResolverCall));
                if (gSpawnResolverDiagnostics)
                    Log("resolver return recorder %s at %08X",
                        gSpawnResolver ? "active" : "NOT installed",
                        static_cast<unsigned>(kSpawnResolverReturn));
                if (gSpawnResolverDiagnostics)
                    Log("resolver branch probes: preflight=%s filter=%s passed=%s "
                        "callback=%s factory=%s result=%s",
                        gResolverPreflightPassed ? "on" : "OFF",
                        gResolverFilterResult ? "on" : "OFF",
                        gResolverFiltersPassed ? "on" : "OFF",
                        gResolverCallbackResult ? "on" : "OFF",
                        gResolverFactoryEnter ? "on" : "OFF",
                        gResolverFactoryResult ? "on" : "OFF");
                Log("FuncSpawn probe %s at %08X",
                    gFuncSpawnCtor ? "active" : "NOT installed",
                    static_cast<unsigned>(kFuncSpawnAllocated));
                if (gCheckpointChanger)
                {
                    if (HANDLE thread = CreateThread(nullptr, 0,
                            &CheckpointHotkeyThread, nullptr, 0, nullptr))
                        CloseHandle(thread);
                    else
                        Log("checkpoint changer: hotkey thread creation failed");
                }
                Log("hook sites: %ld verified, %ld REFUSED, %ld computed at run time (unlisted)",
                    InterlockedCompareExchange(&gHooksVerified, 0, 0),
                    InterlockedCompareExchange(&gHooksRefused, 0, 0),
                    InterlockedCompareExchange(&gHooksUnlisted, 0, 0));

                // Wait for a level rather than for the clock. Spawners are
                // built while a level streams in, so the moment their count
                // stops growing is the moment their fields are filled in.
                long previous = 0;
                bool reported = false;
                for (;;)
                {
                    SleepPollingHotkey(1000);
                    // Do this every pass, not just once a level: areas stream
                    // in and out constantly and every new spawner starts
                    // without the bit.
                    // The table is built during start-up, well after this
                    // plugin loads, so it is looked for until it appears
                    // rather than once at the beginning.
                    static unsigned versuche = 0;
                    if (!gClassCount && versuche < 120)
                    {
                        ++versuche;
                        if (const uintptr_t table = FindClassTable())
                        {
                            Log("class table found at %08X",
                                static_cast<unsigned>(table));
                            LoadClassTable(table);
                            HookEnemyFactories(table);
                            LoadEnemyPercents();
                        }
                    }
                    // EnableRetry() is deliberately not called any more. It
                    // set the retry option bits on every spawner in the level,
                    // including the ones that place crew members, and that is
                    // a capability the level designers left switched off. With
                    // our own multiplication doing nothing at all (the log
                    // read "0 multiplied"), a second Kendra still turned up,
                    // which leaves those bits as the only thing we were still
                    // changing. The measurement that mattered came earlier:
                    // with both bits set on 101 spawners, not one retry was
                    // ever queued, so they were never buying us anything.
                    ReportTotals();
                    PollFirstWeaponDelivery();
                    static unsigned crashFilterPasses = 0;
                    if (++crashFilterPasses % 10 == 0)
                        KeepCrashFilter();
                    const long now = gSpawners;
                    if (now == 0)
                        continue;
                    if (now != previous)
                    {
                        previous = now;
                        reported = false;       // a new level is loading
                        continue;
                    }
                    if (reported)
                        continue;
                    reported = true;
                    // Skip the holes the ring buffer leaves behind, and
                    // anything whose area has already been unloaded.
                    uintptr_t lebend[6]{};
                    unsigned show = 0;
                    const unsigned seenCount = SeenSpawnerSnapshotCount();
                    for (unsigned i = 0; i < seenCount && show < 6; ++i)
                        if (StillASpawner(gSeen[i]))
                            lebend[show++] = gSeen[i];
                    Log("--- %ld spawners in this level, retry enabled on %ld, "
                        "showing %u ---", now, gPatched, show);
                    ReportTotals();
                    for (unsigned i = 0; i < show; ++i)
                        ReportSpawnerIdentity(lebend[i]);
                    if (show)
                        DumpSpawner(lebend[0]);
                }
            }
            Sleep(500);
        }
        Log("code still looks encrypted after 10 seconds. Scanning from this "
            "point would find nothing.");
        return 0;
    }
}

BOOL APIENTRY DllMain(HMODULE self, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    DisableThreadLibraryCalls(self);
    gPlugin = self;

    GetModuleFileNameA(GetModuleHandleA(nullptr), gLogPath, MAX_PATH);
    strncpy_s(gGameRoot, sizeof(gGameRoot), gLogPath, _TRUNCATE);
    if (char* rootSlash = std::strrchr(gGameRoot, '\\'))
        rootSlash[1] = '\0';
    if (char* slash = std::strrchr(gLogPath, '\\'))
        slash[1] = '\0';
    std::strncat(gLogPath, "DeadSpace1Patch.log", MAX_PATH - 1);
    // The previous run's log stays as DeadSpace1Patch.prev.log, so a crash log survives a restart.
    char previousLog[MAX_PATH]{};
    strncpy_s(previousLog, sizeof(previousLog), gLogPath, _TRUNCATE);
    if (char* dot = std::strrchr(previousLog, '.'))
        strcpy_s(dot, sizeof(previousLog) - (dot - previousLog), ".prev.log");
    if (!MoveFileExA(gLogPath, previousLog, MOVEFILE_REPLACE_EXISTING))
        DeleteFileA(gLogPath);

    GetModuleFileNameA(GetModuleHandleA(nullptr), gSpawnCatalogPath, MAX_PATH);
    if (char* slash = std::strrchr(gSpawnCatalogPath, '\\'))
        slash[1] = '\0';
    std::strncat(gSpawnCatalogPath, "DeadSpace1Patch_SpawnCatalog.tsv", MAX_PATH - 1);
    const HANDLE catalog = CreateFileA(gSpawnCatalogPath, GENERIC_WRITE, FILE_SHARE_READ,
                                       nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (catalog != INVALID_HANDLE_VALUE)
    {
        if (GetFileSize(catalog, nullptr) == 0)
        {
            static constexpr char kHeader[] = "source_uid\tauthored_target_uid\tactor_class\r\n";
            DWORD written = 0;
            WriteFile(catalog, kHeader, sizeof(kHeader) - 1, &written, nullptr);
        }
        CloseHandle(catalog);
    }

    GetModuleFileNameA(GetModuleHandleA(nullptr), gIniPath, MAX_PATH);
    if (char* cut = std::strrchr(gIniPath, '\\'))
        cut[1] = '\0';
    std::strncat(gIniPath, "DeadSpace1Patch.ini", MAX_PATH - 1);

    CreateThread(nullptr, 0, &Start, nullptr, 0, nullptr);
    return TRUE;
}

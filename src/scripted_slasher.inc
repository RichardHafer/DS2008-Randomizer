
namespace scripted_skin {
constexpr Guid128 actors[] = {
#include "scripted_slasher_ids.inc"
};
constexpr Guid128 skins[] = {
    {{0x99A9EDC1u,0x99A9EDC1u,0x99A9EDC1u,0x99A9EDC1u}},
    {{0x3925D0E1u,0x3925D0E1u,0x3925D0E1u,0x3925D0E1u}},
    {{0x64A3526Bu,0x64A3526Bu,0x64A3526Bu,0x64A3526Bu}},
    {{0x84EF33A1u,0x84EF33A1u,0x84EF33A1u,0x84EF33A1u}}
};
bool protectedActor(const Guid128& id) {
    for (const auto& actor : actors)
        if (SameGuid(id, actor)) return true;
    return false;
}
volatile LONG ready;
wchar_t root[MAX_PATH]{}, privatePaths[2][MAX_PATH]{};
constexpr const wchar_t* stock[] = {
    L"global_assets\\global_assets\\ds_assets\\char_str\\npc\\zomB_cct\\zombie.str",
    L"global_assets\\global_assets\\ds_assets\\char_str\\npc\\zomB_cct\\zombievar1.str"
};
using OpenA = decltype(&CreateFileA);
using OpenW = decltype(&CreateFileW);
OpenA originalA;
OpenW originalW;
safetyhook::MidHook baseModel, npcModel;
const wchar_t* replacement(const wchar_t* path, DWORD access, DWORD mode) {
    if (!ready || !path || mode != OPEN_EXISTING ||
        (access & ~(GENERIC_READ | FILE_READ_ATTRIBUTES | SYNCHRONIZE))) return nullptr;
    const wchar_t* ext = wcsrchr(path, L'.');
    if (!ext || _wcsicmp(ext, L".str")) return nullptr;
    wchar_t full[MAX_PATH];
    DWORD n = GetFullPathNameW(path, MAX_PATH, full, nullptr);
    size_t prefix = wcslen(root);
    if (!n || n >= MAX_PATH || _wcsnicmp(root, full, prefix)) return nullptr;
    for (unsigned i = 0; i < 2; ++i)
        if (!_wcsicmp(full + prefix, stock[i])) return privatePaths[i];
    return nullptr;
}

HANDLE WINAPI openW(LPCWSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD m, DWORD f, HANDLE t) {
    auto other = replacement(p, a, m);
    return originalW(other ? other : p, a, s, sa, m, f, t);
}
HANDLE WINAPI openA(LPCSTR p, DWORD a, DWORD s, LPSECURITY_ATTRIBUTES sa, DWORD m, DWORD f, HANDLE t) {
    wchar_t wide[MAX_PATH];
    UINT cp = AreFileApisANSI() ? CP_ACP : CP_OEMCP;
    if (p && MultiByteToWideChar(cp, 0, p, -1, wide, MAX_PATH)) {
        if (auto other = replacement(wide, a, m)) {
            char redirected[MAX_PATH * 4];
            wchar_t roundTrip[MAX_PATH];
            if (WideCharToMultiByte(cp, 0, other, -1, redirected, sizeof(redirected), nullptr, nullptr) &&
                MultiByteToWideChar(cp, 0, redirected, -1, roundTrip, MAX_PATH) && !wcscmp(other, roundTrip))
                return originalA(redirected, a, s, sa, m, f, t);
            return originalW(other, a, s, sa, m, f, t);
        }
    }
    return originalA(p, a, s, sa, m, f, t);
}

bool namedFileImport(BYTE* base, IMAGE_NT_HEADERS* nt, DWORD slot, const char* name) {
    const auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    auto imports = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (size_t i = 0; i < dir.Size / sizeof(*imports) && imports[i].Name; ++i) {
        const auto& imp = imports[i];
        if (!imp.OriginalFirstThunk || _stricmp(reinterpret_cast<char*>(base + imp.Name), "kernel32.dll"))
            continue;
        auto names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + imp.OriginalFirstThunk);
        for (DWORD j = 0; names[j].u1.AddressOfData; ++j) {
            if (imp.FirstThunk + j * sizeof(*names) != slot || IMAGE_SNAP_BY_ORDINAL(names[j].u1.Ordinal))
                continue;
            auto entry = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[j].u1.AddressOfData);
            return strcmp(entry->Name, name) == 0;
        }
    }
    return false;
}

bool executableHandler(const void* target) {
    MEMORY_BASIC_INFORMATION mem{};
    constexpr DWORD executable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return VirtualQuery(target, &mem, sizeof(mem)) && mem.State == MEM_COMMIT &&
           (mem.Protect & executable) && !(mem.Protect & PAGE_GUARD);
}


uintptr_t choose(uintptr_t object, uintptr_t model) {
    if (!ready || !gEnemyRandomizer || !gEnemyRandomizerScriptedVariants || !object || !model)
        return model;
    const auto& id = *reinterpret_cast<const Guid128*>(object + 0x20);
    if (!protectedActor(id)) return model;
    const unsigned slot = ActorSlotNamed("Zombie");
    if (slot >= gActorCount || (gEnemyRandomizerSourceFlagsLoaded && !gEnemyRandomizerSourceEnabled[slot]) ||
        KeepChanceKeeps("Zombie", id, id))
        return model;
    const auto& authored = *reinterpret_cast<const Guid128*>(model);
    if (!SameGuid(authored, skins[0]) && !SameGuid(authored, skins[1])) return model;
    if (StableRandomizerHash(id,id,0x43484e43u) % 100 >= gEnemyRandomizerChance) return model;
    const unsigned pick = StableRandomizerHash(id,id,0x534b494eu) % _countof(skins);
    static volatile LONG lines;
    if (gEnemyRandomizerDiagnostics && InterlockedIncrement(&lines) <= 64)
        Log("Scene Slasher %08X-%08X-%08X-%08X seed=%08X model=%08X -> %08X",id.value[0],id.value[1],
            id.value[2],id.value[3],gEnemyRandomizerSeed,authored.value[0],skins[pick].value[0]);
    return reinterpret_cast<uintptr_t>(&skins[pick]);
}
void install() {
    if (!gEnemyRandomizer || !gEnemyRandomizerScriptedVariants || !ExeIsRetail()) return;
    auto base=reinterpret_cast<BYTE*>(gGame);
    if (!GetModuleFileNameW(nullptr,root,MAX_PATH) || !wcsrchr(root,L'\\')) return;
    wcsrchr(root,L'\\')[1]=0;
    const wchar_t* files[]={L"scene_skin_zombie.str",L"scene_skin_zombievar1.str"};
    const DWORD sizes[]={25427968,25821184};
    for (unsigned i=0;i<2;++i) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (swprintf_s(privatePaths[i],L"%lsds1_randomizer_assets\\%ls",root,files[i])<0 ||
            !GetFileAttributesExW(privatePaths[i],GetFileExInfoStandard,&data) ||
            data.nFileSizeHigh || data.nFileSizeLow!=sizes[i]) {
            Log("Scene Slasher skins disabled: missing or incompatible pool archive %ls",files[i]); return;
        }
    }
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    auto a=reinterpret_cast<OpenA*>(base+0x7c9180);
    auto w=reinterpret_cast<OpenW*>(base+0x7c911c);
    if (!namedFileImport(base,nt,0x7c9180,"CreateFileA") || !namedFileImport(base,nt,0x7c911c,"CreateFileW") ||
        memcmp(base+0x32df48,"\x8b\x3d\x50\x43\xf4\x00",6) ||
        memcmp(base+0x75446,"\x8b\x08\x89\x8f\x04\x05\x00\x00",8)) {
        Log("Scene Slasher skins disabled: retail signature mismatch"); return;
    }
    originalA=*a; originalW=*w;
    if (!executableHandler(originalA) || !executableHandler(originalW)) return;
    baseModel=safetyhook::create_mid(base+0x32df48,[](safetyhook::Context& c){ c.esi=choose(c.ebp,c.esi); });
    npcModel=safetyhook::create_mid(base+0x75446,[](safetyhook::Context& c){ c.eax=choose(c.edi,c.eax); });
    DWORD old,unused;
    if (!baseModel || !npcModel || !VirtualProtect(w,0x68,PAGE_READWRITE,&old)) {
        baseModel.reset(); npcModel.reset(); Log("Scene Slasher skins disabled: hooks unavailable"); return;
    }
    auto sa=reinterpret_cast<void* volatile*>(a), sw=reinterpret_cast<void* volatile*>(w);
    const bool first=InterlockedCompareExchangePointer(sa,openA,originalA)==originalA;
    const bool both=first && InterlockedCompareExchangePointer(sw,openW,originalW)==originalW;
    if (!both && first) InterlockedCompareExchangePointer(sa,originalA,openA);
    VirtualProtect(w,0x68,old,&unused);
    if (!both) { baseModel.reset(); npcModel.reset(); Log("Scene Slasher skins disabled: import changed"); return; }
    InterlockedExchange(&ready,1);
    Log("Scene Slasher skins active: %u actors, four retail models",unsigned(_countof(actors)));
}
}

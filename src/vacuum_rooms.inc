    // Vacuum rooms ([DeadSpace2008Randomizer] VacuumZones, VacuumRoomRandomizer,
    // VacuumRecorder; [VacuumZones] in the INI, DeadSpace2008Randomizer_VacuumRooms.txt).
    //
    // A room is a zone stream (ch10_crw/res_sleepblockb_z01, ...) and each carries
    // exactly one ZoneRender (zone_rooms.py, 360 in 12 chapters). Being loaded is
    // not enough, since up to six rooms stay loaded at once (ch01 hangar, lounge,
    // Kellion, lift). The ZoneRender byte +0x1EC says whether the room is shown; its
    // message handler sets it to 1 and 0 (0x007326C4, 0x007326FD) when the level
    // shows or hides the room, as a door into it opens or closes. A room seen far
    // away through open doors is shown too, so Isaac must also stand inside one of
    // its zone spheres. Measured live: in the lounge only lounge_z01/z02 were 1, in
    // the hangar hangar_z01/z04 and the Kellion. A vacuum room takes the air while
    // it is shown.
    //
    // Rooms come from two lists: [VacuumZones] (always airless while VacuumZones=1)
    // and the recorder file, from which the seed picks VacuumRoomsPerChapter per
    // chapter while VacuumRoomRandomizer=1. The recorder key (Home) appends the room
    // Isaac stands in to that file: the shown room with the smallest ZoneRender
    // sphere around him (+0x1D0 centre, +0x1DC radius; hangar_z04's radius of 1094
    // holds the space outside). A newly recorded room is airless at once, so the
    // recording can be checked right away.
    //
    // The ZoneRender is looked up by GUID in the SimManager key table
    // (TargetKeyFor, object at key+0x14, vtable 0x00C842F0).
    struct ZoneRoom
    {
        Guid128 zone;
        uint32_t chapter;
        const char* stream;
    };
    constexpr ZoneRoom kZoneRooms[] = {
#include "zone_rooms_generated.inc"
    };
    constexpr unsigned kZoneRoomCount = sizeof(kZoneRooms) / sizeof(kZoneRooms[0]);
    constexpr uint32_t kZoneRenderVtable = 0x00C842F0;
    constexpr unsigned kZoneRenderSphere = 0x1D0;
    constexpr unsigned kZoneRenderShown = 0x1EC;
    constexpr unsigned kMaxVacuumRooms = 256;

    struct VacuumRoom
    {
        Guid128 zone;
        char name[64];
        const char* source;   // "[VacuumZones]", "random" or "recorded"
        bool shown;           // as last seen by the air check
        // Every zone of the same room: lounge_z01 and lounge_z02 are one lounge, and a door on
        // the z02 side shows z02 and hides z01 (the air came back as that door opened).
        unsigned zones[12];
        unsigned zoneCount;
    };

    bool gVacuumZones = false;
    bool gVacuumZoneDiagnostics = false;
    bool gVacuumRoomRandomizer = false;
    unsigned gVacuumRoomsPerChapter = 1;
    bool gVacuumRecorder = false;
    int gVacuumRecorderHotkey = 0x24;   // Home
    char gVacuumRoomFile[MAX_PATH]{};

    // The rooms the air check watches: [VacuumZones] plus the seed's picks.
    VacuumRoom gVacuumRooms[kMaxVacuumRooms]{};
    unsigned gVacuumRoomCount = 0;
    unsigned gVacuumZoneRuleCount = 0;   // the [VacuumZones] part, for the log
    // Everything recorded so far, so a second press in a room is not written twice.
    Guid128 gRecordedRooms[kMaxVacuumRooms]{};
    unsigned gRecordedRoomCount = 0;
    // Recorded since the game started; they stay airless through INI reloads.
    Guid128 gSessionRooms[kMaxVacuumRooms]{};
    unsigned gSessionRoomCount = 0;
    SRWLOCK gVacuumRoomLock = SRWLOCK_INIT;

    bool PlayerTransform(float* out12);
    uintptr_t TargetKeyFor(const Guid128& target);

    const ZoneRoom* ZoneRoomFor(const Guid128& zone)
    {
        for (const ZoneRoom& room : kZoneRooms)
            if (SameGuid(room.zone, zone))
                return &room;
        return nullptr;
    }

    // The live ZoneRender of a room, or 0 while its stream is not loaded.
    uintptr_t LiveZoneRender(const Guid128& zone)
    {
        const uintptr_t key = TargetKeyFor(zone);
        uintptr_t object = 0;
        uint32_t vtable = 0;
        if (!key ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(key + 0x14), &object, sizeof(object),
                               nullptr) ||
            !object ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(object), &vtable, sizeof(vtable),
                               nullptr) ||
            vtable != kZoneRenderVtable)
            return 0;
        return object;
    }

    bool ZoneShown(uintptr_t zone)
    {
        uint8_t shown = 0;
        return zone &&
               ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(zone + kZoneRenderShown), &shown,
                                 sizeof(shown), nullptr) &&
               shown == 1;
    }

    bool ZoneSphere(uintptr_t zone, float* sphere)
    {
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(zone + kZoneRenderSphere), sphere,
                                 sizeof(float) * 4, nullptr) != 0;
    }

    // "ch01_flt/lounge_z02" -> length of "ch01_flt/lounge"; the whole name when there is no _zNN.
    size_t RoomBaseLength(const char* stream)
    {
        const size_t length = std::strlen(stream);
        size_t cut = length;
        while (cut > 0 && stream[cut - 1] >= '0' && stream[cut - 1] <= '9')
            --cut;
        if (cut < length && cut >= 2 && (stream[cut - 1] == 'z' || stream[cut - 1] == 'Z') && stream[cut - 2] == '_')
            return cut - 2;
        return length;
    }

    void FindRoomZones(VacuumRoom& room)
    {
        room.zoneCount = 0;
        const ZoneRoom* known = ZoneRoomFor(room.zone);
        if (!known)
            return;
        const size_t base = RoomBaseLength(known->stream);
        for (unsigned i = 0; i < kZoneRoomCount && room.zoneCount < 12; ++i)
            if (kZoneRooms[i].chapter == known->chapter && RoomBaseLength(kZoneRooms[i].stream) == base &&
                _strnicmp(kZoneRooms[i].stream, known->stream, base) == 0)
                room.zones[room.zoneCount++] = i;
    }

    // Two zones of one room (lounge_z01, lounge_z02) count as the same room.
    bool SameRoom(const Guid128& a, const Guid128& b)
    {
        if (SameGuid(a, b))
            return true;
        const ZoneRoom* ra = ZoneRoomFor(a);
        const ZoneRoom* rb = ZoneRoomFor(b);
        const size_t base = ra ? RoomBaseLength(ra->stream) : 0;
        return ra && rb && ra->chapter == rb->chapter && base == RoomBaseLength(rb->stream) &&
               _strnicmp(ra->stream, rb->stream, base) == 0;
    }

    void AddVacuumRoomLocked(const Guid128& zone, const char* name, const char* source)
    {
        for (unsigned i = 0; i < gVacuumRoomCount; ++i)
            if (SameGuid(gVacuumRooms[i].zone, zone))
                return;
        if (gVacuumRoomCount >= kMaxVacuumRooms)
            return;
        VacuumRoom& room = gVacuumRooms[gVacuumRoomCount++];
        room = VacuumRoom{};
        room.zone = zone;
        room.source = source;
        const ZoneRoom* known = ZoneRoomFor(zone);
        std::snprintf(room.name, sizeof(room.name), "%s", name && *name ? name : known ? known->stream : "unknown");
        FindRoomZones(room);
    }

    // The recorder file: one room per line, its ZoneRender GUID first; the rest is a comment.
    unsigned ReadRecordedRooms(Guid128* out, unsigned capacity)
    {
        FILE* file = nullptr;
        if (fopen_s(&file, gVacuumRoomFile, "r") || !file)
            return 0;
        unsigned count = 0;
        char line[512];
        while (count < capacity && std::fgets(line, sizeof(line), file))
        {
            const char* text = line;
            while (*text == ' ' || *text == '\t')
                ++text;
            Guid128 zone{};
            if (*text == ';' || *text == '#' || !ParseGuidText(text, &zone))
                continue;
            bool known = false;
            for (unsigned i = 0; i < count && !known; ++i)
                known = SameGuid(out[i], zone);
            if (!known)
                out[count++] = zone;
        }
        std::fclose(file);
        return count;
    }

    // Per chapter, VacuumRoomsPerChapter of its recorded rooms, drawn from the seed.
    void PickVacuumRoomsLocked(const Guid128* recorded, unsigned recordedCount)
    {
        for (uint32_t chapter = 0; chapter <= 15; ++chapter)
        {
            unsigned members[kMaxVacuumRooms]{};
            unsigned count = 0;
            for (unsigned i = 0; i < recordedCount; ++i)
            {
                const ZoneRoom* room = ZoneRoomFor(recorded[i]);
                if ((room ? room->chapter : 0u) == chapter)
                    members[count++] = i;
            }
            if (!count)
                continue;
            uint32_t state = (gStartingSuitSeed ^ 0x56414355u) + chapter * 0x9E3779B9u;
            if (!state)
                state = 1;
            for (unsigned i = count; i > 1; --i)
            {
                state ^= state << 13;
                state ^= state >> 17;
                state ^= state << 5;
                const unsigned j = state % i;
                const unsigned swap = members[i - 1];
                members[i - 1] = members[j];
                members[j] = swap;
            }
            const unsigned take = count < gVacuumRoomsPerChapter ? count : gVacuumRoomsPerChapter;
            for (unsigned i = 0; i < take; ++i)
            {
                AddVacuumRoomLocked(recorded[members[i]], nullptr, "random");
                const VacuumRoom& picked = gVacuumRooms[gVacuumRoomCount - 1];
                Log("vacuum room picked for chapter %u: %s (%08X-%08X), %u of %u recorded", chapter, picked.name,
                    picked.zone.value[0], picked.zone.value[1], i + 1, count);
            }
        }
    }

    void LoadVacuumRoomSettings()
    {
        gVacuumZones = GetPrivateProfileIntA("DeadSpace2008Randomizer", "VacuumZones", 0, gIniPath) != 0;
        gVacuumZoneDiagnostics = GetPrivateProfileIntA("DeadSpace2008Randomizer", "VacuumZoneDiagnostics", 0, gIniPath) != 0;
        gVacuumRoomRandomizer = GetPrivateProfileIntA("DeadSpace2008Randomizer", "VacuumRoomRandomizer", 0, gIniPath) != 0;
        gVacuumRoomsPerChapter = static_cast<unsigned>(
            GetPrivateProfileIntA("DeadSpace2008Randomizer", "VacuumRoomsPerChapter", 1, gIniPath));
        if (gVacuumRoomsPerChapter > 16)
            gVacuumRoomsPerChapter = 16;
        gVacuumRecorder = GetPrivateProfileIntA("DeadSpace2008Randomizer", "VacuumRecorder", 0, gIniPath) != 0;
        gVacuumRecorderHotkey = GetPrivateProfileIntA("DeadSpace2008Randomizer", "VacuumRecorderHotkey", 0x24, gIniPath);
        std::snprintf(gVacuumRoomFile, sizeof(gVacuumRoomFile), "%s", gIniPath);
        char* slash = std::strrchr(gVacuumRoomFile, '\\');
        std::snprintf(slash ? slash + 1 : gVacuumRoomFile,
                      sizeof(gVacuumRoomFile) - (slash ? static_cast<size_t>(slash + 1 - gVacuumRoomFile) : 0),
                      "DeadSpace2008Randomizer_VacuumRooms.txt");

        static Guid128 recorded[kMaxVacuumRooms];
        const unsigned recordedCount = ReadRecordedRooms(recorded, kMaxVacuumRooms);

        AcquireSRWLockExclusive(&gVacuumRoomLock);
        gVacuumRoomCount = 0;
        gVacuumZoneRuleCount = 0;
        gRecordedRoomCount = recordedCount;
        for (unsigned i = 0; i < recordedCount; ++i)
            gRecordedRooms[i] = recorded[i];
        if (gVacuumZones)
        {
            char section[8192]{};
            const DWORD used = GetPrivateProfileSectionA("VacuumZones", section, sizeof(section) - 2, gIniPath);
            for (const char* row = section; used && *row; row += std::strlen(row) + 1)
            {
                const char* equals = std::strchr(row, '=');
                Guid128 zone{};
                if (!equals || equals == row || !ParseGuidText(equals + 1, &zone))
                {
                    Log("vacuum zone rule ignored: %s", row);
                    continue;
                }
                char name[64]{};
                std::snprintf(name, sizeof(name), "%.*s", static_cast<int>(equals - row), row);
                AddVacuumRoomLocked(zone, name, "[VacuumZones]");
                ++gVacuumZoneRuleCount;
            }
        }
        if (gVacuumRoomRandomizer)
            PickVacuumRoomsLocked(recorded, recordedCount);
        for (unsigned i = 0; i < gSessionRoomCount; ++i)
            AddVacuumRoomLocked(gSessionRooms[i], nullptr, "recorded");
        const unsigned watched = gVacuumRoomCount;
        ReleaseSRWLockExclusive(&gVacuumRoomLock);

        Log("vacuum rooms: [VacuumZones] %s with %u room(s), random rooms %s (%u per chapter from %u recorded in %s), "
            "%u watched; recorder %s (key VK=0x%02X)", gVacuumZones ? "on" : "off", gVacuumZoneRuleCount,
            gVacuumRoomRandomizer ? "on" : "off", gVacuumRoomsPerChapter, recordedCount, gVacuumRoomFile, watched,
            gVacuumRecorder ? "on" : "off", static_cast<unsigned>(gVacuumRecorderHotkey));
    }

    bool VacuumRoomsWanted()
    {
        return gVacuumZones || gVacuumRoomRandomizer || gVacuumRecorder;
    }

    // From the per-frame air check: is a watched room shown? Checked every 100 ms; every change is logged.
    bool VacuumRoomLoaded()
    {
        static ULONGLONG lastCheck = 0;
        static bool cached = false;
        const ULONGLONG now = GetTickCount64();
        if (now - lastCheck < 100)
            return cached;
        lastCheck = now;
        bool any = false;
        float me[12]{};
        const bool havePlayer = PlayerTransform(me);
        AcquireSRWLockExclusive(&gVacuumRoomLock);
        for (unsigned i = 0; i < gVacuumRoomCount; ++i)
        {
            VacuumRoom& room = gVacuumRooms[i];
            // Shown alone also covers a room seen far away through open doors (hallB from the
            // tram station), so Isaac must also be inside one of its zone spheres.
            bool shown = false;
            const char* through = room.name;
            const unsigned zoneTotal = room.zoneCount ? room.zoneCount : 1;
            for (unsigned z = 0; z < zoneTotal && !shown; ++z)
            {
                const ZoneRoom* zoneRoom = room.zoneCount ? &kZoneRooms[room.zones[z]] : nullptr;
                const uintptr_t zone = LiveZoneRender(zoneRoom ? zoneRoom->zone : room.zone);
                float sphere[4]{};
                if (!ZoneShown(zone) || !ZoneSphere(zone, sphere) || !havePlayer)
                    continue;
                const double dx = me[9] - sphere[0], dy = me[10] - sphere[1], dz = me[11] - sphere[2];
                if (sphere[3] > 0.0f && std::sqrt(dx * dx + dy * dy + dz * dz) <= sphere[3])
                {
                    shown = true;
                    through = zoneRoom ? zoneRoom->stream : room.name;
                }
            }
            if (shown != room.shown)
            {
                Log("vacuum room %s %s (%s, %u zone(s), through %s): %s, Isaac at %.1f %.1f %.1f", room.name,
                    shown ? "around Isaac and shown" : "left, hidden or unloaded", room.source, room.zoneCount,
                    through, shown ? "no air from here on" : "air is back unless another vacuum room holds him",
                    havePlayer ? me[9] : 0.0f, havePlayer ? me[10] : 0.0f, havePlayer ? me[11] : 0.0f);
                room.shown = shown;
            }
            any = any || shown;
        }
        ReleaseSRWLockExclusive(&gVacuumRoomLock);
        cached = any;
        return any;
    }

    // The shown room around Isaac: the smallest ZoneRender sphere that holds him,
    // else the nearest centre. Every loaded room is logged.
    const ZoneRoom* CurrentZoneRoom(const float* me, float* radiusOut, bool log)
    {
        const ZoneRoom* best = nullptr;
        float bestRadius = 0.0f;
        const ZoneRoom* nearest = nullptr;
        double nearestDistance = 0.0;
        unsigned loaded = 0, shownCount = 0;
        for (const ZoneRoom& room : kZoneRooms)
        {
            const uintptr_t zone = LiveZoneRender(room.zone);
            float sphere[4]{};
            if (!zone || !ZoneSphere(zone, sphere))
                continue;
            ++loaded;
            const bool shown = ZoneShown(zone);
            shownCount += shown;
            const double dx = me[9] - sphere[0], dy = me[10] - sphere[1], dz = me[11] - sphere[2];
            const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
            const bool inside = sphere[3] > 0.0f && distance <= sphere[3];
            if (log)
                Log("   loaded room ch%02u %s: %s, centre %.1f %.1f %.1f, radius %.1f, Isaac %.1f away%s", room.chapter,
                    room.stream, shown ? "shown" : "hidden", sphere[0], sphere[1], sphere[2], sphere[3], distance,
                    inside ? ", inside" : "");
            if (!shown)
                continue;
            if (inside && (!best || sphere[3] < bestRadius))
            {
                best = &room;
                bestRadius = sphere[3];
            }
            if (!nearest || distance < nearestDistance)
            {
                nearest = &room;
                nearestDistance = distance;
            }
        }
        if (log)
            Log("   %u room(s) loaded, %u shown", loaded, shownCount);
        if (radiusOut)
            *radiusOut = best ? bestRadius : -1.0f;
        return best ? best : nearest;
    }

    void RecordVacuumRoom()
    {
        float me[12]{};
        if (!PlayerTransform(me))
        {
            Log("vacuum recorder: no player yet");
            MessageBeep(MB_ICONHAND);
            return;
        }
        Log("vacuum recorder: Isaac at %.2f %.2f %.2f", me[9], me[10], me[11]);
        float radius = 0.0f;
        const ZoneRoom* room = CurrentZoneRoom(me, &radius, true);
        if (!room)
        {
            Log("vacuum recorder: no room is shown here, nothing written");
            MessageBeep(MB_ICONHAND);
            return;
        }
        AcquireSRWLockExclusive(&gVacuumRoomLock);
        bool known = false;
        Guid128 recordedZone = room->zone;   // the zone the file names, maybe another zone of this room
        for (unsigned i = 0; i < gRecordedRoomCount && !known; ++i)
            if (SameRoom(gRecordedRooms[i], room->zone))
            {
                // A second press takes the room out again.
                known = true;
                recordedZone = gRecordedRooms[i];
                gRecordedRooms[i] = gRecordedRooms[--gRecordedRoomCount];
            }
        if (known)
        {
            for (unsigned i = 0; i < gSessionRoomCount; ++i)
                if (SameRoom(gSessionRooms[i], room->zone))
                {
                    gSessionRooms[i] = gSessionRooms[--gSessionRoomCount];
                    break;
                }
            for (unsigned i = 0; i < gVacuumRoomCount; ++i)
                if (SameRoom(gVacuumRooms[i].zone, room->zone) && std::strcmp(gVacuumRooms[i].source, "recorded") == 0)
                {
                    gVacuumRooms[i] = gVacuumRooms[--gVacuumRoomCount];
                    break;
                }
        }
        else
        {
            if (gRecordedRoomCount < kMaxVacuumRooms)
                gRecordedRooms[gRecordedRoomCount++] = room->zone;
            // Airless from now on, so the choice can be felt right away.
            if (gSessionRoomCount < kMaxVacuumRooms)
                gSessionRooms[gSessionRoomCount++] = room->zone;
            AddVacuumRoomLocked(room->zone, nullptr, "recorded");
        }
        const unsigned total = gRecordedRoomCount;
        ReleaseSRWLockExclusive(&gVacuumRoomLock);
        if (known)
        {
            // Keep every line but the ones that name this room.
            char guid[40]{};
            std::snprintf(guid, sizeof(guid), "%08X-%08X-%08X-%08X", recordedZone.value[0], recordedZone.value[1],
                          recordedZone.value[2], recordedZone.value[3]);
            std::string kept;
            unsigned dropped = 0;
            FILE* in = nullptr;
            if (!fopen_s(&in, gVacuumRoomFile, "r") && in)
            {
                char line[512];
                while (std::fgets(line, sizeof(line), in))
                {
                    const char* text = line;
                    while (*text == ' ' || *text == '\t')
                        ++text;
                    if (_strnicmp(text, guid, std::strlen(guid)) == 0)
                        ++dropped;
                    else
                        kept += line;
                }
                std::fclose(in);
            }
            FILE* out = nullptr;
            const bool written = !fopen_s(&out, gVacuumRoomFile, "w") && out;
            if (written)
            {
                std::fputs(kept.c_str(), out);
                std::fclose(out);
            }
            Log("vacuum recorder: ch%02u %s removed again (%u line(s) dropped, %u left in the file%s); it has air again",
                room->chapter, room->stream, dropped, total, written ? "" : ", the file could NOT be rewritten");
            MessageBeep(written ? MB_ICONASTERISK : MB_ICONHAND);
            return;
        }
        FILE* file = nullptr;
        if (fopen_s(&file, gVacuumRoomFile, "a") || !file)
        {
            Log("vacuum recorder: could not open %s", gVacuumRoomFile);
            MessageBeep(MB_ICONHAND);
            return;
        }
        SYSTEMTIME stamp{};
        GetLocalTime(&stamp);
        std::fprintf(file, "%08X-%08X-%08X-%08X ; ch%02u %s, %s, at %.1f %.1f %.1f, %04u-%02u-%02u %02u:%02u\n",
                     room->zone.value[0], room->zone.value[1], room->zone.value[2], room->zone.value[3], room->chapter,
                     room->stream, radius >= 0.0f ? "inside its zone" : "nearest zone, Isaac outside every sphere",
                     me[9], me[10], me[11], stamp.wYear, stamp.wMonth, stamp.wDay, stamp.wHour, stamp.wMinute);
        std::fclose(file);
        Log("vacuum recorder: ch%02u %s recorded (%u in the file), airless now; the pick takes it from the next start",
            room->chapter, room->stream, total);
        MessageBeep(MB_OK);
    }

    void PollVacuumRecorder()
    {
        if (!gVacuumRecorder || !gVacuumRecorderHotkey)
            return;
        const bool down = (GetAsyncKeyState(gVacuumRecorderHotkey) & 0x8000) != 0;
        static bool wasDown = false;
        if (down && !wasDown && GameIsForeground())
            RecordVacuumRoom();
        wasDown = down;
    }

    // F10 with VacuumZoneDiagnostics: which room the recorder would take here, and what is watched.
    void LogCurrentVacuumZone()
    {
        if (!gVacuumZoneDiagnostics)
            return;
        float me[12]{};
        if (!PlayerTransform(me))
            return;
        float radius = 0.0f;
        const ZoneRoom* room = CurrentZoneRoom(me, &radius, true);
        if (room)
            Log("vacuum: this is ch%02u %s (%s)", room->chapter, room->stream,
                radius >= 0.0f ? "inside its zone" : "nearest zone");
        AcquireSRWLockShared(&gVacuumRoomLock);
        for (unsigned i = 0; i < gVacuumRoomCount; ++i)
            Log("   watched %s room %s: %s", gVacuumRooms[i].source, gVacuumRooms[i].name,
                gVacuumRooms[i].shown ? "shown, no air" : "hidden or not loaded");
        ReleaseSRWLockShared(&gVacuumRoomLock);
    }

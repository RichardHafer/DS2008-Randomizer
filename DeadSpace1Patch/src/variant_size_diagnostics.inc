    bool gVariantSizeDiagnostics = false;

    struct VariantSizeSample
    {
        uintptr_t owner, view, shapes[4];
        float position[3], radius, height, scale;
        uint32_t state;
        uint16_t flags;
    };

    struct VariantSizeTrace
    {
        uintptr_t movement = 0;
        uintptr_t owner = 0;
        DWORD start = 0, next = 0;
        unsigned samples = 0;
        bool finished = false;
        uintptr_t reportedCapsule = 0;
        VariantSizeSample before{};
    };

    VariantSizeTrace gVariantSizeTraces[4];
    unsigned gVariantSizeFaults = 0;

    void ResetVariantSizeDiagnostics()
    {
        for (auto& trace : gVariantSizeTraces)
            trace = {};
        gVariantSizeFaults = 0;
    }

    bool ReadVariantSizeSample(void* movement, VariantSizeSample& sample)
    {
        __try
        {
            const uintptr_t component = reinterpret_cast<uintptr_t>(movement);
            if (!component || *reinterpret_cast<uintptr_t*>(component) != 0x00C60E04)
                return false;
            sample.owner = *reinterpret_cast<uintptr_t*>(component + 8);
            if (!sample.owner || *reinterpret_cast<uintptr_t*>(sample.owner) != 0x00C69580)
                return false;
            const char* model = *reinterpret_cast<const char**>(sample.owner + 0x668);
            if (!model || std::memcmp(model, "sup_expl", 9) != 0)
                return false;
            sample.view = *reinterpret_cast<uintptr_t*>(sample.owner + 0x268);
            sample.scale = sample.view ? *reinterpret_cast<float*>(sample.view + 0xD4) : 0.0f;
            std::memcpy(sample.position, reinterpret_cast<void*>(sample.owner + 0xC0), 12);
            std::memcpy(sample.shapes, reinterpret_cast<void*>(component + 0x14), 16);
            sample.radius = *reinterpret_cast<float*>(component + 0x48);
            sample.height = *reinterpret_cast<float*>(component + 0x4C);
            sample.flags = *reinterpret_cast<uint16_t*>(component + 0x0C);
            sample.state = *reinterpret_cast<uint32_t*>(component + 0x224);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            if (gVariantSizeFaults++ < 4)
                Log("variant size: unreadable movement sample at %08X",
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(movement)));
            return false;
        }
    }

    int BeginVariantSizeSample(void* movement)
    {
        if (!gVariantSizeDiagnostics)
            return -1;
        const uintptr_t address = reinterpret_cast<uintptr_t>(movement);
        const DWORD now = GetTickCount();
        int freeSlot = -1;
        for (unsigned i = 0; i < 4; ++i)
        {
            auto& trace = gVariantSizeTraces[i];
            if (!trace.movement)
                freeSlot = static_cast<int>(i);
            if (trace.movement != address)
                continue;
            if (trace.finished || static_cast<LONG>(now - trace.next) < 0)
                return -1;
            if (now - trace.start >= 12000 || trace.samples >= 60)
            {
                trace.finished = true;
                Log("variant size: trace complete owner=%08X samples=%u",
                    static_cast<unsigned>(trace.owner), trace.samples);
                return -1;
            }
            VariantSizeSample sample{};
            if (!ReadVariantSizeSample(movement, sample) || sample.owner != trace.owner)
            {
                trace.finished = true;
                Log("variant size: trace ended, owner changed or unavailable movement=%08X",
                    static_cast<unsigned>(address));
                return -1;
            }
            trace.before = sample;
            trace.next = now + 200;
            return static_cast<int>(i);
        }
        if (freeSlot < 0)
            return -1;
        VariantSizeSample sample{};
        if (!ReadVariantSizeSample(movement, sample))
            return -1;
        auto& trace = gVariantSizeTraces[freeSlot];
        trace.movement = address;
        trace.owner = sample.owner;
        trace.start = now;
        trace.next = now + 200;
        trace.before = sample;
        Log("variant size: trace start owner=%08X movement=%08X view=%08X slot=%d/4",
            static_cast<unsigned>(sample.owner), static_cast<unsigned>(address),
            static_cast<unsigned>(sample.view), freeSlot + 1);
        return freeSlot;
    }

    void EndVariantSizeSample(void* movement, int index)
    {
        if (index < 0)
            return;
        auto& trace = gVariantSizeTraces[index];
        VariantSizeSample after{};
        if (!ReadVariantSizeSample(movement, after) || after.owner != trace.owner)
        {
            trace.finished = true;
            Log("variant size: post-update sample unavailable movement=%08X",
                static_cast<unsigned>(trace.movement));
            return;
        }
        const auto& before = trace.before;
        ++trace.samples;
        if (trace.samples == 1 || after.shapes[2] != trace.reportedCapsule)
        {
            trace.reportedCapsule = after.shapes[2];
            __try
            {
                const uintptr_t shape = after.shapes[2];
                const uintptr_t sphere = after.shapes[1];
                const uintptr_t wrapper = after.shapes[0];
                if (sphere && wrapper &&
                    *reinterpret_cast<uintptr_t*>(sphere) == 0x00BCC844 &&
                    *reinterpret_cast<uintptr_t*>(wrapper) == 0x00BCC960 &&
                    *reinterpret_cast<uintptr_t*>(wrapper + 0x14) == sphere)
                    Log("variant size: sphere=%08X radius=%.3f wrapper=%08X localY=%.3f",
                        static_cast<unsigned>(sphere), *reinterpret_cast<float*>(sphere + 0x10),
                        static_cast<unsigned>(wrapper), *reinterpret_cast<float*>(wrapper + 0x64));
                const uintptr_t type = shape ? *reinterpret_cast<uintptr_t*>(shape) : 0;
                if (type == 0x00BCC8AC)
                    Log("variant size: capsule=%08X radius=%.3f endpointY=(%.3f,%.3f)",
                        static_cast<unsigned>(shape), *reinterpret_cast<float*>(shape + 0x10),
                        *reinterpret_cast<float*>(shape + 0x24), *reinterpret_cast<float*>(shape + 0x34));
                else
                    Log("variant size: capsule slot=%08X type=%08X, geometry unavailable",
                        static_cast<unsigned>(shape), static_cast<unsigned>(type));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                Log("variant size: capsule geometry unreadable");
            }
        }
        Log("variant size: owner=%08X t=%u n=%u scale=%.3f r=%.3f h=%.3f "
            "pos=(%.3f,%.3f,%.3f)->(%.3f,%.3f,%.3f) "
            "flags=%04X->%04X state224=%u->%u shapes=%08X/%08X/%08X/%08X",
            static_cast<unsigned>(after.owner), GetTickCount() - trace.start,
            trace.samples, after.scale, after.radius, after.height,
            before.position[0], before.position[1], before.position[2],
            after.position[0], after.position[1], after.position[2],
            before.flags, after.flags, before.state, after.state,
            static_cast<unsigned>(after.shapes[0]), static_cast<unsigned>(after.shapes[1]),
            static_cast<unsigned>(after.shapes[2]), static_cast<unsigned>(after.shapes[3]));
    }

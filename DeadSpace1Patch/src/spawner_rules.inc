    // =====================================================================
    // Spawner and family rules (DS2's [Sources] and spawner rules).
    //
    // [SpawnerRules], one authored spawner per line, by the name the F10
    // readout prints or by its GUID:
    //     spawn_quarantine_zombie_3=keep              never replaced
    //     spawn_cc_leaper_1=family:Lurker             only that family
    //     spawn_triage_super_slasher=variant:twitcher only that variant
    // A name several spawners share applies to all of them. A family or
    // variant rule chooses among what the randomizer allows there anyway (the
    // global pool); one that names the Hunter also lifts the Hunter's limits.
    // EnemyRandomizerKeepChance_<Class>=N: N per cent of that class's
    // spawners keep their authored enemy, drawn from the seed.
    // Both are read again from the INI watcher thread when the file is saved,
    // so the game thread reads them through a lock and gets copies.
    // =====================================================================
    enum class SpawnerRuleKind
    {
        Keep,
        Family,
        Variant,
    };

    struct SpawnerRule
    {
        Guid128 source{};
        SpawnerRuleKind kind = SpawnerRuleKind::Keep;
        char value[32]{};
        char name[64]{};
        unsigned index = 0;
        bool reported = false;
    };
    constexpr unsigned kMaxSpawnerRules = 64;
    SpawnerRule gSpawnerRules[kMaxSpawnerRules]{};
    unsigned gSpawnerRuleCount = 0;

    struct KeepChanceRow
    {
        const char* actorClass = nullptr;
        unsigned percent = 0;
    };
    KeepChanceRow gKeepChances[24]{};
    unsigned gKeepChanceCount = 0;
    SRWLOCK gSpawnerRulesLock = SRWLOCK_INIT;

    void LoadSpawnerRules()
    {
        static SpawnerRule rules[kMaxSpawnerRules];
        static KeepChanceRow keeps[24];
        unsigned ruleCount = 0, keepCount = 0;
        static char section[8192];
        std::memset(section, 0, sizeof(section));
        const DWORD used = GetPrivateProfileSectionA("SpawnerRules", section, sizeof(section) - 2, gIniPath);
        unsigned unknown = 0, malformed = 0, counts[3]{};
        for (const char* cursor = section; used && *cursor; cursor += std::strlen(cursor) + 1)
        {
            const char* equals = std::strchr(cursor, '=');
            if (!equals || equals == cursor || cursor[0] == ';')
                continue;
            SpawnerRule rule;
            const size_t keyLength = static_cast<size_t>(equals - cursor);
            if (keyLength >= sizeof(rule.name))
            {
                ++malformed;
                continue;
            }
            std::memcpy(rule.name, cursor, keyLength);
            for (size_t n = std::strlen(rule.name); n && rule.name[n - 1] == ' '; --n)
                rule.name[n - 1] = '\0';
            const char* value = equals + 1;
            while (*value == ' ')
                ++value;
            if (_strnicmp(value, "keep", 4) == 0)
                rule.kind = SpawnerRuleKind::Keep;
            else if (_strnicmp(value, "family:", 7) == 0)
            {
                rule.kind = SpawnerRuleKind::Family;
                strncpy_s(rule.value, value + 7, _TRUNCATE);
            }
            else if (_strnicmp(value, "variant:", 8) == 0)
            {
                rule.kind = SpawnerRuleKind::Variant;
                strncpy_s(rule.value, value + 8, _TRUNCATE);
            }
            else
            {
                ++malformed;
                Log("spawner rule: '%s=%s' is not keep, family:<Class> or variant:<variant>", rule.name, value);
                continue;
            }
            for (size_t n = std::strlen(rule.value); n && rule.value[n - 1] == ' '; --n)
                rule.value[n - 1] = '\0';
            // Every spawner of that name (13 names are shared), or the one GUID.
            unsigned matched = 0;
            for (unsigned i = 0; i < kSpawnSourceNameCount && ruleCount < kMaxSpawnerRules; ++i)
                if (_stricmp(kSpawnSourceNames[i].name, rule.name) == 0)
                {
                    rule.source = kSpawnSourceNames[i].source;
                    rule.index = ruleCount;
                    rules[ruleCount++] = rule;
                    ++matched;
                }
            if (!matched && ParseGuidText(rule.name, &rule.source) && ruleCount < kMaxSpawnerRules)
            {
                rule.index = ruleCount;
                rules[ruleCount++] = rule;
                ++matched;
            }
            if (!matched)
            {
                ++unknown;
                Log("spawner rule: '%s' is no known spawner name or GUID (F10 prints the names)", rule.name);
                continue;
            }
            counts[static_cast<int>(rule.kind)] += matched;
            if (ruleCount >= kMaxSpawnerRules)
            {
                Log("spawner rule: %u spawners reached, '%s' and the rest may be cut short", kMaxSpawnerRules,
                    rule.name);
                break;
            }
        }

        for (unsigned i = 0; i < kSpawnCompatibilitySourceCount && keepCount < 24; ++i)
        {
            const char* actorClass = kSpawnCompatibilitySources[i].actorClass;
            bool known = false;
            for (unsigned k = 0; k < keepCount && !known; ++k)
                known = std::strcmp(keeps[k].actorClass, actorClass) == 0;
            if (known || !actorClass)
                continue;
            char key[96]{};
            std::snprintf(key, sizeof(key), "EnemyRandomizerKeepChance_%s", actorClass);
            int percent = GetPrivateProfileIntA("DeadSpace1Patch", key, 0, gIniPath);
            percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
            keeps[keepCount++] = {actorClass, static_cast<unsigned>(percent)};
            if (percent)
                Log("randomizer keep chance: %d%% of the %s spawners keep their authored enemy", percent, actorClass);
        }

        AcquireSRWLockExclusive(&gSpawnerRulesLock);
        std::memcpy(gSpawnerRules, rules, sizeof(SpawnerRule) * ruleCount);
        gSpawnerRuleCount = ruleCount;
        std::memcpy(gKeepChances, keeps, sizeof(KeepChanceRow) * keepCount);
        gKeepChanceCount = keepCount;
        ReleaseSRWLockExclusive(&gSpawnerRulesLock);
        Log("spawner rules: %u spawner(s) (%u keep, %u family, %u variant), %u unknown name(s), %u malformed",
            ruleCount, counts[0], counts[1], counts[2], unknown, malformed);
    }

    // A copy, so a reload on the watcher thread cannot change it under the caller.
    bool SpawnerRuleFor(const Guid128& source, SpawnerRule* out)
    {
        bool found = false;
        AcquireSRWLockShared(&gSpawnerRulesLock);
        for (unsigned i = 0; i < gSpawnerRuleCount && !found; ++i)
            if (SameGuid(gSpawnerRules[i].source, source))
            {
                *out = gSpawnerRules[i];
                found = true;
            }
        ReleaseSRWLockShared(&gSpawnerRulesLock);
        return found;
    }

    // Seeded per spawner, so a checkpoint reload keeps the same answer.
    bool KeepChanceKeeps(const char* actorClass, const Guid128& source, const Guid128& target)
    {
        unsigned percent = 0;
        AcquireSRWLockShared(&gSpawnerRulesLock);
        for (unsigned i = 0; i < gKeepChanceCount; ++i)
            if (actorClass && std::strcmp(gKeepChances[i].actorClass, actorClass) == 0)
                percent = gKeepChances[i].percent;
        ReleaseSRWLockShared(&gSpawnerRulesLock);
        return percent && StableRandomizerHash(source, target, 0x4B454550u) % 100u < percent;
    }

    bool SpawnerRuleAllows(const SpawnerRule* rule, const char* actorClass, const char* variant)
    {
        if (!rule)
            return true;
        if (rule->kind == SpawnerRuleKind::Family)
            return actorClass && _stricmp(actorClass, rule->value) == 0;
        if (rule->kind == SpawnerRuleKind::Variant)
            return variant && _stricmp(variant, rule->value) == 0;
        return false;
    }

    bool SpawnerRuleNamesHunter(const SpawnerRule* rule)
    {
        return rule && ((rule->kind == SpawnerRuleKind::Family && _stricmp(rule->value, "Hunter") == 0) ||
                        (rule->kind == SpawnerRuleKind::Variant && _stricmp(rule->value, "hunter") == 0));
    }

    void ReportSpawnerRule(const SpawnerRule* rule, const char* what)
    {
        if (!rule)
            return;
        bool first = false;
        AcquireSRWLockExclusive(&gSpawnerRulesLock);
        if (rule->index < gSpawnerRuleCount && SameGuid(gSpawnerRules[rule->index].source, rule->source) &&
            !gSpawnerRules[rule->index].reported)
            first = gSpawnerRules[rule->index].reported = true;
        ReleaseSRWLockExclusive(&gSpawnerRulesLock);
        if (first)
            Log("spawner rule: %s %s", rule->name, what);
    }

    // For the spawn hook, ahead of the variant substitutions: a keep rule or
    // a keep chance holds them back, and a family or variant rule leaves the
    // pick to CandidateFor.
    bool SpawnerRulesHoldBack(const Guid128& source, const Guid128& target, const char* actorClass)
    {
        SpawnerRule rule;
        if (SpawnerRuleFor(source, &rule))
        {
            if (rule.kind == SpawnerRuleKind::Keep)
                ReportSpawnerRule(&rule, "keeps its authored enemy");
            return true;
        }
        return KeepChanceKeeps(actorClass, source, target);
    }

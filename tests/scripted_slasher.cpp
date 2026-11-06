#include "../src/dllmain.cpp"
#include <cassert>

int main()
{
    using namespace scripted_skin;
    struct Object { char prefix[0x20]; Guid128 id; } object{};
    const auto address = reinterpret_cast<uintptr_t>(&object);
    const auto original = reinterpret_cast<uintptr_t>(&skins[0]);
    gEnemyRandomizer = gEnemyRandomizerScriptedVariants = true;
    gEnemyRandomizerChance = 100;
    gActorCount = 1;
    strcpy_s(gActorNames[0], "Zombie");
    ready = 1;
    for (unsigned seed : {1u, 0x12345678u, 0xffffffffu}) {
        gEnemyRandomizerSeed = seed;
        uintptr_t expected[_countof(actors)]{};
        for (unsigned i = 0; i < _countof(actors); ++i) {
            object.id = actors[i];
            expected[i] = choose(address, original);
            assert(expected[i] == reinterpret_cast<uintptr_t>(&skins[StableRandomizerHash(object.id, object.id, 0x534b494eu) % 4]));
        }
        for (unsigned i = _countof(actors); i-- > 0;) {
            Object reload = object;
            reload.id = actors[i];
            assert(choose(reinterpret_cast<uintptr_t>(&reload), original) == expected[i]);
        }
    }
    object.id = {};
    assert(choose(address, original) == original);
    object.id = actors[0];
    gEnemyRandomizerChance = 0;
    assert(choose(address, original) == original);
    gEnemyRandomizerChance = 100;
    gEnemyRandomizerSourceFlagsLoaded = true;
    assert(choose(address, original) == original);
    gEnemyRandomizerSourceEnabled[0] = true;
    gEnemyRandomizerScriptedVariants = false;
    assert(choose(address, original) == original);
    puts("PASS: seeded Slasher appearances, reload order, source filters and opt-out.");
}

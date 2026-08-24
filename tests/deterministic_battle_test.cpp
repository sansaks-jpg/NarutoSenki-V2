#include "Network/AuthoritativeBattleState.hpp"
#include "Network/LanProtocol.hpp"

#include <cassert>
#include <iostream>
#include <vector>

using namespace nsv2::network;

void testDeterministicPRNG()
{
    DeterministicPRNG rng1(123456);
    DeterministicPRNG rng2(123456);

    for (int i = 0; i < 1000; ++i)
    {
        assert(rng1.next() == rng2.next());
        assert(rng1.nextRange(1, 100) == rng2.nextRange(1, 100));
    }
}

void testIdenticalInputProducesIdenticalChecksum()
{
    MatchConfig config;
    config.matchId = 1001;
    config.seed = 998877;
    config.slots = {
        {0, GroupId::Konoha, true, false, "Host", "Naruto"},
        {1, GroupId::Akatsuki, true, true, "Client", "Sasuke"},
    };

    AuthoritativeBattleState simA;
    AuthoritativeBattleState simB;

    simA.initializeMatch(config);
    simB.initializeMatch(config);

    // Simulate 300 ticks of identical inputs
    for (uint32_t tick = 1; tick <= 300; ++tick)
    {
        InputCommand cmd0;
        cmd0.matchId = config.matchId;
        cmd0.tick = tick;
        cmd0.sequence = tick;
        cmd0.playerSlot = 0;
        cmd0.axisX = static_cast<int16_t>((tick % 60 < 30) ? 1000 : -1000);
        cmd0.axisY = static_cast<int16_t>((tick % 40 < 20) ? 500 : -500);

        InputCommand cmd1;
        cmd1.matchId = config.matchId;
        cmd1.tick = tick;
        cmd1.sequence = tick;
        cmd1.playerSlot = 1;
        cmd1.axisX = static_cast<int16_t>((tick % 50 < 25) ? -1000 : 1000);
        cmd1.axisY = 0;

        simA.submitPlayerInput(cmd0);
        simA.submitPlayerInput(cmd1);
        simA.stepSimulation(tick);

        simB.submitPlayerInput(cmd0);
        simB.submitPlayerInput(cmd1);
        simB.stepSimulation(tick);

        if (tick % 15 == 0)
        {
            StateSnapshot snapA = simA.buildSnapshot(tick, static_cast<uint16_t>(tick / 30));
            StateSnapshot snapB = simB.buildSnapshot(tick, static_cast<uint16_t>(tick / 30));

            assert(snapA.stateChecksum == snapB.stateChecksum);
            assert(snapA.characters[0].x == snapB.characters[0].x);
            assert(snapA.characters[1].x == snapB.characters[1].x);
        }
    }
}

void testSweptProjectileCollisionNoTunneling()
{
    MatchConfig config;
    config.matchId = 2002;
    config.seed = 555;
    config.slots = {
        {0, GroupId::Konoha, true, false, "Host", "Naruto"},
        {1, GroupId::Akatsuki, true, true, "Client", "Sasuke"},
    };

    AuthoritativeBattleState sim;
    sim.initializeMatch(config);

    // Hero 0 at x=28000, Hero 1 at x=320000
    // Spawn high-speed projectile moving 50000 units/tick (would tunnel over target in point check)
    // Target is at 320000, y=8000. Projectile flies from 250000 to 350000 in two ticks.
    sim.spawnProjectile(0, 250000, 8000, 60000, 0, 1500);

    const auto *hero1Before = sim.getHero(1);
    assert(hero1Before->hp == 10000);

    // Step 1 tick: projectile moves from 250000 to 310000 (hasn't reached 320000 yet)
    sim.stepSimulation(1);
    const auto *hero1Mid = sim.getHero(1);
    assert(hero1Mid->hp == 10000);

    // Step 2nd tick: projectile sweeps from 310000 to 370000, passing through 320000
    sim.stepSimulation(2);
    const auto *hero1After = sim.getHero(1);
    assert(hero1After->hp == 8500); // Successfully registered hit via swept collision!

    StateSnapshot snap = sim.buildSnapshot(2, 0);
    assert(!snap.combatEvents.empty());
    assert(snap.combatEvents[0].eventType == CombatEventType::HitImpact);
    assert(snap.combatEvents[0].value == -1500);
}

int main()
{
    testDeterministicPRNG();
    testIdenticalInputProducesIdenticalChecksum();
    testSweptProjectileCollisionNoTunneling();
    std::cout << "deterministic_battle_test: ok\n";
    return 0;
}

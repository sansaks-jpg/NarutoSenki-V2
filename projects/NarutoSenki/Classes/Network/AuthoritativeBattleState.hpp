#pragma once

#include "LanProtocol.hpp"

#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nsv2::network
{

struct SimHeroState
{
    uint8_t slot = 0;
    int32_t x = 0; // Fixed-point (x100)
    int32_t y = 0; // Fixed-point (x100)
    int32_t vx = 0;
    int32_t vy = 0;
    uint32_t hp = 10000;
    uint32_t maxHp = 10000;
    uint32_t ckr = 0;
    uint32_t maxCkr = 3000;
    uint8_t state = 0; // CharacterBase State enum value
    bool flipped = false;
    uint32_t lastAppliedSequence = 0;
    uint32_t lastInputTick = 0;
    int deadCount = 0;
    int killCount = 0;
};

struct SimProjectile
{
    uint32_t id = 0;
    uint8_t ownerSlot = 0;
    int32_t startX = 0;
    int32_t startY = 0;
    int32_t currentX = 0;
    int32_t currentY = 0;
    int32_t prevX = 0;
    int32_t prevY = 0;
    int32_t speedX = 0;
    int32_t speedY = 0;
    int32_t damage = 500;
    int32_t radius = 4000; // Fixed-point 40.0f * 100
    uint32_t lifetimeTicks = 60;
    bool active = true;
};

class DeterministicPRNG
{
public:
    explicit DeterministicPRNG(uint32_t seed = 0x12345678u) : _state(seed ? seed : 0x12345678u) {}

    uint32_t next()
    {
        uint32_t x = _state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        _state = x;
        return _state;
    }

    uint32_t nextRange(uint32_t minVal, uint32_t maxVal)
    {
        if (minVal >= maxVal)
            return minVal;
        return minVal + (next() % (maxVal - minVal + 1));
    }

    void setSeed(uint32_t seed)
    {
        _state = seed ? seed : 0x12345678u;
    }

    uint32_t state() const { return _state; }

private:
    uint32_t _state;
};

class AuthoritativeBattleState
{
public:
    AuthoritativeBattleState();
    ~AuthoritativeBattleState() = default;

    void initializeMatch(const MatchConfig &config);
    void submitPlayerInput(const InputCommand &command);
    void stepSimulation(uint32_t tick);

    StateSnapshot buildSnapshot(uint32_t tick, uint16_t elapsedSeconds);
    void drainCombatEvents(std::vector<CombatEvent> &out);

    const SimHeroState *getHero(uint8_t slot) const;
    SimHeroState *getHeroMut(uint8_t slot);

    void applyDamage(uint8_t sourceSlot, uint8_t targetSlot, int32_t damage, bool knockback);
    void spawnProjectile(uint8_t ownerSlot, int32_t startX, int32_t startY, int32_t speedX, int32_t speedY, int32_t damage);

    uint32_t currentTick() const { return _currentTick; }
    uint32_t matchId() const { return _config.matchId; }
    uint32_t sessionEpoch() const { return _sessionEpoch; }
    DeterministicPRNG &prng() { return _prng; }

private:
    void resolveMovement(uint8_t slot, const InputCommand &cmd);
    void resolveProjectiles();
    bool checkSweptCollision(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t targetX, int32_t targetY, int32_t radius);

    MatchConfig _config;
    uint32_t _currentTick = 0;
    uint32_t _sessionEpoch = 0;
    uint32_t _nextEventId = 1;
    uint32_t _nextProjectileId = 1;
    DeterministicPRNG _prng;

    std::vector<SimHeroState> _heroes;
    std::vector<SimProjectile> _projectiles;
    std::deque<CombatEvent> _pendingCombatEvents;
    std::unordered_map<uint8_t, InputCommand> _latestInputs;
};

} // namespace nsv2::network

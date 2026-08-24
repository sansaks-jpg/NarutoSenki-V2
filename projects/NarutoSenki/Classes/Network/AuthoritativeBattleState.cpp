#include "AuthoritativeBattleState.hpp"

#include <algorithm>
#include <cmath>

namespace nsv2::network
{

namespace
{
constexpr int32_t kMaxSpeed = 450; // max speed in units per second * 100
constexpr int32_t kMapMinX = 5000;
constexpr int32_t kMapMaxX = 350000;
constexpr int32_t kMapMinY = 3000;
constexpr int32_t kMapMaxY = 15000;
} // namespace

AuthoritativeBattleState::AuthoritativeBattleState() = default;

void AuthoritativeBattleState::initializeMatch(const MatchConfig &config)
{
    _config = config;
    _currentTick = 0;
    _sessionEpoch = config.seed ^ config.matchId;
    _prng.setSeed(config.seed ? config.seed : 0x12345678u);
    _nextEventId = 1;
    _nextProjectileId = 1;
    _pendingCombatEvents.clear();
    _latestInputs.clear();
    _projectiles.clear();

    _heroes.clear();
    _heroes.resize(config.slots.size());
    for (size_t i = 0; i < config.slots.size(); ++i)
    {
        _heroes[i].slot = static_cast<uint8_t>(i);
        _heroes[i].hp = 10000;
        _heroes[i].maxHp = 10000;
        _heroes[i].ckr = 0;
        _heroes[i].maxCkr = 3000;
        _heroes[i].state = 0; // IDLE
        _heroes[i].deadCount = 0;
        _heroes[i].killCount = 0;
        _heroes[i].lastAppliedSequence = 0;
        _heroes[i].lastInputTick = 0;

        // Position 1P on the left, 2P on the right (fixed point x100)
        if (i == 0)
        {
            _heroes[i].x = 28000;
            _heroes[i].y = 8000;
            _heroes[i].flipped = false;
        }
        else
        {
            _heroes[i].x = 320000;
            _heroes[i].y = 8000;
            _heroes[i].flipped = true;
        }
    }
}

void AuthoritativeBattleState::submitPlayerInput(const InputCommand &command)
{
    if (command.playerSlot >= _heroes.size())
        return;
    _latestInputs[command.playerSlot] = command;
    _heroes[command.playerSlot].lastAppliedSequence = command.sequence;
    _heroes[command.playerSlot].lastInputTick = command.tick;
}

void AuthoritativeBattleState::resolveMovement(uint8_t slot, const InputCommand &cmd)
{
    if (slot >= _heroes.size())
        return;
    auto &hero = _heroes[slot];

    // Clamped direction axis
    float ax = std::clamp(cmd.axisX / 1000.0f, -1.0f, 1.0f);
    float ay = std::clamp(cmd.axisY / 1000.0f, -1.0f, 1.0f);

    if (std::abs(ax) > 0.05f || std::abs(ay) > 0.05f)
    {
        hero.vx = static_cast<int32_t>(ax * kMaxSpeed);
        hero.vy = static_cast<int32_t>(ay * kMaxSpeed);
        hero.x += hero.vx;
        hero.y += hero.vy;
        hero.flipped = ax < 0;
        hero.state = 1; // WALK
    }
    else
    {
        hero.vx = 0;
        hero.vy = 0;
        if (hero.state == 1) // If was WALK, transition to IDLE
            hero.state = 0;
    }

    // Boundary check
    hero.x = std::clamp(hero.x, kMapMinX, kMapMaxX);
    hero.y = std::clamp(hero.y, kMapMinY, kMapMaxY);
}

bool AuthoritativeBattleState::checkSweptCollision(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t targetX, int32_t targetY, int32_t radius)
{
    // Distance from target point to segment (x1, y1) -> (x2, y2)
    double px = x2 - x1;
    double py = y2 - y1;
    double lenSq = px * px + py * py;
    if (lenSq < 1e-6)
    {
        double dx = targetX - x1;
        double dy = targetY - y1;
        return (dx * dx + dy * dy) <= static_cast<double>(radius) * radius;
    }

    double u = ((targetX - x1) * px + (targetY - y1) * py) / lenSq;
    u = std::clamp(u, 0.0, 1.0);
    double ix = x1 + u * px;
    double iy = y1 + u * py;
    double distSq = (targetX - ix) * (targetX - ix) + (targetY - iy) * (targetY - iy);
    return distSq <= static_cast<double>(radius) * radius;
}

void AuthoritativeBattleState::resolveProjectiles()
{
    for (auto &p : _projectiles)
    {
        if (!p.active)
            continue;

        p.prevX = p.currentX;
        p.prevY = p.currentY;
        p.currentX += p.speedX;
        p.currentY += p.speedY;

        if (p.lifetimeTicks > 0)
            --p.lifetimeTicks;
        else
        {
            p.active = false;
            continue;
        }

        // Test collision against opponent heroes
        for (auto &hero : _heroes)
        {
            if (hero.slot == p.ownerSlot || hero.hp == 0)
                continue;

            if (checkSweptCollision(p.prevX, p.prevY, p.currentX, p.currentY, hero.x, hero.y, p.radius))
            {
                applyDamage(p.ownerSlot, hero.slot, p.damage, true);
                p.active = false;
                break;
            }
        }
    }

    // Clean up inactive projectiles
    _projectiles.erase(std::remove_if(_projectiles.begin(), _projectiles.end(), [](const SimProjectile &p) {
        return !p.active;
    }), _projectiles.end());
}

void AuthoritativeBattleState::spawnProjectile(uint8_t ownerSlot, int32_t startX, int32_t startY, int32_t speedX, int32_t speedY, int32_t damage)
{
    SimProjectile proj;
    proj.id = _nextProjectileId++;
    proj.ownerSlot = ownerSlot;
    proj.startX = startX;
    proj.startY = startY;
    proj.prevX = startX;
    proj.prevY = startY;
    proj.currentX = startX;
    proj.currentY = startY;
    proj.speedX = speedX;
    proj.speedY = speedY;
    proj.damage = damage;
    proj.active = true;
    proj.lifetimeTicks = 60;
    _projectiles.push_back(proj);
}

void AuthoritativeBattleState::applyDamage(uint8_t sourceSlot, uint8_t targetSlot, int32_t damage, bool knockback)
{
    if (targetSlot >= _heroes.size())
        return;

    auto &target = _heroes[targetSlot];
    if (target.hp == 0)
        return;

    uint32_t actualDmg = static_cast<uint32_t>(std::min(static_cast<int32_t>(target.hp), damage));
    target.hp -= actualDmg;

    // Generate Hit Impact event
    CombatEvent hitEv;
    hitEv.eventId = _nextEventId++;
    hitEv.tick = _currentTick;
    hitEv.eventType = CombatEventType::HitImpact;
    hitEv.sourceSlot = sourceSlot;
    hitEv.targetSlot = targetSlot;
    hitEv.value = -static_cast<int32_t>(actualDmg);
    hitEv.posX = static_cast<int16_t>(target.x / 100);
    hitEv.posY = static_cast<int16_t>(target.y / 100);
    _pendingCombatEvents.push_back(hitEv);

    if (knockback)
    {
        CombatEvent kbEv;
        kbEv.eventId = _nextEventId++;
        kbEv.tick = _currentTick;
        kbEv.eventType = CombatEventType::KnockbackApplied;
        kbEv.sourceSlot = sourceSlot;
        kbEv.targetSlot = targetSlot;
        kbEv.value = target.flipped ? 20 : -20;
        kbEv.posX = static_cast<int16_t>(target.x / 100);
        kbEv.posY = static_cast<int16_t>(target.y / 100);
        _pendingCombatEvents.push_back(kbEv);
    }

    if (target.hp == 0)
    {
        target.deadCount++;
        if (sourceSlot < _heroes.size())
            _heroes[sourceSlot].killCount++;

        CombatEvent deadEv;
        deadEv.eventId = _nextEventId++;
        deadEv.tick = _currentTick;
        deadEv.eventType = CombatEventType::CharacterDead;
        deadEv.sourceSlot = sourceSlot;
        deadEv.targetSlot = targetSlot;
        deadEv.value = target.deadCount;
        deadEv.posX = static_cast<int16_t>(target.x / 100);
        deadEv.posY = static_cast<int16_t>(target.y / 100);
        _pendingCombatEvents.push_back(deadEv);
    }
}

void AuthoritativeBattleState::stepSimulation(uint32_t tick)
{
    _currentTick = tick;

    // Process inputs
    for (size_t i = 0; i < _heroes.size(); ++i)
    {
        auto it = _latestInputs.find(static_cast<uint8_t>(i));
        if (it != _latestInputs.end())
        {
            resolveMovement(static_cast<uint8_t>(i), it->second);
        }
    }

    // Step projectiles
    resolveProjectiles();
}

StateSnapshot AuthoritativeBattleState::buildSnapshot(uint32_t tick, uint16_t elapsedSeconds)
{
    StateSnapshot snapshot;
    snapshot.matchId = _config.matchId;
    snapshot.tick = tick;
    snapshot.elapsedSeconds = elapsedSeconds;
    snapshot.sessionEpoch = _sessionEpoch;

    uint32_t maxSeq = 0;
    for (const auto &h : _heroes)
    {
        maxSeq = std::max(maxSeq, h.lastAppliedSequence);
        CharacterSnapshot cs;
        cs.slot = h.slot;
        cs.x = h.x;
        cs.y = h.y;
        cs.hp = h.hp;
        cs.ckr = h.ckr;
        cs.state = h.state;
        cs.flipped = h.flipped;
        snapshot.characters.push_back(cs);
    }
    snapshot.clientSequenceWatermark = maxSeq;

    // Drain events into snapshot
    while (!_pendingCombatEvents.empty())
    {
        snapshot.combatEvents.push_back(_pendingCombatEvents.front());
        _pendingCombatEvents.pop_front();
    }

    snapshot.stateChecksum = computeStateChecksum(snapshot);
    return snapshot;
}

void AuthoritativeBattleState::drainCombatEvents(std::vector<CombatEvent> &out)
{
    while (!_pendingCombatEvents.empty())
    {
        out.push_back(std::move(_pendingCombatEvents.front()));
        _pendingCombatEvents.pop_front();
    }
}

const SimHeroState *AuthoritativeBattleState::getHero(uint8_t slot) const
{
    if (slot < _heroes.size())
        return &_heroes[slot];
    return nullptr;
}

SimHeroState *AuthoritativeBattleState::getHeroMut(uint8_t slot)
{
    if (slot < _heroes.size())
        return &_heroes[slot];
    return nullptr;
}

} // namespace nsv2::network

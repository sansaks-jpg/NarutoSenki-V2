#include "NetworkPresentationAdapter.hpp"
#include "CharacterBase.h"
#include "Core/Hero.hpp"
#include "GameLayer.h"
#include "HPBar.h"
#include "HudLayer.h"
#include "SimpleAudioEngine.h"

#include <algorithm>
#include <cmath>

using namespace CocosDenshion;

namespace nsv2::network
{

namespace
{
constexpr float kSnapThreshold = 40.0f; // in Cocos2d-x points
constexpr size_t kMaxDedupEvents = 256;
constexpr size_t kMaxUnackedInputs = 60;
} // namespace

NetworkPresentationAdapter::NetworkPresentationAdapter() = default;

void NetworkPresentationAdapter::initialize(GameLayer *gameLayer, uint8_t localSlot)
{
    _gameLayer = gameLayer;
    _localSlot = localSlot;
    reset();
}

void NetworkPresentationAdapter::reset()
{
    _lastAppliedTick = 0;
    _errorOffsetX = 0.0f;
    _errorOffsetY = 0.0f;
    _unackedInputs.clear();
    _remoteCharLerp.clear();
    _remoteUnitLerp.clear();
    _processedEventIds.clear();
    _eventOrderQueue.clear();
}

void NetworkPresentationAdapter::recordPredictedInput(uint32_t sequence, uint32_t tick, int16_t axisX, int16_t axisY, float speed, float dt)
{
    PredictedInput input;
    input.sequence = sequence;
    input.tick = tick;
    input.axisX = axisX;
    input.axisY = axisY;

    float ax = std::clamp(axisX / 1000.0f, -1.0f, 1.0f);
    float ay = std::clamp(axisY / 1000.0f, -1.0f, 1.0f);
    input.deltaX = ax * speed * dt;
    input.deltaY = ay * speed * dt;

    _unackedInputs.push_back(input);
    if (_unackedInputs.size() > kMaxUnackedInputs)
        _unackedInputs.pop_front();
}

void NetworkPresentationAdapter::pruneAcknowledgedInputs(uint32_t ackSequence)
{
    while (!_unackedInputs.empty() && _unackedInputs.front().sequence <= ackSequence)
    {
        _unackedInputs.pop_front();
    }
}

void NetworkPresentationAdapter::reconcileLocalPosition(const CharacterSnapshot &authSnap, float speed)
{
    if (!_gameLayer || authSnap.slot >= _gameLayer->_CharacterArray.size())
        return;
    auto *localHero = _gameLayer->_CharacterArray[authSnap.slot];
    if (!localHero || localHero->getState() == State::DEAD)
        return;

    float authX = authSnap.x / 100.0f;
    float authY = authSnap.y / 100.0f;

    // Replay unacknowledged predicted inputs on top of authoritative base
    float replayedX = authX;
    float replayedY = authY;
    for (const auto &inp : _unackedInputs)
    {
        replayedX += inp.deltaX;
        replayedY += inp.deltaY;
    }

    Vec2 currentPos = localHero->getPosition();
    float dx = currentPos.x - replayedX;
    float dy = currentPos.y - replayedY;
    float dist = std::sqrt(dx * dx + dy * dy);

    if (dist > kSnapThreshold)
    {
        // Major discrepancy (e.g. knockback/stun/teleport) -> snap immediately
        localHero->setPosition(Vec2(replayedX, replayedY));
        _errorOffsetX = 0.0f;
        _errorOffsetY = 0.0f;
    }
    else
    {
        // Smooth error decay offset
        _errorOffsetX = dx;
        _errorOffsetY = dy;
        localHero->setPosition(Vec2(replayedX, replayedY));
    }
}

bool NetworkPresentationAdapter::hasRecentEvent(uint32_t eventId) const
{
    return _processedEventIds.count(eventId) > 0;
}

void NetworkPresentationAdapter::dispatchCombatEvents(const std::vector<CombatEvent> &events)
{
    for (const auto &ev : events)
    {
        if (ev.eventId != 0 && hasRecentEvent(ev.eventId))
            continue; // Deduplicate

        if (ev.eventId != 0)
        {
            _processedEventIds.insert(ev.eventId);
            _eventOrderQueue.push_back(ev.eventId);
            if (_eventOrderQueue.size() > kMaxDedupEvents)
            {
                _processedEventIds.erase(_eventOrderQueue.front());
                _eventOrderQueue.pop_front();
            }
        }

        if (_eventCallback)
        {
            _eventCallback(ev);
        }

        // Apply visual/audio feedback for combat events
        if (!_gameLayer)
            continue;

        if (ev.eventType == CombatEventType::HitImpact)
        {
            if (ev.targetSlot < _gameLayer->_CharacterArray.size())
            {
                auto *target = _gameLayer->_CharacterArray[ev.targetSlot];
                if (target && target->getState() != State::DEAD)
                {
                    SimpleAudioEngine::sharedEngine()->playEffect("Audio/Effect/hit.ogg");
                }
            }
        }
        else if (ev.eventType == CombatEventType::KnockbackApplied)
        {
            if (ev.targetSlot < _gameLayer->_CharacterArray.size())
            {
                auto *target = _gameLayer->_CharacterArray[ev.targetSlot];
                if (target && target->getState() != State::DEAD)
                {
                    target->setPosition(Vec2(target->getPositionX() + ev.value, target->getPositionY()));
                }
            }
        }
    }
}

void NetworkPresentationAdapter::applyAuthoritativeSnapshot(const StateSnapshot &snapshot, float speed)
{
    if (!_gameLayer)
        return;

    // Stale snapshot rejection
    if (snapshot.tick <= _lastAppliedTick)
        return;
    _lastAppliedTick = snapshot.tick;

    // Prune acknowledged prediction inputs
    pruneAcknowledgedInputs(snapshot.clientSequenceWatermark);

    // Process characters
    for (const auto &charSnap : snapshot.characters)
    {
        if (charSnap.slot == _localSlot)
        {
            reconcileLocalPosition(charSnap, speed);
        }
        else
        {
            // Remote character interpolation
            if (charSnap.slot < _gameLayer->_CharacterArray.size())
            {
                auto *remoteChar = _gameLayer->_CharacterArray[charSnap.slot];
                if (remoteChar)
                {
                    auto &lerp = _remoteCharLerp[charSnap.slot];
                    lerp.fromX = remoteChar->getPositionX();
                    lerp.fromY = remoteChar->getPositionY();
                    lerp.toX = charSnap.x / 100.0f;
                    lerp.toY = charSnap.y / 100.0f;
                    lerp.t = 0.0f;
                    lerp.duration = 0.066f;

                    if (remoteChar->_isFlipped != charSnap.flipped)
                    {
                        remoteChar->_isFlipped = charSnap.flipped;
                        remoteChar->setFlipX(charSnap.flipped);
                    }
                }
            }
        }
    }

    // Process combat events with deduplication
    dispatchCombatEvents(snapshot.combatEvents);
}

void NetworkPresentationAdapter::update(float dt)
{
    if (!_gameLayer)
        return;

    // 1. Decay error offset smoothly for local hero
    if (std::abs(_errorOffsetX) > 0.01f || std::abs(_errorOffsetY) > 0.01f)
    {
        float decayFactor = std::exp(-_errorDecayRate * dt);
        _errorOffsetX *= decayFactor;
        _errorOffsetY *= decayFactor;
    }

    // 2. Advance interpolation for remote heroes
    for (auto &pair : _remoteCharLerp)
    {
        int slot = pair.first;
        auto &lerp = pair.second;
        if (slot >= static_cast<int>(_gameLayer->_CharacterArray.size()))
            continue;
        auto *remoteChar = _gameLayer->_CharacterArray[slot];
        if (!remoteChar || remoteChar->getState() == State::DEAD)
            continue;

        lerp.t += dt;
        float progress = std::clamp(lerp.t / lerp.duration, 0.0f, 1.0f);
        float currentX = lerp.fromX + (lerp.toX - lerp.fromX) * progress;
        float currentY = lerp.fromY + (lerp.toY - lerp.fromY) * progress;
        remoteChar->setPosition(Vec2(currentX, currentY));
    }
}

} // namespace nsv2::network

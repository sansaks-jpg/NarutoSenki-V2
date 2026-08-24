#pragma once

#include "LanProtocol.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class CharacterBase;
class GameLayer;

namespace nsv2::network
{

struct EntityLerpState
{
    float fromX = 0.0f;
    float fromY = 0.0f;
    float toX = 0.0f;
    float toY = 0.0f;
    float t = 0.0f;
    float duration = 0.066f; // ~2 snapshot frames
};

struct PredictedInput
{
    uint32_t sequence = 0;
    uint32_t tick = 0;
    int16_t axisX = 0;
    int16_t axisY = 0;
    float deltaX = 0.0f;
    float deltaY = 0.0f;
};

class NetworkPresentationAdapter
{
public:
    NetworkPresentationAdapter();
    ~NetworkPresentationAdapter() = default;

    void initialize(GameLayer *gameLayer, uint8_t localSlot);
    void reset();

    // Client-side prediction
    void recordPredictedInput(uint32_t sequence, uint32_t tick, int16_t axisX, int16_t axisY, float speed, float dt);
    void pruneAcknowledgedInputs(uint32_t ackSequence);

    // Apply authoritative snapshot from host
    void applyAuthoritativeSnapshot(const StateSnapshot &snapshot, float speed);

    // Update interpolation & error decay smoothing every frame (dt in seconds)
    void update(float dt);

    // Combat event callback registration
    using CombatEventCallback = std::function<void(const CombatEvent &)>;
    void setCombatEventCallback(CombatEventCallback callback) { _eventCallback = std::move(callback); }

    // Helpers
    bool hasRecentEvent(uint32_t eventId) const;

private:
    void reconcileLocalPosition(const CharacterSnapshot &authSnap, float speed);
    void dispatchCombatEvents(const std::vector<CombatEvent> &events);

    GameLayer *_gameLayer = nullptr;
    uint8_t _localSlot = 0;
    uint32_t _lastAppliedTick = 0;

    // Error decay vector for local hero reconciliation
    float _errorOffsetX = 0.0f;
    float _errorOffsetY = 0.0f;
    float _errorDecayRate = 12.0f; // lambda per second

    // Local unacknowledged input buffer for prediction replay
    std::deque<PredictedInput> _unackedInputs;

    // Interpolation states for remote heroes and battlefield units
    std::unordered_map<int, EntityLerpState> _remoteCharLerp;
    std::unordered_map<int, EntityLerpState> _remoteUnitLerp;

    // Event deduplication window (stores recent eventIds)
    std::unordered_set<uint32_t> _processedEventIds;
    std::deque<uint32_t> _eventOrderQueue;

    CombatEventCallback _eventCallback;
};

} // namespace nsv2::network

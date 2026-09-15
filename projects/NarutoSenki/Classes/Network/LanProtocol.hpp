#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nsv2::network
{

// v4 changes control/reliability semantics: delivery acknowledgements are
// explicit MessageType::Ack payloads instead of a cumulative header watermark.
constexpr uint16_t kProtocolVersion = 4;
constexpr uint32_t kProtocolMagic = 0x3256534E; // "NSV2" in little-endian.
// Frame layout: magic(u32) version(u16) type(u16) payloadLen(u32)
//               sequence(u32) tick(u32) ack(u32).
inline constexpr size_t kHeaderBytes = 24;
constexpr uint32_t kMaxPayloadBytes = 64 * 1024;
constexpr uint16_t kDefaultLanPort = 28765;
constexpr uint16_t kDiscoveryPort = 28766;

// Keep message types stable once released; they are part of the wire protocol.
enum class MessageType : uint16_t
{
    Hello = 1,
    RoomAdvertise = 2,
    RoomCreate = 3,
    RoomJoin = 4,
    JoinAccept = 5,
    JoinReject = 6,
    LobbyUpdate = 7,
    SelectHero = 8,
    Ready = 9,
    MatchStart = 10,
    Loaded = 11,
    Input = 12,
    Snapshot = 13,
    Ack = 14,
    Heartbeat = 15,
    Leave = 16,
    Disconnect = 17,
    MatchEnd = 18,
    Resync = 19,
    Error = 20,
    ClientState = 21,
};

enum class ActionType : uint8_t
{
    Move = 1,
    NormalAttack = 2,
    Skill1 = 3,
    Skill2 = 4,
    Skill3 = 5,
    Skill4 = 6,
    Skill5 = 7,
    Item1 = 8,
};

enum class CombatEventType : uint8_t
{
    None = 0,
    AttackConfirmed = 1,
    HitImpact = 2,
    KnockbackApplied = 3,
    CharacterDead = 4,
    CharacterReborn = 5,
    SkillCooldownTriggered = 6,
};

enum class GroupId : uint8_t
{
    Konoha = 0,
    Akatsuki = 1,
};

struct Message
{
    MessageType type = MessageType::Error;
    uint32_t sequence = 0;
    uint32_t tick = 0;
    // Reserved for wire compatibility. v4 uses explicit Ack payloads so a
    // missing lower sequence can never be incorrectly acknowledged by a
    // later packet.
    uint32_t ack = 0;
    std::vector<uint8_t> payload;
};

struct PlayerSlot
{
    uint8_t slot = 0;
    GroupId group = GroupId::Konoha;
    bool ready = false;
    bool remote = false;
    std::string playerName;
    std::string heroName;
};

struct MatchConfig
{
    uint32_t matchId = 0;
    uint32_t mode = 0;
    uint32_t mapId = 1;
    uint32_t seed = 0;
    uint16_t tickRate = 30;
    uint8_t maxPlayers = 2;
    bool enableGear = true;
    bool enableReborn = true;
    std::vector<PlayerSlot> slots;
};

struct InputCommand
{
    uint32_t matchId = 0;
    uint32_t tick = 0;
    uint32_t sequence = 0;
    uint8_t playerSlot = 0;
    ActionType action = ActionType::Move;
    int16_t axisX = 0;
    int16_t axisY = 0;
    bool isDiscreteAction = false;
};

struct CharacterSnapshot
{
    uint8_t slot = 0;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t hp = 0;
    uint32_t ckr = 0;
    uint8_t state = 0;
    bool flipped = false;
};

enum class NetUnitKind : uint8_t
{
    FlogKonoha = 1,
    FlogAkatsuki = 2,
    Tower = 3,
    Guardian = 4,
};

struct UnitSnapshot
{
    uint16_t unitId = 0;
    NetUnitKind kind = NetUnitKind::Tower;
    uint8_t variant = 0;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t hp = 0;
    uint8_t state = 0;
    bool flipped = false;
};

struct CombatEvent
{
    uint32_t eventId = 0;
    uint32_t tick = 0;
    CombatEventType eventType = CombatEventType::None;
    uint8_t sourceSlot = 0;
    uint8_t targetSlot = 0;
    int32_t value = 0;
    int16_t posX = 0;
    int16_t posY = 0;
};

struct StateSnapshot
{
    uint32_t matchId = 0;
    uint32_t tick = 0;
    uint16_t elapsedSeconds = 0;
    uint32_t sessionEpoch = 0;
    uint32_t clientSequenceWatermark = 0;
    uint32_t stateChecksum = 0;
    std::vector<CharacterSnapshot> characters;
    std::vector<UnitSnapshot> units;
    std::vector<CombatEvent> combatEvents;
};

struct RoomAdvertisement
{
    std::string address;
    std::string roomId;
    std::string roomName;
    std::string hostName;
    uint16_t port = kDefaultLanPort;
    uint8_t playerCount = 0;
    uint8_t maxPlayers = 2;
    uint32_t mode = 0;
    uint32_t mapId = 1;
};

// A stable per-match generation id. Do not derive this as seed ^ matchId:
// MatchConfig currently derives seed from matchId, which made that expression
// constant and allowed stale packets from previous matches to look current.
inline uint32_t computeSessionEpoch(uint32_t matchId, uint32_t seed)
{
    uint32_t x = matchId + 0x9E3779B9u;
    x ^= seed + 0x85EBCA6Bu + (x << 6) + (x >> 2);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x == 0 ? 1u : x;
}

uint32_t computeStateChecksum(const StateSnapshot &snapshot);

bool encodeMessage(const Message &message, std::vector<uint8_t> &out, std::string *error = nullptr);
bool decodeMessage(const uint8_t *data, size_t size, Message &out, size_t &consumed, std::string *error = nullptr);
inline bool decodeMessage(const std::vector<uint8_t> &data, Message &out, std::string *error = nullptr)
{
    size_t consumed = 0;
    return decodeMessage(data.data(), data.size(), out, consumed, error) && consumed == data.size();
}

bool encodeMatchConfig(const MatchConfig &config, std::vector<uint8_t> &out, std::string *error = nullptr);
bool decodeMatchConfig(const std::vector<uint8_t> &data, MatchConfig &out, std::string *error = nullptr);

bool encodeInputCommand(const InputCommand &command, std::vector<uint8_t> &out, std::string *error = nullptr);
bool decodeInputCommand(const std::vector<uint8_t> &data, InputCommand &out, std::string *error = nullptr);

bool encodeRoomAdvertisement(const RoomAdvertisement &room, std::vector<uint8_t> &out, std::string *error = nullptr);
bool decodeRoomAdvertisement(const std::vector<uint8_t> &data, RoomAdvertisement &out, std::string *error = nullptr);

bool encodeStateSnapshot(const StateSnapshot &snapshot, std::vector<uint8_t> &out, std::string *error = nullptr);
bool decodeStateSnapshot(const std::vector<uint8_t> &data, StateSnapshot &out, std::string *error = nullptr);

} // namespace nsv2::network

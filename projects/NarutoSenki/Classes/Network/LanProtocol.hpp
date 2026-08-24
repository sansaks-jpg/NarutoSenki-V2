#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nsv2::network
{

constexpr uint16_t kProtocolVersion = 2;
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
    // Client -> Host authoritative state of the client-owned hero (v2).
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
    // Piggybacked cumulative ack: highest remote input sequence the sender has
    // processed. Used to clear the reliable-retransmit queue.
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

// Non-hero battlefield entities mirrored from the host simulation.
// unitId namespaces: towers = 100 + charId, guardian = 200, flogs = 300+.
enum class NetUnitKind : uint8_t
{
    FlogKonoha = 1,
    FlogAkatsuki = 2,
    Tower = 3,
    Guardian = 4,
};

struct UnitSnapshot
{
    // uint16 namespaces: towers = charId (1..), guardian = 200, flogs = 300+.
    uint16_t unitId = 0;
    NetUnitKind kind = NetUnitKind::Tower;
    // Flogs: index into the shared flog-name table. Guardian:
    // bit0 = name (0 Roshi / 1 Han), bit1 = group (0 Konoha / 1 Akatsuki).
    uint8_t variant = 0;
    int32_t x = 0;
    int32_t y = 0;
    uint32_t hp = 0;
    uint8_t state = 0;
    bool flipped = false;
};

struct StateSnapshot
{
    uint32_t matchId = 0;
    uint32_t tick = 0;
    // Match clock in seconds as tracked by the snapshot sender (host).
    uint16_t elapsedSeconds = 0;
    std::vector<CharacterSnapshot> characters;
    std::vector<UnitSnapshot> units;
};

struct RoomAdvertisement
{
    // Filled by discovery from the UDP source address; not serialized in the payload.
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

// Encodes a complete length-delimited frame suitable for TCP or a reliable packet.
bool encodeMessage(const Message &message, std::vector<uint8_t> &out, std::string *error = nullptr);

// Decodes one frame from bytes. `consumed` allows a TCP stream parser to retain trailing bytes.
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

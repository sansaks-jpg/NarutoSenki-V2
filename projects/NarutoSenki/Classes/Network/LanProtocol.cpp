#include "LanProtocol.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace nsv2::network
{
namespace
{
constexpr size_t kHeaderBytes = 20;
constexpr uint16_t kMaxStringBytes = 1024;
constexpr uint8_t kMaxSlots = 8;

void fail(std::string *error, const char *message)
{
    if (error)
        *error = message;
}

void putU8(std::vector<uint8_t> &out, uint8_t value)
{
    out.push_back(value);
}

void putU16(std::vector<uint8_t> &out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void putU32(std::vector<uint8_t> &out, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
}

void putI16(std::vector<uint8_t> &out, int16_t value)
{
    putU16(out, static_cast<uint16_t>(value));
}

void putI32(std::vector<uint8_t> &out, int32_t value)
{
    putU32(out, static_cast<uint32_t>(value));
}

bool putString(std::vector<uint8_t> &out, const std::string &value, std::string *error)
{
    if (value.size() > kMaxStringBytes)
    {
        fail(error, "string field exceeds protocol limit");
        return false;
    }
    putU16(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

class Reader
{
public:
    Reader(const uint8_t *data, size_t size) : _data(data), _size(size) {}

    bool u8(uint8_t &out)
    {
        if (_offset + 1 > _size)
            return false;
        out = _data[_offset++];
        return true;
    }

    bool u16(uint16_t &out)
    {
        if (_offset + 2 > _size)
            return false;
        out = static_cast<uint16_t>(_data[_offset]) |
              static_cast<uint16_t>(_data[_offset + 1] << 8);
        _offset += 2;
        return true;
    }

    bool u32(uint32_t &out)
    {
        if (_offset + 4 > _size)
            return false;
        out = static_cast<uint32_t>(_data[_offset]) |
              (static_cast<uint32_t>(_data[_offset + 1]) << 8) |
              (static_cast<uint32_t>(_data[_offset + 2]) << 16) |
              (static_cast<uint32_t>(_data[_offset + 3]) << 24);
        _offset += 4;
        return true;
    }

    bool i16(int16_t &out)
    {
        uint16_t raw = 0;
        if (!u16(raw))
            return false;
        out = static_cast<int16_t>(raw);
        return true;
    }

    bool i32(int32_t &out)
    {
        uint32_t raw = 0;
        if (!u32(raw))
            return false;
        out = static_cast<int32_t>(raw);
        return true;
    }

    bool string(std::string &out)
    {
        uint16_t length = 0;
        if (!u16(length) || length > kMaxStringBytes || _offset + length > _size)
            return false;
        out.assign(reinterpret_cast<const char *>(_data + _offset), length);
        _offset += length;
        return true;
    }

    size_t remaining() const { return _size - _offset; }

private:
    const uint8_t *_data = nullptr;
    size_t _size = 0;
    size_t _offset = 0;
};

bool validMessageType(uint16_t raw)
{
    return raw >= static_cast<uint16_t>(MessageType::Hello) &&
           raw <= static_cast<uint16_t>(MessageType::Error);
}

bool validAction(uint8_t raw)
{
    return raw >= static_cast<uint8_t>(ActionType::Move) &&
           raw <= static_cast<uint8_t>(ActionType::NormalAttack);
}

bool decodeReaderResult(Reader &reader, std::string *error)
{
    if (reader.remaining() != 0)
    {
        fail(error, "payload contains trailing bytes");
        return false;
    }
    return true;
}
} // namespace

bool encodeMessage(const Message &message, std::vector<uint8_t> &out, std::string *error)
{
    if (!validMessageType(static_cast<uint16_t>(message.type)))
    {
        fail(error, "unknown message type");
        return false;
    }
    if (message.payload.size() > kMaxPayloadBytes)
    {
        fail(error, "payload exceeds protocol limit");
        return false;
    }

    out.clear();
    out.reserve(kHeaderBytes + message.payload.size());
    putU32(out, kProtocolMagic);
    putU16(out, kProtocolVersion);
    putU16(out, static_cast<uint16_t>(message.type));
    putU32(out, static_cast<uint32_t>(message.payload.size()));
    putU32(out, message.sequence);
    putU32(out, message.tick);
    out.insert(out.end(), message.payload.begin(), message.payload.end());
    return true;
}

bool decodeMessage(const uint8_t *data, size_t size, Message &out, size_t &consumed, std::string *error)
{
    consumed = 0;
    if (!data || size < kHeaderBytes)
    {
        fail(error, "incomplete message header");
        return false;
    }

    Reader reader(data, size);
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t rawType = 0;
    uint32_t payloadLength = 0;
    if (!reader.u32(magic) || !reader.u16(version) || !reader.u16(rawType) ||
        !reader.u32(payloadLength) || !reader.u32(out.sequence) || !reader.u32(out.tick))
    {
        fail(error, "incomplete message header");
        return false;
    }
    if (magic != kProtocolMagic)
    {
        fail(error, "invalid protocol magic");
        return false;
    }
    if (version != kProtocolVersion)
    {
        fail(error, "unsupported protocol version");
        return false;
    }
    if (!validMessageType(rawType))
    {
        fail(error, "unknown message type");
        return false;
    }
    if (payloadLength > kMaxPayloadBytes)
    {
        fail(error, "payload exceeds protocol limit");
        return false;
    }
    const size_t frameSize = kHeaderBytes + static_cast<size_t>(payloadLength);
    if (size < frameSize)
    {
        fail(error, "incomplete message payload");
        return false;
    }

    out.type = static_cast<MessageType>(rawType);
    out.payload.assign(data + kHeaderBytes, data + frameSize);
    consumed = frameSize;
    return true;
}

bool encodeMatchConfig(const MatchConfig &config, std::vector<uint8_t> &out, std::string *error)
{
    if (config.slots.size() > kMaxSlots || config.maxPlayers == 0 || config.maxPlayers > kMaxSlots)
    {
        fail(error, "invalid match slot count");
        return false;
    }
    out.clear();
    putU32(out, config.matchId);
    putU32(out, config.mode);
    putU32(out, config.mapId);
    putU32(out, config.seed);
    putU16(out, config.tickRate);
    putU8(out, config.maxPlayers);
    putU8(out, config.enableGear ? 1 : 0);
    putU8(out, config.enableReborn ? 1 : 0);
    putU8(out, static_cast<uint8_t>(config.slots.size()));
    for (const auto &slot : config.slots)
    {
        putU8(out, slot.slot);
        putU8(out, static_cast<uint8_t>(slot.group));
        putU8(out, slot.ready ? 1 : 0);
        putU8(out, slot.remote ? 1 : 0);
        if (!putString(out, slot.playerName, error) || !putString(out, slot.heroName, error))
            return false;
    }
    return out.size() <= kMaxPayloadBytes || (fail(error, "match config exceeds protocol limit"), false);
}

bool decodeMatchConfig(const std::vector<uint8_t> &data, MatchConfig &out, std::string *error)
{
    Reader reader(data.data(), data.size());
    uint8_t maxPlayers = 0;
    uint8_t enableGear = 0;
    uint8_t enableReborn = 0;
    uint8_t slotCount = 0;
    if (!reader.u32(out.matchId) || !reader.u32(out.mode) || !reader.u32(out.mapId) ||
        !reader.u32(out.seed) || !reader.u16(out.tickRate) || !reader.u8(maxPlayers) ||
        !reader.u8(enableGear) || !reader.u8(enableReborn) || !reader.u8(slotCount))
    {
        fail(error, "incomplete match config");
        return false;
    }
    if (maxPlayers == 0 || maxPlayers > kMaxSlots || slotCount > kMaxSlots || slotCount > maxPlayers ||
        out.tickRate == 0 || enableGear > 1 || enableReborn > 1)
    {
        fail(error, "invalid match config");
        return false;
    }

    out.maxPlayers = maxPlayers;
    out.enableGear = enableGear != 0;
    out.enableReborn = enableReborn != 0;
    out.slots.clear();
    out.slots.reserve(slotCount);
    for (uint8_t i = 0; i < slotCount; ++i)
    {
        PlayerSlot slot;
        uint8_t rawGroup = 0;
        uint8_t ready = 0;
        uint8_t remote = 0;
        if (!reader.u8(slot.slot) || !reader.u8(rawGroup) || !reader.u8(ready) || !reader.u8(remote) ||
            !reader.string(slot.playerName) || !reader.string(slot.heroName))
        {
            fail(error, "incomplete player slot");
            return false;
        }
        if (rawGroup > static_cast<uint8_t>(GroupId::Akatsuki) || ready > 1 || remote > 1)
        {
            fail(error, "invalid player slot");
            return false;
        }
        slot.group = static_cast<GroupId>(rawGroup);
        slot.ready = ready != 0;
        slot.remote = remote != 0;
        out.slots.push_back(std::move(slot));
    }
    return decodeReaderResult(reader, error);
}

bool encodeInputCommand(const InputCommand &command, std::vector<uint8_t> &out, std::string *error)
{
    if (!validAction(static_cast<uint8_t>(command.action)))
    {
        fail(error, "unknown input action");
        return false;
    }
    out.clear();
    putU32(out, command.matchId);
    putU32(out, command.tick);
    putU32(out, command.sequence);
    putU8(out, command.playerSlot);
    putU8(out, static_cast<uint8_t>(command.action));
    putI16(out, command.axisX);
    putI16(out, command.axisY);
    return true;
}

bool decodeInputCommand(const std::vector<uint8_t> &data, InputCommand &out, std::string *error)
{
    Reader reader(data.data(), data.size());
    uint8_t rawAction = 0;
    if (!reader.u32(out.matchId) || !reader.u32(out.tick) || !reader.u32(out.sequence) ||
        !reader.u8(out.playerSlot) || !reader.u8(rawAction) || !reader.i16(out.axisX) || !reader.i16(out.axisY))
    {
        fail(error, "incomplete input command");
        return false;
    }
    if (!validAction(rawAction))
    {
        fail(error, "unknown input action");
        return false;
    }
    out.action = static_cast<ActionType>(rawAction);
    return decodeReaderResult(reader, error);
}

bool encodeRoomAdvertisement(const RoomAdvertisement &room, std::vector<uint8_t> &out, std::string *error)
{
    if (room.port == 0 || room.maxPlayers == 0 || room.playerCount > room.maxPlayers)
    {
        fail(error, "invalid room advertisement");
        return false;
    }
    out.clear();
    if (!putString(out, room.roomId, error) || !putString(out, room.roomName, error) ||
        !putString(out, room.hostName, error))
        return false;
    putU16(out, room.port);
    putU8(out, room.playerCount);
    putU8(out, room.maxPlayers);
    putU32(out, room.mode);
    putU32(out, room.mapId);
    return out.size() <= kMaxPayloadBytes || (fail(error, "room advertisement exceeds protocol limit"), false);
}

bool encodeStateSnapshot(const StateSnapshot &snapshot, std::vector<uint8_t> &out, std::string *error)
{
    if (snapshot.characters.size() > kMaxSlots)
    {
        fail(error, "snapshot entity count exceeds protocol limit");
        return false;
    }
    out.clear();
    putU32(out, snapshot.matchId);
    putU32(out, snapshot.tick);
    putU8(out, static_cast<uint8_t>(snapshot.characters.size()));
    for (const auto &character : snapshot.characters)
    {
        putU8(out, character.slot);
        putI32(out, character.x);
        putI32(out, character.y);
        putU32(out, character.hp);
        putU32(out, character.ckr);
        putU8(out, character.state);
        putU8(out, character.flipped ? 1 : 0);
    }
    return out.size() <= kMaxPayloadBytes || (fail(error, "snapshot exceeds protocol limit"), false);
}

bool decodeStateSnapshot(const std::vector<uint8_t> &data, StateSnapshot &out, std::string *error)
{
    Reader reader(data.data(), data.size());
    uint8_t count = 0;
    if (!reader.u32(out.matchId) || !reader.u32(out.tick) || !reader.u8(count) || count > kMaxSlots)
    {
        fail(error, "invalid snapshot header");
        return false;
    }
    out.characters.clear();
    out.characters.reserve(count);
    for (uint8_t i = 0; i < count; ++i)
    {
        CharacterSnapshot character;
        uint8_t flipped = 0;
        if (!reader.u8(character.slot) || !reader.i32(character.x) || !reader.i32(character.y) ||
            !reader.u32(character.hp) || !reader.u32(character.ckr) || !reader.u8(character.state) ||
            !reader.u8(flipped) || flipped > 1)
        {
            fail(error, "incomplete snapshot entity");
            return false;
        }
        character.flipped = flipped != 0;
        out.characters.push_back(character);
    }
    return decodeReaderResult(reader, error);
}

bool decodeRoomAdvertisement(const std::vector<uint8_t> &data, RoomAdvertisement &out, std::string *error)
{
    Reader reader(data.data(), data.size());
    if (!reader.string(out.roomId) || !reader.string(out.roomName) || !reader.string(out.hostName) ||
        !reader.u16(out.port) || !reader.u8(out.playerCount) || !reader.u8(out.maxPlayers) ||
        !reader.u32(out.mode) || !reader.u32(out.mapId))
    {
        fail(error, "incomplete room advertisement");
        return false;
    }
    if (out.port == 0 || out.maxPlayers == 0 || out.playerCount > out.maxPlayers)
    {
        fail(error, "invalid room advertisement");
        return false;
    }
    return decodeReaderResult(reader, error);
}

} // namespace nsv2::network

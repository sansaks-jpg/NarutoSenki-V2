#include "Network/LanProtocol.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using namespace nsv2::network;

namespace
{
void testMessageRoundTrip()
{
    Message source;
    source.type = MessageType::Input;
    source.sequence = 12;
    source.tick = 30;
    source.payload = {1, 2, 3, 4};

    std::vector<uint8_t> bytes;
    std::string error;
    assert(encodeMessage(source, bytes, &error));

    Message decoded;
    size_t consumed = 0;
    assert(decodeMessage(bytes.data(), bytes.size(), decoded, consumed, &error));
    assert(consumed == bytes.size());
    assert(decoded.type == source.type);
    assert(decoded.sequence == source.sequence);
    assert(decoded.tick == source.tick);
    assert(decoded.payload == source.payload);
}

void testMatchConfigRoundTrip()
{
    MatchConfig source;
    source.matchId = 100;
    source.mode = 0;
    source.mapId = 2;
    source.seed = 12345;
    source.tickRate = 30;
    source.slots = {
        {0, GroupId::Konoha, true, false, "Host", "Naruto"},
        {1, GroupId::Akatsuki, true, true, "Client", "Sasuke"},
    };

    std::vector<uint8_t> bytes;
    std::string error;
    assert(encodeMatchConfig(source, bytes, &error));

    MatchConfig decoded;
    assert(decodeMatchConfig(bytes, decoded, &error));
    assert(decoded.matchId == source.matchId);
    assert(decoded.mapId == source.mapId);
    assert(decoded.seed == source.seed);
    assert(decoded.slots.size() == 2);
    assert(decoded.slots[1].heroName == "Sasuke");
    assert(decoded.slots[1].remote);
}

void testSnapshotRoundTrip()
{
    StateSnapshot source;
    source.matchId = 42;
    source.tick = 90;
    source.characters = {
        {0, 1234, 5678, 900, 120, 2, false},
        {1, 4321, 8765, 700, 80, 3, true},
    };

    std::vector<uint8_t> bytes;
    std::string error;
    assert(encodeStateSnapshot(source, bytes, &error));

    StateSnapshot decoded;
    assert(decodeStateSnapshot(bytes, decoded, &error));
    assert(decoded.matchId == source.matchId);
    assert(decoded.tick == source.tick);
    assert(decoded.characters.size() == 2);
    assert(decoded.characters[1].x == 4321);
    assert(decoded.characters[1].flipped);
}

void testInputRoundTrip()
{
    InputCommand source;
    source.matchId = 7;
    source.tick = 42;
    source.sequence = 9;
    source.playerSlot = 1;
    source.action = ActionType::Move;
    source.axisX = -1000;
    source.axisY = 1000;

    std::vector<uint8_t> bytes;
    std::string error;
    assert(encodeInputCommand(source, bytes, &error));

    InputCommand decoded;
    assert(decodeInputCommand(bytes, decoded, &error));
    assert(decoded.matchId == source.matchId);
    assert(decoded.tick == source.tick);
    assert(decoded.playerSlot == source.playerSlot);
    assert(decoded.axisX == source.axisX);
    assert(decoded.axisY == source.axisY);
}

void testRejectsOversizedPayload()
{
    Message source;
    source.type = MessageType::Hello;
    source.payload.assign(kMaxPayloadBytes + 1, 0xAB);
    std::vector<uint8_t> bytes;
    std::string error;
    assert(!encodeMessage(source, bytes, &error));
    assert(error == "payload exceeds protocol limit");

    bytes.assign(20 + kMaxPayloadBytes + 1, 0);
    bytes[0] = 0x4E;
    bytes[1] = 0x53;
    bytes[2] = 0x56;
    bytes[3] = 0x32;
    bytes[4] = static_cast<uint8_t>(kProtocolVersion & 0xFF);
    bytes[5] = static_cast<uint8_t>(kProtocolVersion >> 8);
    bytes[6] = static_cast<uint8_t>(MessageType::Hello);
    bytes[8] = static_cast<uint8_t>((kMaxPayloadBytes + 1) & 0xFF);
    bytes[9] = static_cast<uint8_t>((kMaxPayloadBytes + 1) >> 8);
    bytes[10] = static_cast<uint8_t>((kMaxPayloadBytes + 1) >> 16);
    bytes[11] = static_cast<uint8_t>((kMaxPayloadBytes + 1) >> 24);

    Message decoded;
    size_t consumed = 0;
    assert(!decodeMessage(bytes.data(), bytes.size(), decoded, consumed, &error));
    assert(error == "payload exceeds protocol limit");
}

void testRejectsUnknownTypeAndVersion()
{
    Message source;
    source.type = MessageType::Hello;
    source.payload = {1};
    std::vector<uint8_t> bytes;
    std::string error;
    assert(encodeMessage(source, bytes, &error));

    Message decoded;
    size_t consumed = 0;
    bytes[6] = 0xFF;
    bytes[7] = 0xFF;
    assert(!decodeMessage(bytes.data(), bytes.size(), decoded, consumed, &error));
    assert(error == "unknown message type");

    assert(encodeMessage(source, bytes, &error));
    bytes[4] = 0xFF;
    bytes[5] = 0xFF;
    assert(!decodeMessage(bytes.data(), bytes.size(), decoded, consumed, &error));
    assert(error == "unsupported protocol version");
}

void testRejectsMalformedFrames()
{
    Message decoded;
    std::string error;
    size_t consumed = 0;
    std::vector<uint8_t> tooShort = {0x4E, 0x53};
    assert(!decodeMessage(tooShort.data(), tooShort.size(), decoded, consumed, &error));

    Message source;
    source.type = MessageType::Hello;
    source.payload = {1};
    std::vector<uint8_t> bytes;
    assert(encodeMessage(source, bytes, &error));
    bytes.push_back(99);
    assert(decodeMessage(bytes.data(), bytes.size(), decoded, consumed, &error));
    assert(consumed == bytes.size() - 1);
}
} // namespace

int main()
{
    testMessageRoundTrip();
    testMatchConfigRoundTrip();
    testInputRoundTrip();
    testSnapshotRoundTrip();
    testRejectsOversizedPayload();
    testRejectsUnknownTypeAndVersion();
    testRejectsMalformedFrames();
    std::cout << "lan_protocol_test: ok\n";
    return 0;
}

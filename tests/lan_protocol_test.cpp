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
    testRejectsMalformedFrames();
    std::cout << "lan_protocol_test: ok\n";
    return 0;
}

// Protocol unit tests for the LAN multiplayer wire format (v2).
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

void testMessageAckRoundTrip()
{
    // v2: every frame carries a piggybacked cumulative input ack.
    Message source;
    source.type = MessageType::ClientState;
    source.sequence = 7;
    source.tick = 9;
    source.ack = 42;
    source.payload = {1, 2, 3};

    std::vector<uint8_t> bytes;
    std::string error;
    assert(encodeMessage(source, bytes, &error));

    Message decoded;
    size_t consumed = 0;
    assert(decodeMessage(bytes.data(), bytes.size(), decoded, consumed, &error));
    assert(consumed == bytes.size());
    assert(decoded.type == MessageType::ClientState);
    assert(decoded.sequence == 7 && decoded.tick == 9 && decoded.ack == 42);
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
    assert(decoded.elapsedSeconds == 0);
    assert(decoded.units.empty());
    assert(decoded.characters.size() == 2);
    assert(decoded.characters[1].x == 4321);
    assert(decoded.characters[1].flipped);
}

void testSnapshotUnitsRoundTrip()
{
    // v2: snapshots carry battlefield units and the match clock.
    StateSnapshot source;
    source.matchId = 1234;
    source.tick = 30;
    source.elapsedSeconds = 125;
    source.characters.push_back({0, 280000, 8000, 50000, 12000, 3, false});
    source.units.push_back({1, NetUnitKind::Tower, 0, 500, 600, 40000, 0, false});
    source.units.push_back({200, NetUnitKind::Guardian, static_cast<uint8_t>(1 | 2), 272, 80, 9999, 4, true});
    source.units.push_back({305, NetUnitKind::FlogAkatsuki, 4, 2656, 120, 300, 1, false});

    std::vector<uint8_t> bytes;
    std::string error;
    assert(encodeStateSnapshot(source, bytes, &error));

    StateSnapshot decoded;
    assert(decodeStateSnapshot(bytes, decoded, &error));
    assert(decoded.elapsedSeconds == 125);
    assert(decoded.units.size() == 3);
    assert(decoded.units[0].unitId == 1 && decoded.units[0].kind == NetUnitKind::Tower);
    assert(decoded.units[1].unitId == 200 && decoded.units[1].kind == NetUnitKind::Guardian);
    assert(decoded.units[1].variant == 3); // Han (bit0) + Akatsuki (bit1)
    assert(decoded.units[1].flipped);
    assert(decoded.units[2].unitId == 305 && decoded.units[2].kind == NetUnitKind::FlogAkatsuki);

    // Truncated payload must fail cleanly instead of crashing.
    StateSnapshot bad;
    assert(!decodeStateSnapshot(std::vector<uint8_t>(bytes.begin(), bytes.begin() + 10), bad, &error));
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
    source.isDiscreteAction = true;

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
    assert(decoded.isDiscreteAction == true);

    // Test Skill actions
    source.action = ActionType::Skill1;
    source.isDiscreteAction = false;
    assert(encodeInputCommand(source, bytes, &error));
    assert(decodeInputCommand(bytes, decoded, &error));
    assert(decoded.action == ActionType::Skill1);
    assert(!decoded.isDiscreteAction);

    source.action = ActionType::Item1;
    assert(encodeInputCommand(source, bytes, &error));
    assert(decodeInputCommand(bytes, decoded, &error));
    assert(decoded.action == ActionType::Item1);
}

void testCombatEventsAndChecksumRoundTrip()
{
    StateSnapshot source;
    source.matchId = 99;
    source.tick = 100;
    source.elapsedSeconds = 45;
    source.sessionEpoch = 123456;
    source.clientSequenceWatermark = 77;
    source.characters.push_back({0, 1000, 2000, 5000, 100, 1, false});
    source.combatEvents.push_back({1, 100, CombatEventType::HitImpact, 0, 1, -250, 120, 30});
    source.combatEvents.push_back({2, 100, CombatEventType::KnockbackApplied, 0, 1, 15, 120, 30});
    source.stateChecksum = computeStateChecksum(source);
    assert(source.stateChecksum != 0);

    std::vector<uint8_t> bytes;
    std::string error;
    assert(encodeStateSnapshot(source, bytes, &error));

    StateSnapshot decoded;
    assert(decodeStateSnapshot(bytes, decoded, &error));
    assert(decoded.matchId == 99);
    assert(decoded.sessionEpoch == 123456);
    assert(decoded.clientSequenceWatermark == 77);
    assert(decoded.stateChecksum == source.stateChecksum);
    assert(decoded.combatEvents.size() == 2);
    assert(decoded.combatEvents[0].eventId == 1);
    assert(decoded.combatEvents[0].eventType == CombatEventType::HitImpact);
    assert(decoded.combatEvents[0].value == -250);
    assert(decoded.combatEvents[1].eventType == CombatEventType::KnockbackApplied);
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

    bytes.assign(kHeaderBytes + kMaxPayloadBytes + 1, 0);
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

void testAcceptsNewestMessageType()
{
    // v3 boundary: ClientState is the newest valid type; Error+1 must fail.
    Message source;
    source.type = MessageType::ClientState;
    source.payload = {1};
    std::vector<uint8_t> bytes;
    std::string error;
    assert(encodeMessage(source, bytes, &error));
    Message decoded;
    size_t consumed = 0;
    assert(decodeMessage(bytes.data(), bytes.size(), decoded, consumed, &error));

    assert(encodeMessage(source, bytes, &error));
    const uint16_t invalidType = static_cast<uint16_t>(MessageType::ClientState) + 1;
    bytes[6] = static_cast<uint8_t>(invalidType & 0xFF);
    bytes[7] = static_cast<uint8_t>(invalidType >> 8);
    assert(!decodeMessage(bytes.data(), bytes.size(), decoded, consumed, &error));
    assert(error == "unknown message type");
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
    testMessageAckRoundTrip();
    testMatchConfigRoundTrip();
    testInputRoundTrip();
    testSnapshotRoundTrip();
    testSnapshotUnitsRoundTrip();
    testCombatEventsAndChecksumRoundTrip();
    testRejectsOversizedPayload();
    testRejectsUnknownTypeAndVersion();
    testAcceptsNewestMessageType();
    testRejectsMalformedFrames();
    std::cout << "lan_protocol_test: ok (v3)\n";
    return 0;
}

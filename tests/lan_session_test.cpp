// Session integration tests: loopback host/client lifecycle + protocol v4 reliability/authority flow.
#include "Network/LanSession.hpp"

#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

using namespace nsv2::network;

namespace
{
int gWaitCheckpoint = 0;

void waitFor(LanSession &host, LanSession &client, const std::function<bool()> &condition, int timeoutMs)
{
    const int checkpoint = ++gWaitCheckpoint;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        host.poll();
        client.poll();
        if (condition())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cerr << "waitFor TIMEOUT checkpoint=" << checkpoint
              << ": host state=" << static_cast<int>(host.state())
              << ", client state=" << static_cast<int>(client.state())
              << ", host remoteConn=" << host.remoteConnected()
              << ", client remoteConn=" << client.remoteConnected() << std::endl;
    SessionDiagnostics hd;
    SessionDiagnostics cd;
    host.getDiagnostics(hd);
    client.getDiagnostics(cd);
    std::cerr << "host diag: match=" << hd.matchId << " ack=" << hd.lastAckedByRemote
              << " remoteSeq=" << hd.remoteSequenceWatermark
              << " pending=" << hd.pendingReliableCount << std::endl;
    std::cerr << "client diag: match=" << cd.matchId << " ack=" << cd.lastAckedByRemote
              << " remoteSeq=" << cd.remoteSequenceWatermark
              << " pending=" << cd.pendingReliableCount << std::endl;
    assert(condition());
}

void pump(LanSession &a, LanSession &b, int milliseconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        a.poll();
        b.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(3));
    }
}

void testOfflineDoesNotOpenSocket()
{
    LanSession offline;
    assert(offline.state() == SessionState::Idle);
    assert(!offline.networkActive());
    offline.poll();
    std::vector<RoomAdvertisement> rooms;
    offline.getRooms(rooms);
    assert(rooms.empty());
    assert(!offline.networkActive());
}

void testSessionEpochIsPerMatch()
{
    const uint32_t epochA = computeSessionEpoch(100, 200);
    const uint32_t epochB = computeSessionEpoch(101, 201);
    assert(epochA != 0);
    assert(epochB != 0);
    assert(epochA != epochB);
}
} // namespace

int main()
{
    testOfflineDoesNotOpenSocket();
    testSessionEpochIsPerMatch();

    LanSession host;
    LanSession client;
    std::string error;
    assert(host.host("Loopback Room", "Host", 29876, &error));
    assert(client.join("127.0.0.1", 29876, "Client", &error));

    waitFor(host, client, [&]() {
        return host.state() == SessionState::Lobby && client.state() == SessionState::Lobby &&
               host.remoteConnected() && client.remoteConnected();
    }, 4000);

    assert(!host.matchConfig().enableGear);
    assert(!client.matchConfig().enableGear);

    assert(host.setLocalHero("Naruto"));
    assert(client.setLocalHero("Sasuke"));
    waitFor(host, client, [&]() {
        return host.matchConfig().slots.size() == 2 &&
               client.matchConfig().slots.size() == 2 &&
               host.matchConfig().slots[1].heroName == "Sasuke" &&
               client.matchConfig().slots[1].heroName == "Sasuke";
    }, 3000);

    assert(host.setLocalReady(true));
    assert(client.setLocalReady(true));
    waitFor(host, client, [&]() {
        return host.matchConfig().slots[0].ready && host.matchConfig().slots[1].ready;
    }, 3000);

    assert(host.startMatch(&error));
    waitFor(host, client, [&]() {
        return host.state() == SessionState::Loading && client.state() == SessionState::Loading;
    }, 3000);

    assert(client.markLoaded(&error));
    pump(host, client, 100);
    assert(host.state() == SessionState::Loading);
    assert(client.state() == SessionState::Loading);
    assert(host.markLoaded(&error));
    waitFor(host, client, [&]() {
        return host.state() == SessionState::Battle && client.state() == SessionState::Battle;
    }, 3000);

    InputCommand input;
    input.tick = 1;
    input.action = ActionType::Move;
    input.axisX = 1000;
    assert(client.submitInput(input, &error));
    bool inputReceived = false;
    waitFor(host, client, [&]() {
        std::vector<InputCommand> commands;
        host.drainInputCommands(commands);
        for (const auto &c : commands)
        {
            if (c.playerSlot == 1 && c.sequence > 0)
                inputReceived = true;
        }
        return inputReceived;
    }, 3000);
    assert(inputReceived);

    InputCommand newestMove;
    newestMove.sequence = 50;
    newestMove.tick = 3;
    newestMove.action = ActionType::Move;
    newestMove.axisX = 500;
    assert(client.submitInput(newestMove, &error));
    bool newestMoveReceived = false;
    waitFor(host, client, [&]() {
        std::vector<InputCommand> cmds;
        host.drainInputCommands(cmds);
        for (const auto &c : cmds)
            newestMoveReceived = newestMoveReceived || c.sequence == 50;
        return newestMoveReceived;
    }, 3000);

    InputCommand staleMove = newestMove;
    staleMove.sequence = 49;
    staleMove.tick = 2;
    staleMove.axisX = -500;
    assert(client.submitInput(staleMove, &error));
    pump(host, client, 250);
    {
        std::vector<InputCommand> cmds;
        host.drainInputCommands(cmds);
        for (const auto &c : cmds)
            assert(c.sequence != 49);
    }

    InputCommand discreteHigh;
    discreteHigh.sequence = 60;
    discreteHigh.tick = 5;
    discreteHigh.action = ActionType::Skill1;
    assert(client.submitInput(discreteHigh, &error));
    bool highReceived = false;
    waitFor(host, client, [&]() {
        std::vector<InputCommand> cmds;
        host.drainInputCommands(cmds);
        for (const auto &c : cmds)
            highReceived = highReceived || c.sequence == 60;
        return highReceived;
    }, 3000);

    InputCommand discreteGap = discreteHigh;
    discreteGap.sequence = 59;
    discreteGap.tick = 4;
    discreteGap.action = ActionType::Skill2;
    assert(client.submitInput(discreteGap, &error));
    bool gapFilled = false;
    waitFor(host, client, [&]() {
        std::vector<InputCommand> cmds;
        host.drainInputCommands(cmds);
        for (const auto &c : cmds)
            gapFilled = gapFilled || (c.sequence == 59 && c.action == ActionType::Skill2);
        return gapFilled;
    }, 3000);
    assert(gapFilled);

    InputCommand duplicate = discreteHigh;
    duplicate.tick = 6;
    assert(client.submitInput(duplicate, &error));
    pump(host, client, 250);
    {
        std::vector<InputCommand> cmds;
        host.drainInputCommands(cmds);
        for (const auto &c : cmds)
            assert(c.sequence != 60);
    }

    ActionType testActions[] = {
        ActionType::NormalAttack,
        ActionType::Skill1,
        ActionType::Skill2,
        ActionType::Skill3,
        ActionType::Skill4,
        ActionType::Skill5,
        ActionType::Item1
    };
    for (size_t i = 0; i < sizeof(testActions) / sizeof(testActions[0]); ++i)
    {
        InputCommand skillCmd;
        skillCmd.sequence = 70 + static_cast<uint32_t>(i);
        skillCmd.tick = 10 + static_cast<uint32_t>(i);
        skillCmd.action = testActions[i];
        assert(client.submitInput(skillCmd, &error));
        bool received = false;
        waitFor(host, client, [&]() {
            std::vector<InputCommand> cmds;
            host.drainInputCommands(cmds);
            for (const auto &c : cmds)
            {
                if (c.sequence == skillCmd.sequence && c.action == testActions[i])
                    received = true;
            }
            return received;
        }, 3000);
        assert(received);
    }

    StateSnapshot snapshot;
    snapshot.matchId = host.matchConfig().matchId;
    snapshot.tick = 2;
    snapshot.elapsedSeconds = 33;
    snapshot.characters.push_back({1, 100, 200, 900, 100, 2, false});
    snapshot.units.push_back({1, NetUnitKind::Tower, 0, 300, 80, 45000, 0, false});
    snapshot.units.push_back({305, NetUnitKind::FlogKonoha, 0, 416, 120, 250, 1, false});
    assert(host.sendSnapshot(snapshot, &error));
    bool snapshotReceived = false;
    waitFor(host, client, [&]() {
        std::vector<StateSnapshot> snapshots;
        client.drainSnapshots(snapshots);
        if (snapshots.empty())
            return false;
        const auto &received = snapshots.front();
        assert(received.characters.front().x == 100);
        assert(received.units.size() == 2);
        assert(received.elapsedSeconds == 33);
        assert(received.sessionEpoch == computeSessionEpoch(host.matchConfig().matchId, host.matchConfig().seed));
        assert(received.stateChecksum == computeStateChecksum(received));
        assert(received.clientSequenceWatermark >= 70);
        snapshotReceived = true;
        return true;
    }, 3000);
    assert(snapshotReceived);

    StateSnapshot clientState;
    clientState.matchId = client.matchConfig().matchId;
    clientState.tick = 12;
    clientState.characters.push_back({1, 20500, 8100, 8000, 55, 2, true});
    assert(client.sendClientState(clientState, &error));

    StateSnapshot wrongSlot = clientState;
    wrongSlot.characters[0].slot = 0;
    assert(!client.sendClientState(wrongSlot, &error));

    bool clientStateReceived = false;
    waitFor(host, client, [&]() {
        std::vector<StateSnapshot> states;
        host.drainClientStates(states);
        if (!states.empty())
        {
            assert(states.front().characters[0].slot == 1);
            assert(states.front().characters[0].x == 20500);
            clientStateReceived = true;
        }
        return clientStateReceived;
    }, 3000);

    StateSnapshot staleClientState = clientState;
    staleClientState.tick = 11;
    staleClientState.characters[0].x = 9999;
    assert(client.sendClientState(staleClientState, &error));
    pump(host, client, 200);
    {
        std::vector<StateSnapshot> states;
        host.drainClientStates(states);
        assert(states.empty());
    }

    assert(host.sendMatchEnd(1, &error));
    std::vector<uint8_t> ends;
    waitFor(host, client, [&]() {
        client.drainMatchEnds(ends);
        return !ends.empty();
    }, 3000);
    assert(ends.back() == 1);
    waitFor(host, client, [&]() { return host.matchEndAcknowledged(); }, 3000);

    host.stop();
    client.stop();

    {
        LanSession hostSession;
        LanSession client1;
        LanSession busyClient;
        LanSession client2;
        std::string err;
        assert(hostSession.host("Rejoin Room", "Host", 29877, &err));
        assert(client1.join("127.0.0.1", 29877, "Client1", &err));

        waitFor(hostSession, client1, [&]() {
            return hostSession.state() == SessionState::Lobby && client1.state() == SessionState::Lobby &&
                   hostSession.remoteConnected() && client1.remoteConnected();
        }, 4000);

        assert(busyClient.join("127.0.0.1", 29877, "BusyClient", &err));
        waitFor(hostSession, busyClient, [&]() {
            return busyClient.state() == SessionState::Error;
        }, 3000);
        assert(hostSession.remoteConnected());
        assert(hostSession.matchConfig().slots[1].playerName == "Client1");
        busyClient.stop();

        assert(client1.setLocalHero("Sasuke"));
        waitFor(hostSession, client1, [&]() {
            return hostSession.matchConfig().slots.size() >= 2 &&
                   hostSession.matchConfig().slots[1].heroName == "Sasuke";
        }, 3000);

        client1.stop();
        waitFor(hostSession, client1, [&]() {
            return hostSession.state() == SessionState::Hosting && !hostSession.remoteConnected();
        }, 3000);

        assert(hostSession.matchConfig().slots.size() >= 2);
        assert(hostSession.matchConfig().slots[1].playerName.empty());
        assert(hostSession.matchConfig().slots[1].heroName.empty());

        assert(client2.join("127.0.0.1", 29877, "Client2", &err));
        waitFor(hostSession, client2, [&]() {
            return hostSession.state() == SessionState::Lobby && client2.state() == SessionState::Lobby &&
                   hostSession.remoteConnected() && client2.remoteConnected() &&
                   hostSession.matchConfig().slots[1].playerName == "Client2";
        }, 4000);

        assert(client2.setLocalHero("Kakashi"));
        assert(client2.setLocalReady(true));
        assert(hostSession.setLocalReady(true));
        waitFor(hostSession, client2, [&]() {
            return hostSession.matchConfig().slots[1].heroName == "Kakashi" &&
                   hostSession.matchConfig().slots[0].ready && hostSession.matchConfig().slots[1].ready;
        }, 3000);

        assert(hostSession.startMatch(&err));
        waitFor(hostSession, client2, [&]() {
            return hostSession.state() == SessionState::Loading && client2.state() == SessionState::Loading;
        }, 3000);

        assert(hostSession.markLoaded(&err));
        pump(hostSession, client2, 100);
        assert(hostSession.state() == SessionState::Loading);
        assert(client2.markLoaded(&err));
        waitFor(hostSession, client2, [&]() {
            return hostSession.state() == SessionState::Battle && client2.state() == SessionState::Battle;
        }, 3000);

        hostSession.stop();
        client2.stop();
    }

    std::cout << "lan_session_test: ok\n";
    return 0;
}
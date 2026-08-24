// Session integration tests: loopback host/client lifecycle + v2 authority flow.
#include "Network/LanSession.hpp"

#include <cassert>
#include <chrono>
#include <functional>
#include <iostream>
#include <thread>

using namespace nsv2::network;

namespace
{
void waitFor(LanSession &host, LanSession &client, const std::function<bool()> &condition, int timeoutMs)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline)
    {
        host.poll();
        client.poll();
        if (condition())
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::cerr << "waitFor TIMEOUT: host state=" << static_cast<int>(host.state())
              << ", client state=" << static_cast<int>(client.state())
              << ", host remoteConn=" << host.remoteConnected()
              << ", client remoteConn=" << client.remoteConnected() << std::endl;
    assert(condition());
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
} // namespace

int main()
{
    testOfflineDoesNotOpenSocket();

    LanSession host;
    LanSession client;
    std::string error;
    assert(host.host("Loopback Room", "Host", 29876, &error));
    assert(client.join("127.0.0.1", 29876, "Client", &error));

    waitFor(host, client, [&]() {
        return host.state() == SessionState::Lobby && client.state() == SessionState::Lobby &&
               host.remoteConnected() && client.remoteConnected();
    }, 4000);

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
    assert(host.markLoaded(&error));
    assert(client.markLoaded(&error));
    waitFor(host, client, [&]() {
        return host.state() == SessionState::Battle && client.state() == SessionState::Battle;
    }, 3000);

    // --- Input relay: discrete actions and movement both reach the host. ---
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

    // --- Out-of-order / retransmitted inputs are accepted (gap fill). ---
    InputCommand ordered;
    ordered.sequence = 50;
    ordered.tick = 3;
    ordered.action = ActionType::Move;
    ordered.axisX = 500;
    assert(client.submitInput(ordered, &error));
    std::vector<InputCommand> accepted;
    for (int i = 0; i < 200 && accepted.empty(); ++i)
    {
        client.poll();
        host.poll();
        std::vector<InputCommand> temp;
        host.drainInputCommands(temp);
        for (auto &c : temp)
        {
            if (c.sequence == 50)
                accepted.push_back(std::move(c));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(accepted.size() == 1);
    assert(accepted.front().sequence == 50);

    InputCommand outOfOrder = ordered; // seq 49 arrives after 50
    outOfOrder.sequence = 49;
    outOfOrder.tick = 4;
    outOfOrder.axisX = -500;
    assert(client.submitInput(outOfOrder, &error));
    bool gapFilled = false;
    waitFor(host, client, [&]() {
        std::vector<InputCommand> cmds;
        host.drainInputCommands(cmds);
        for (const auto &c : cmds)
        {
            if (c.sequence == 49 && c.axisX == -500)
            {
                gapFilled = true;
                return true;
            }
        }
        return false;
    }, 3000);
    assert(gapFilled); // v2: retransmit fills the lost packet's slot

    // --- Duplicate delivery must not double-apply. ---
    InputCommand dupe = ordered; // seq 50 again
    dupe.tick = 6;
    assert(client.submitInput(dupe, &error));
    for (int i = 0; i < 40; ++i) // drain window; duplicates are dropped
    {
        client.poll();
        host.poll();
        std::vector<InputCommand> cmds;
        host.drainInputCommands(cmds);
        for (const auto &c : cmds)
            assert(c.sequence != 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // --- All skill/item actions relay. ---
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
        skillCmd.sequence = 60 + i;
        skillCmd.tick = 5 + i;
        skillCmd.action = testActions[i];
        assert(client.submitInput(skillCmd, &error));
        bool received = false;
        waitFor(host, client, [&]() {
            std::vector<InputCommand> cmds;
            host.drainInputCommands(cmds);
            for (const auto &c : cmds)
            {
                if (c.action == testActions[i])
                    received = true;
            }
            return received;
        }, 3000);
        assert(received);
    }

    // --- Host snapshot with battlefield units reaches the client. ---
    StateSnapshot snapshot;
    snapshot.matchId = host.matchConfig().matchId;
    snapshot.tick = 2;
    snapshot.elapsedSeconds = 33;
    snapshot.characters.push_back({1, 100, 200, 900, 100, 2, false});
    snapshot.units.push_back({1, NetUnitKind::Tower, 0, 300, 80, 45000, 0, false});
    snapshot.units.push_back({305, NetUnitKind::FlogKonoha, 0, 416, 120, 250, 1, false});
    assert(host.sendSnapshot(snapshot, &error));
    waitFor(host, client, [&]() {
        std::vector<StateSnapshot> snapshots;
        client.drainSnapshots(snapshots);
        return !snapshots.empty() &&
               snapshots.front().characters.front().x == 100 &&
               snapshots.front().units.size() == 2 &&
               snapshots.front().elapsedSeconds == 33;
    }, 3000);

    // --- ClientState: validated slot ownership, delivered to host. ---
    StateSnapshot clientState;
    clientState.matchId = client.matchConfig().matchId;
    clientState.tick = 12;
    clientState.characters.push_back({1, 20500, 8100, 8000, 55, 2, true});
    assert(client.sendClientState(clientState, &error));

    StateSnapshot wrongSlot = clientState;
    wrongSlot.characters[0].slot = 0;
    assert(!client.sendClientState(wrongSlot, &error)); // must own the slot

    waitFor(host, client, [&]() {
        std::vector<StateSnapshot> states;
        host.drainClientStates(states);
        return !states.empty() &&
               states.front().characters[0].slot == 1 &&
               states.front().characters[0].x == 20500;
    }, 3000);

    // --- MatchEnd verdict broadcast. ---
    assert(host.sendMatchEnd(1, &error));
    std::vector<uint8_t> ends;
    waitFor(host, client, [&]() {
        client.drainMatchEnds(ends);
        return !ends.empty();
    }, 3000);
    assert(ends.back() == 1);

    host.stop();
    client.stop();

    // --- Disconnect & rejoin keeps slot state clean. ---
    {
        LanSession hostSession;
        LanSession client1;
        LanSession client2;
        std::string err;
        assert(hostSession.host("Rejoin Room", "Host", 29877, &err));
        assert(client1.join("127.0.0.1", 29877, "Client1", &err));

        waitFor(hostSession, client1, [&]() {
            return hostSession.state() == SessionState::Lobby && client1.state() == SessionState::Lobby &&
                   hostSession.remoteConnected() && client1.remoteConnected();
        }, 4000);

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

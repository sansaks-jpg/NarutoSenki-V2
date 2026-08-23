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
    }, 1000);

    assert(host.setLocalHero("Naruto"));
    assert(client.setLocalHero("Sasuke"));
    waitFor(host, client, [&]() {
        return host.matchConfig().slots.size() == 2 &&
               client.matchConfig().slots.size() == 2 &&
               host.matchConfig().slots[1].heroName == "Sasuke" &&
               client.matchConfig().slots[1].heroName == "Sasuke";
    }, 1000);

    assert(host.setLocalReady(true));
    assert(client.setLocalReady(true));
    waitFor(host, client, [&]() {
        return host.matchConfig().slots[0].ready && host.matchConfig().slots[1].ready;
    }, 1000);

    assert(host.startMatch(&error));
    waitFor(host, client, [&]() {
        return host.state() == SessionState::Loading && client.state() == SessionState::Loading;
    }, 1000);
    assert(host.markLoaded(&error));
    assert(client.markLoaded(&error));
    waitFor(host, client, [&]() {
        return host.state() == SessionState::Battle && client.state() == SessionState::Battle;
    }, 1000);

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
            if (c.playerSlot == 1)
                inputReceived = true;
        }
        return inputReceived;
    }, 1000);
    assert(inputReceived);

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

    InputCommand outOfOrder = ordered;
    outOfOrder.sequence = 49;
    outOfOrder.tick = 4;
    outOfOrder.axisX = -500;
    assert(client.submitInput(outOfOrder, &error));
    for (int i = 0; i < 20; ++i)
    {
        client.poll();
        host.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    std::vector<InputCommand> rejected;
    host.drainInputCommands(rejected);
    assert(rejected.empty());

    StateSnapshot snapshot;
    snapshot.matchId = host.matchConfig().matchId;
    snapshot.tick = 2;
    snapshot.characters.push_back({1, 100, 200, 900, 100, 2, false});
    assert(host.sendSnapshot(snapshot, &error));
    waitFor(host, client, [&]() {
        std::vector<StateSnapshot> snapshots;
        client.drainSnapshots(snapshots);
        return !snapshots.empty() && snapshots.front().characters.front().x == 100;
    }, 1000);

    // Test all skill and item actions (C1 verification)
    ActionType testActions[] = {
        ActionType::NormalAttack,
        ActionType::Skill1,
        ActionType::Skill2,
        ActionType::Skill3,
        ActionType::Skill4,
        ActionType::Skill5,
        ActionType::Item1
    };
    for (size_t i = 0; i < sizeof(testActions)/sizeof(testActions[0]); ++i)
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
        }, 1000);
        assert(received);
    }

    host.stop();
    client.stop();
    std::cout << "lan_session_test: ok\n";
    return 0;
}

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
    assert(condition());
}
}

int main()
{
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
    waitFor(host, client, [&]() {
        std::vector<InputCommand> commands;
        host.drainInputCommands(commands);
        return !commands.empty() && commands.front().playerSlot == 1;
    }, 1000);

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

    host.stop();
    client.stop();
    std::cout << "lan_session_test: ok\n";
    return 0;
}

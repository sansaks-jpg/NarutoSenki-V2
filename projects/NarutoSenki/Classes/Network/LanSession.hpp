#pragma once

#include "LanDiscovery.hpp"
#include "LanProtocol.hpp"
#include "LanTransport.hpp"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace nsv2::network
{

enum class SessionRole
{
    None,
    Host,
    Client,
};

enum class SessionState
{
    Idle,
    Hosting,
    Connecting,
    Lobby,
    Loading,
    Battle,
    Finished,
    Error,
};

struct SessionNotice
{
    SessionState state = SessionState::Idle;
    std::string text;
};

class LanSession
{
public:
    LanSession();
    ~LanSession();

    LanSession(const LanSession &) = delete;
    LanSession &operator=(const LanSession &) = delete;

    bool host(const std::string &roomName, const std::string &playerName,
              uint16_t port = kDefaultLanPort, std::string *error = nullptr);
    bool join(const std::string &address, uint16_t port, const std::string &playerName,
              std::string *error = nullptr);

    void poll();
    void stop();

    bool startScan(std::string *error = nullptr);
    void stopScan();
    void getRooms(std::vector<RoomAdvertisement> &rooms);

    bool setLocalHero(const std::string &heroName);
    bool setLocalReady(bool ready);
    bool startMatch(std::string *error = nullptr);
    bool markLoaded(std::string *error = nullptr);
    bool submitInput(const InputCommand &command, std::string *error = nullptr);
    void drainInputCommands(std::vector<InputCommand> &commands);
    bool sendSnapshot(const StateSnapshot &snapshot, std::string *error = nullptr);
    void drainSnapshots(std::vector<StateSnapshot> &snapshots);

    void drainNotices(std::vector<SessionNotice> &notices);
    SessionRole role() const { return _role; }
    SessionState state() const { return _state; }
    const MatchConfig &matchConfig() const { return _config; }
    const std::string &localPlayerName() const { return _localPlayerName; }
    uint8_t localSlot() const { return _localSlot; }
    bool localReady() const { return _localReady; }
    bool remoteConnected() const { return _remoteConnected; }
    bool networkActive() const
    {
        return _transport.isRunning() || _discovery.isAdvertising() || _discovery.isScanning();
    }
    const std::string &remoteAddress() const { return _remoteAddress; }
    uint16_t remotePort() const { return _remotePort; }

private:
    void setState(SessionState state, const std::string &notice);
    void handleTransportEvents(const std::vector<TransportEvent> &events);
    void handleMessage(const TransportEvent &event);
    void sendLobbyUpdate();
    bool sendToRemote(const Message &message, std::string *error = nullptr);
    bool sendLoadedToHost(std::string *error);
    void initializeConfig();
    bool updateRemoteHero(const std::string &heroName);
    bool validateInput(const InputCommand &command, std::string *error) const;

    LanTransport _transport;
    LanDiscovery _discovery;
    SessionRole _role = SessionRole::None;
    SessionState _state = SessionState::Idle;
    std::string _localPlayerName;
    std::string _remotePlayerName;
    std::string _remoteAddress;
    uint16_t _remotePort = 0;
    uint8_t _localSlot = 0;
    bool _localReady = false;
    bool _remoteConnected = false;
    bool _localLoaded = false;
    bool _remoteLoaded = false;
    uint32_t _nextSequence = 1;
    MatchConfig _config;
    std::deque<SessionNotice> _notices;
    std::deque<InputCommand> _inputCommands;
    std::deque<StateSnapshot> _snapshots;
    uint32_t _lastRemoteInputSequence = 0;
    uint64_t _sessionStartedMs = 0;
    uint64_t _lastReceiveMs = 0;
    uint64_t _lastHeartbeatMs = 0;
    uint64_t _lastHelloSendMs = 0;
};

} // namespace nsv2::network

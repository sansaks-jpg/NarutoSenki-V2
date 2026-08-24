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

struct SessionDiagnostics
{
    uint32_t matchId = 0;
    SessionRole role = SessionRole::None;
    SessionState state = SessionState::Idle;
    std::string remoteAddress;
    uint16_t remotePort = 0;
    bool remoteConnected = false;
    uint32_t lastRemoteInputSequence = 0;
    uint32_t lastAckedByRemote = 0;
    uint32_t remoteSequenceWatermark = 0;
    size_t pendingReliableCount = 0;
    size_t inputQueueDepth = 0;
    size_t snapshotQueueDepth = 0;
    uint64_t lastReceiveMs = 0;
    uint64_t lastHeartbeatMs = 0;
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
    // Host -> Client full authoritative world snapshot.
    bool sendSnapshot(const StateSnapshot &snapshot, std::string *error = nullptr);
    void drainSnapshots(std::vector<StateSnapshot> &snapshots);
    // Client -> Host authoritative state of the client-owned hero.
    bool sendClientState(const StateSnapshot &state, std::string *error = nullptr);
    void drainClientStates(std::vector<StateSnapshot> &states);
    // Host -> Client final match verdict (payload: winner GroupId).
    bool sendMatchEnd(uint8_t winnerGroup, std::string *error = nullptr);
    void drainMatchEnds(std::vector<uint8_t> &winners);

    void drainNotices(std::vector<SessionNotice> &notices);
    void getDiagnostics(SessionDiagnostics &out) const;
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
    void processReliableQueue(uint64_t nowMs);
    void clearBattleQueues();

    struct PendingReliable
    {
        Message message;
        uint64_t lastSendMs = 0;
        int attempts = 0;
    };

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
    std::deque<StateSnapshot> _clientStates;
    std::deque<uint8_t> _matchEnds;
    std::vector<PendingReliable> _pendingReliable;
    uint32_t _lastRemoteInputSequence = 0;
    // Highest sequence of OUR inputs the remote peer has acked back to us.
    uint32_t _lastAckedByRemote = 0;
    // Replay window: bit i of the mask is set when sequence
    // (_remoteSequenceWatermark - i) was already accepted/applied. Lets late
    // retransmits fill gaps while rejecting replays of applied inputs.
    uint64_t _remoteRecentMask = 0;
    uint32_t _remoteSequenceWatermark = 0;
    bool tryAcceptRemoteSequence(uint32_t sequence);
    uint64_t _sessionStartedMs = 0;
    uint64_t _lastReceiveMs = 0;
    uint64_t _lastHeartbeatMs = 0;
    uint64_t _lastHelloSendMs = 0;
};

} // namespace nsv2::network

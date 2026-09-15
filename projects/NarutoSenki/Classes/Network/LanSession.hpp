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
    uint32_t lastClientStateTick = 0;
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

    // Cocos stops main-thread polling while Android is backgrounded. Treat a
    // mid-battle background as a local forfeit: notify the peer, stop sockets,
    // and queue the opponent as winner so GameLayer resolves a loss on resume.
    // Outside Battle a plain stop is enough (LoadLayer already treats Idle as abort).
    void handleAppBackground()
    {
        if (_role == SessionRole::None)
            return;
        if (_state != SessionState::Battle)
        {
            stop();
            return;
        }
        if (!_matchEnds.empty())
            return;

        const uint8_t opponentSlot = _localSlot == 0 ? 1 : 0;
        if (_config.slots.size() > opponentSlot)
            _matchEnds.push_back(static_cast<uint8_t>(_config.slots[opponentSlot].group));

        if (_remoteConnected && !_remoteAddress.empty() && _remotePort != 0)
        {
            Message leave;
            leave.type = MessageType::Leave;
            leave.sequence = _nextSequence++;
            std::string ignored;
            sendToRemote(leave, &ignored);
        }
        _transport.stop();
        _discovery.stop();
        _remoteConnected = false;
        _pendingReliable.clear();
        _notices.push_back({_state, "LAN battle dihentikan karena aplikasi masuk background."});
    }

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

    bool sendClientState(const StateSnapshot &state, std::string *error = nullptr);
    void drainClientStates(std::vector<StateSnapshot> &states);

    bool sendMatchEnd(uint8_t winnerGroup, std::string *error = nullptr);
    void drainMatchEnds(std::vector<uint8_t> &winners);
    bool matchEndPending() const { return _matchEndSequence != 0 && !_matchEndAcknowledged; }
    bool matchEndAcknowledged() const { return _matchEndAcknowledged; }

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
    void handleHello(const TransportEvent &event);
    void sendLobbyUpdate();
    bool sendToRemote(const Message &message, std::string *error = nullptr);
    bool sendReliable(Message message, bool replaceSameType, bool critical,
                      std::string *error = nullptr);
    void sendDeliveryAck(uint32_t sequence);
    void acknowledgeReliable(uint32_t sequence);
    bool sendLoadedToHost(std::string *error);
    void maybeSendBattleReady(std::string *error = nullptr);
    void initializeConfig();
    bool updateRemoteHero(const std::string &heroName);
    bool validateInput(const InputCommand &command, std::string *error) const;
    void processReliableQueue(uint64_t nowMs);
    void clearBattleQueues();
    bool isExpectedPeer(const TransportEvent &event) const;
    bool tryAcceptRemoteSequence(uint32_t sequence);
    void updateDiscoveryCapacity(uint8_t playerCount);

    struct PendingReliable
    {
        Message message;
        uint64_t lastSendMs = 0;
        int attempts = 0;
        bool critical = false;
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
    RoomAdvertisement _roomAdvertisement;

    std::deque<SessionNotice> _notices;
    std::deque<InputCommand> _inputCommands;
    std::deque<StateSnapshot> _snapshots;
    std::deque<StateSnapshot> _clientStates;
    std::deque<uint8_t> _matchEnds;
    std::vector<PendingReliable> _pendingReliable;

    uint32_t _lastRemoteInputSequence = 0;
    uint32_t _lastAckedByRemote = 0;
    uint64_t _remoteRecentMask = 0;
    uint32_t _remoteSequenceWatermark = 0;
    uint32_t _lastRemoteMoveSequence = 0;

    uint32_t _lastRemoteHeroSequence = 0;
    uint32_t _lastRemoteReadySequence = 0;
    uint32_t _lastLobbyUpdateSequence = 0;
    uint32_t _lastMatchStartSequence = 0;
    uint32_t _lastClientStateTick = 0;
    uint32_t _lastSnapshotTick = 0;

    uint32_t _battleReadySequence = 0;
    uint32_t _matchEndSequence = 0;
    bool _matchEndAcknowledged = false;

    uint64_t _sessionStartedMs = 0;
    uint64_t _stateEnteredMs = 0;
    uint64_t _lastReceiveMs = 0;
    uint64_t _lastHeartbeatMs = 0;
    uint64_t _lastHelloSendMs = 0;
};

} // namespace nsv2::network
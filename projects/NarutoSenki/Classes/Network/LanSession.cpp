#include "LanSession.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

namespace nsv2::network
{
namespace
{
constexpr size_t kMaxPlayerName = 64;
constexpr size_t kMaxHeroName = 64;
constexpr uint8_t kAckDelivery = 0;
constexpr uint8_t kAckBattleReady = 1;
constexpr uint64_t kReliableResendIntervalMs = 150;
constexpr int kReliableMaxAttempts = 40;
constexpr uint64_t kHandshakeTimeoutMs = 8000;
constexpr uint64_t kPeerTimeoutMs = 10000;
constexpr uint64_t kBattlePeerTimeoutMs = 6000;
constexpr uint64_t kLoadingDeadlineMs = 60000;
constexpr uint64_t kFinalVerdictFlushMs = 500;

void putU16(std::vector<uint8_t> &out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
}

void putU32(std::vector<uint8_t> &out, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<uint8_t>((value >> shift) & 0xFF));
}

bool readU32(const std::vector<uint8_t> &data, size_t offset, uint32_t &out)
{
    if (offset + 4 > data.size())
        return false;
    out = static_cast<uint32_t>(data[offset]) |
          (static_cast<uint32_t>(data[offset + 1]) << 8) |
          (static_cast<uint32_t>(data[offset + 2]) << 16) |
          (static_cast<uint32_t>(data[offset + 3]) << 24);
    return true;
}

bool readString(const std::vector<uint8_t> &data, std::string &out, size_t maxLength)
{
    if (data.size() < 2)
        return false;
    const uint16_t length = static_cast<uint16_t>(data[0]) |
                            static_cast<uint16_t>(data[1] << 8);
    if (length > maxLength || data.size() != static_cast<size_t>(length) + 2)
        return false;
    out.assign(reinterpret_cast<const char *>(data.data() + 2), length);
    return true;
}

bool encodeString(const std::string &value, std::vector<uint8_t> &out, size_t maxLength)
{
    if (value.empty() || value.size() > maxLength)
        return false;
    out.clear();
    putU16(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
    return true;
}

void encodeAckPayload(uint8_t kind, uint32_t value, std::vector<uint8_t> &out)
{
    out.clear();
    out.push_back(kind);
    putU32(out, value);
}

bool decodeAckPayload(const Message &message, uint8_t &kind, uint32_t &value)
{
    if (message.payload.size() != 5)
        return false;
    kind = message.payload[0];
    return readU32(message.payload, 1, value);
}

uint64_t nowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
}

uint32_t makeMatchId()
{
    static std::atomic<uint32_t> counter{0x13579BDFu};
    const uint64_t now = nowMs();
    uint32_t value = static_cast<uint32_t>(now) ^ static_cast<uint32_t>(now >> 32) ^
                     counter.fetch_add(0x9E3779B9u);
    if (value == 0)
        value = 1;
    return value;
}
} // namespace

LanSession::LanSession() = default;

LanSession::~LanSession()
{
    stop();
}

void LanSession::setState(SessionState state, const std::string &notice)
{
    if (_state != state)
    {
        _state = state;
        _stateEnteredMs = nowMs();
    }
    if (!notice.empty())
        _notices.push_back({state, notice});
}

void LanSession::initializeConfig()
{
    _config = {};
    _config.matchId = makeMatchId();
    _config.mode = 0;
    _config.mapId = 1;
    _config.seed = (_config.matchId * 1664525u) + 1013904223u;
    _config.tickRate = 30;
    _config.maxPlayers = 2;
    _config.enableGear = false;
    _config.enableReborn = true;
    // Hero selection is explicit. Empty defaults prevent a player becoming
    // Ready with a hero they never confirmed in the LAN selection screen.
    _config.slots = {
        {0, GroupId::Konoha, false, false, _localPlayerName, ""},
        {1, GroupId::Akatsuki, false, true, "", ""},
    };
}

bool LanSession::host(const std::string &roomName, const std::string &playerName,
                      uint16_t port, std::string *error)
{
    stop();
    _role = SessionRole::Host;
    _localSlot = 0;
    _localPlayerName = playerName.empty() ? "Host" : playerName.substr(0, kMaxPlayerName);
    initializeConfig();

    _roomAdvertisement = {};
    _roomAdvertisement.roomId = std::to_string(_config.matchId);
    _roomAdvertisement.roomName = roomName.empty() ? "Naruto Senki Room" : roomName.substr(0, kMaxPlayerName);
    _roomAdvertisement.hostName = _localPlayerName;
    _roomAdvertisement.port = port;
    _roomAdvertisement.playerCount = 1;
    _roomAdvertisement.maxPlayers = _config.maxPlayers;
    _roomAdvertisement.mode = _config.mode;
    _roomAdvertisement.mapId = _config.mapId;

    if (!_discovery.startAdvertising(_roomAdvertisement, error))
    {
        _role = SessionRole::None;
        return false;
    }
    if (!_transport.startHost(port, error))
    {
        _discovery.stop();
        _role = SessionRole::None;
        return false;
    }

    _localReady = false;
    _remoteConnected = false;
    _remoteLoaded = false;
    _localLoaded = false;
    _nextSequence = 1;
    _sessionStartedMs = nowMs();
    _lastReceiveMs = 0;
    _lastHeartbeatMs = _sessionStartedMs;
    setState(SessionState::Hosting, "Room dibuat. Menunggu pemain lain...");
    return true;
}

bool LanSession::join(const std::string &address, uint16_t port, const std::string &playerName,
                      std::string *error)
{
    stop();
    _role = SessionRole::Client;
    _localSlot = 1;
    _localPlayerName = playerName.empty() ? "Client" : playerName.substr(0, kMaxPlayerName);
    _remoteAddress = address;
    _remotePort = port;
    if (!_transport.connectTo(address, port, error))
    {
        _role = SessionRole::None;
        return false;
    }

    _localReady = false;
    _remoteConnected = false;
    _localLoaded = false;
    _remoteLoaded = false;
    _nextSequence = 1;
    _sessionStartedMs = nowMs();
    _lastReceiveMs = 0;
    _lastHeartbeatMs = _sessionStartedMs;
    _lastHelloSendMs = _sessionStartedMs;

    Message hello;
    hello.type = MessageType::Hello;
    hello.sequence = _nextSequence++;
    if (!encodeString(_localPlayerName, hello.payload, kMaxPlayerName) || !sendToRemote(hello, error))
    {
        stop();
        return false;
    }

    setState(SessionState::Connecting, "Menghubungkan ke host...");
    return true;
}

bool LanSession::sendToRemote(const Message &message, std::string *error)
{
    if (_remoteAddress.empty() || _remotePort == 0)
    {
        if (error)
            *error = "remote peer is not known";
        return false;
    }
    Message stamped = message;
    stamped.ack = 0;
    return _transport.send(stamped, _remoteAddress, _remotePort, error);
}

bool LanSession::sendReliable(Message message, bool replaceSameType, bool critical,
                              std::string *error)
{
    if (message.sequence == 0)
        message.sequence = _nextSequence++;
    if (!sendToRemote(message, error))
        return false;

    if (replaceSameType)
    {
        _pendingReliable.erase(
            std::remove_if(_pendingReliable.begin(), _pendingReliable.end(),
                           [&](const PendingReliable &pending) {
                               return pending.message.type == message.type;
                           }),
            _pendingReliable.end());
    }

    PendingReliable pending;
    pending.message = std::move(message);
    pending.lastSendMs = nowMs();
    pending.attempts = 1;
    pending.critical = critical;
    _pendingReliable.push_back(std::move(pending));
    return true;
}

void LanSession::sendDeliveryAck(uint32_t sequence)
{
    if (sequence == 0 || _remoteAddress.empty() || _remotePort == 0)
        return;
    Message ack;
    ack.type = MessageType::Ack;
    ack.sequence = _nextSequence++;
    encodeAckPayload(kAckDelivery, sequence, ack.payload);
    std::string ignored;
    sendToRemote(ack, &ignored);
}

void LanSession::acknowledgeReliable(uint32_t sequence)
{
    if (sequence == 0)
        return;
    _lastAckedByRemote = std::max(_lastAckedByRemote, sequence);

    _pendingReliable.erase(
        std::remove_if(_pendingReliable.begin(), _pendingReliable.end(),
                       [&](const PendingReliable &pending) {
                           return pending.message.sequence == sequence;
                       }),
        _pendingReliable.end());

    if (_role == SessionRole::Host && sequence == _battleReadySequence &&
        _state == SessionState::Loading)
    {
        setState(SessionState::Battle, "Battle-ready diterima client. Match berjalan.");
    }
    if (_role == SessionRole::Host && sequence == _matchEndSequence)
        _matchEndAcknowledged = true;
}

void LanSession::sendLobbyUpdate()
{
    if (!_remoteConnected || _role != SessionRole::Host || _state != SessionState::Lobby)
        return;
    Message update;
    update.type = MessageType::LobbyUpdate;
    update.sequence = _nextSequence++;
    std::string error;
    if (!encodeMatchConfig(_config, update.payload, &error))
    {
        setState(SessionState::Error, "Lobby config tidak valid: " + error);
        return;
    }
    if (!sendReliable(std::move(update), true, true, &error))
        setState(SessionState::Error, "Gagal mengirim lobby: " + error);
}

void LanSession::updateDiscoveryCapacity(uint8_t playerCount)
{
    if (_roomAdvertisement.port == 0)
        return;
    _roomAdvertisement.playerCount = std::min(playerCount, _roomAdvertisement.maxPlayers);
    _discovery.updateAdvertisement(_roomAdvertisement);
}

bool LanSession::updateRemoteHero(const std::string &heroName)
{
    if (heroName.empty() || heroName.size() > kMaxHeroName || heroName == "None")
        return false;
    if (_config.slots.size() < 2)
        return false;
    if (!_config.slots[0].heroName.empty() && _config.slots[0].heroName == heroName)
        return false;
    _config.slots[1].heroName = heroName;
    _config.slots[1].ready = false;
    return true;
}

bool LanSession::setLocalHero(const std::string &heroName)
{
    if (heroName.empty() || heroName.size() > kMaxHeroName || heroName == "None")
        return false;
    if (_role == SessionRole::Client && _state != SessionState::Lobby)
        return false;
    if (_role == SessionRole::Host && _state != SessionState::Lobby && _state != SessionState::Hosting)
        return false;
    if (_config.slots.size() < 2)
        initializeConfig();

    const uint8_t opponentSlot = _localSlot == 0 ? 1 : 0;
    if (_config.slots.size() > opponentSlot &&
        !_config.slots[opponentSlot].heroName.empty() &&
        _config.slots[opponentSlot].heroName == heroName)
        return false;

    _config.slots[_localSlot].heroName = heroName;
    _localReady = false;
    _config.slots[_localSlot].ready = false;

    if (_role == SessionRole::Client && _remoteConnected)
    {
        Message select;
        select.type = MessageType::SelectHero;
        select.sequence = _nextSequence++;
        if (!encodeString(heroName, select.payload, kMaxHeroName))
            return false;
        std::string error;
        if (!sendReliable(std::move(select), true, true, &error))
        {
            setState(SessionState::Error, "Gagal mengirim pilihan hero: " + error);
            return false;
        }
    }
    else if (_role == SessionRole::Host && _state == SessionState::Lobby)
    {
        sendLobbyUpdate();
    }
    return true;
}

bool LanSession::setLocalReady(bool ready)
{
    if (_role == SessionRole::None || _state != SessionState::Lobby)
        return false;
    if (_config.slots.size() <= _localSlot || _config.slots[_localSlot].heroName.empty())
        return false;

    _localReady = ready;
    _config.slots[_localSlot].ready = ready;
    if (_role == SessionRole::Client)
    {
        Message message;
        message.type = MessageType::Ready;
        message.sequence = _nextSequence++;
        message.payload.push_back(ready ? 1 : 0);
        std::string error;
        if (!sendReliable(std::move(message), true, true, &error))
        {
            setState(SessionState::Error, "Gagal mengirim ready: " + error);
            return false;
        }
    }
    else
    {
        sendLobbyUpdate();
    }
    return true;
}

bool LanSession::startMatch(std::string *error)
{
    if (_role != SessionRole::Host || _state != SessionState::Lobby || !_remoteConnected ||
        _config.slots.size() < 2 || !_localReady || !_config.slots[1].ready ||
        _config.slots[0].heroName.empty() || _config.slots[1].heroName.empty())
    {
        if (error)
            *error = "kedua pemain harus memilih hero dan ready";
        return false;
    }

    clearBattleQueues();
    _config.matchId = makeMatchId();
    _config.seed = (_config.matchId * 1664525u) + 1013904223u;
    _localLoaded = false;
    _remoteLoaded = false;
    _battleReadySequence = 0;
    _matchEndSequence = 0;
    _matchEndAcknowledged = false;

    Message start;
    start.type = MessageType::MatchStart;
    start.sequence = _nextSequence++;
    start.tick = 0;
    if (!encodeMatchConfig(_config, start.payload, error) ||
        !sendReliable(std::move(start), true, true, error))
        return false;

    _discovery.stop();
    setState(SessionState::Loading, "Match dimulai. Memuat resource...");
    return true;
}

bool LanSession::sendLoadedToHost(std::string *error)
{
    Message loaded;
    loaded.type = MessageType::Loaded;
    loaded.sequence = _nextSequence++;
    loaded.payload.push_back(1);
    return sendReliable(std::move(loaded), true, true, error);
}

void LanSession::maybeSendBattleReady(std::string *error)
{
    if (_role != SessionRole::Host || _state != SessionState::Loading ||
        !_localLoaded || !_remoteLoaded || _battleReadySequence != 0)
        return;

    Message ready;
    ready.type = MessageType::Ack;
    ready.sequence = _nextSequence++;
    encodeAckPayload(kAckBattleReady, _config.matchId, ready.payload);
    _battleReadySequence = ready.sequence;
    if (!sendReliable(std::move(ready), false, true, error))
    {
        _battleReadySequence = 0;
        if (error && !error->empty())
            setState(SessionState::Error, "Gagal mengirim battle-ready: " + *error);
    }
}

bool LanSession::markLoaded(std::string *error)
{
    if (_state == SessionState::Battle)
        return true;
    if (_state != SessionState::Loading)
        return false;
    if (_localLoaded)
    {
        if (_role == SessionRole::Host)
            maybeSendBattleReady(error);
        return true;
    }

    _localLoaded = true;
    if (_role == SessionRole::Client)
        return sendLoadedToHost(error);

    maybeSendBattleReady(error);
    return _state != SessionState::Error;
}

bool LanSession::validateInput(const InputCommand &command, std::string *error) const
{
    if (_state != SessionState::Battle)
    {
        if (error)
            *error = "input hanya boleh dikirim saat battle aktif";
        return false;
    }
    if (command.matchId != _config.matchId)
    {
        if (error)
            *error = "match id input tidak cocok";
        return false;
    }
    if (command.playerSlot >= _config.maxPlayers)
    {
        if (error)
            *error = "player slot input tidak valid";
        return false;
    }
    if (command.action < ActionType::Move || command.action > ActionType::Item1)
    {
        if (error)
            *error = "action input tidak valid";
        return false;
    }
    if (command.axisX < -1000 || command.axisX > 1000 ||
        command.axisY < -1000 || command.axisY > 1000)
    {
        if (error)
            *error = "axis input di luar batas";
        return false;
    }
    return true;
}

bool LanSession::tryAcceptRemoteSequence(uint32_t sequence)
{
    if (sequence == 0)
        return false;
    if (_remoteSequenceWatermark == 0)
    {
        _remoteSequenceWatermark = sequence;
        _remoteRecentMask = 1;
        return true;
    }
    if (sequence > _remoteSequenceWatermark)
    {
        const uint32_t jump = sequence - _remoteSequenceWatermark;
        _remoteRecentMask = jump >= 64 ? 0 : (_remoteRecentMask << jump);
        _remoteRecentMask |= 1;
        _remoteSequenceWatermark = sequence;
        return true;
    }
    const uint64_t offset = _remoteSequenceWatermark - sequence;
    if (offset >= 64)
        return false;
    const uint64_t bit = 1ULL << offset;
    if (_remoteRecentMask & bit)
        return false;
    _remoteRecentMask |= bit;
    return true;
}

bool LanSession::submitInput(const InputCommand &command, std::string *error)
{
    InputCommand normalized = command;
    normalized.matchId = _config.matchId;
    normalized.playerSlot = _localSlot;
    if (normalized.sequence == 0)
        normalized.sequence = _nextSequence++;
    if (!validateInput(normalized, error))
        return false;

    if (_remoteConnected)
    {
        Message message;
        message.type = MessageType::Input;
        message.sequence = normalized.sequence;
        message.tick = normalized.tick;
        if (!encodeInputCommand(normalized, message.payload, error))
            return false;

        const bool reliable = normalized.action != ActionType::Move || normalized.isDiscreteAction;
        if (reliable)
        {
            if (!sendReliable(std::move(message), false, true, error))
                return false;
        }
        else if (!sendToRemote(message, error))
        {
            return false;
        }
    }

    return true;
}

void LanSession::drainInputCommands(std::vector<InputCommand> &commands)
{
    while (!_inputCommands.empty())
    {
        commands.push_back(std::move(_inputCommands.front()));
        _inputCommands.pop_front();
    }
}

bool LanSession::sendSnapshot(const StateSnapshot &snapshot, std::string *error)
{
    if (_role != SessionRole::Host || _state != SessionState::Battle ||
        snapshot.matchId != _config.matchId)
    {
        if (error)
            *error = "snapshot hanya dapat dikirim host saat battle dengan match id aktif";
        return false;
    }

    StateSnapshot normalized = snapshot;
    normalized.sessionEpoch = computeSessionEpoch(_config.matchId, _config.seed);
    normalized.clientSequenceWatermark = _lastRemoteInputSequence;
    normalized.stateChecksum = computeStateChecksum(normalized);

    Message message;
    message.type = MessageType::Snapshot;
    message.sequence = _nextSequence++;
    message.tick = normalized.tick;
    if (!encodeStateSnapshot(normalized, message.payload, error))
        return false;
    return sendToRemote(message, error);
}

void LanSession::drainSnapshots(std::vector<StateSnapshot> &snapshots)
{
    while (!_snapshots.empty())
    {
        snapshots.push_back(std::move(_snapshots.front()));
        _snapshots.pop_front();
    }
}

bool LanSession::sendClientState(const StateSnapshot &state, std::string *error)
{
    if (_role != SessionRole::Client || _state != SessionState::Battle ||
        state.matchId != _config.matchId || state.characters.size() != 1 ||
        state.characters[0].slot != _localSlot)
    {
        if (error)
            *error = "client state hanya dapat dikirim client untuk hero miliknya";
        return false;
    }

    StateSnapshot normalized = state;
    normalized.sessionEpoch = computeSessionEpoch(_config.matchId, _config.seed);
    normalized.units.clear();
    normalized.combatEvents.clear();
    normalized.stateChecksum = computeStateChecksum(normalized);

    Message message;
    message.type = MessageType::ClientState;
    message.sequence = _nextSequence++;
    message.tick = normalized.tick;
    if (!encodeStateSnapshot(normalized, message.payload, error))
        return false;
    return sendToRemote(message, error);
}

void LanSession::drainClientStates(std::vector<StateSnapshot> &states)
{
    while (!_clientStates.empty())
    {
        states.push_back(std::move(_clientStates.front()));
        _clientStates.pop_front();
    }
}

bool LanSession::sendMatchEnd(uint8_t winnerGroup, std::string *error)
{
    if (_role != SessionRole::Host || _state != SessionState::Battle || winnerGroup > 1)
    {
        if (error)
            *error = "match end hanya dapat dikirim host saat battle";
        return false;
    }
    if (_matchEndSequence != 0)
        return true;

    Message message;
    message.type = MessageType::MatchEnd;
    message.sequence = _nextSequence++;
    message.payload.push_back(winnerGroup);
    _matchEndSequence = message.sequence;
    _matchEndAcknowledged = false;
    if (!sendReliable(std::move(message), false, false, error))
    {
        _matchEndSequence = 0;
        return false;
    }
    return true;
}

void LanSession::drainMatchEnds(std::vector<uint8_t> &winners)
{
    while (!_matchEnds.empty())
    {
        winners.push_back(_matchEnds.front());
        _matchEnds.pop_front();
    }
}

bool LanSession::isExpectedPeer(const TransportEvent &event) const
{
    if (_remoteAddress.empty() || _remotePort == 0)
        return false;
    return event.address == _remoteAddress && event.port == _remotePort;
}

void LanSession::handleHello(const TransportEvent &event)
{
    if (_role != SessionRole::Host)
        return;

    std::string name;
    if (!readString(event.message.payload, name, kMaxPlayerName))
    {
        Message reject;
        reject.type = MessageType::JoinReject;
        reject.sequence = _nextSequence++;
        const char text[] = "bad hello";
        reject.payload.assign(text, text + sizeof(text) - 1);
        std::string ignored;
        _transport.send(reject, event.address, event.port, &ignored);
        return;
    }

    if (_remoteConnected)
    {
        if (event.address == _remoteAddress && event.port == _remotePort)
        {
            _lastReceiveMs = nowMs();
            auto it = std::find_if(_pendingReliable.begin(), _pendingReliable.end(), [](const PendingReliable &p) {
                return p.message.type == MessageType::JoinAccept;
            });
            if (it != _pendingReliable.end())
            {
                std::string ignored;
                sendToRemote(it->message, &ignored);
                it->lastSendMs = nowMs();
                ++it->attempts;
            }
            else if (_state == SessionState::Lobby)
            {
                Message accept;
                accept.type = MessageType::JoinAccept;
                accept.sequence = _nextSequence++;
                std::string error;
                if (encodeMatchConfig(_config, accept.payload, &error))
                    sendReliable(std::move(accept), true, true, &error);
            }
            return;
        }

        Message reject;
        reject.type = MessageType::JoinReject;
        reject.sequence = _nextSequence++;
        const char text[] = "room full or busy";
        reject.payload.assign(text, text + sizeof(text) - 1);
        std::string ignored;
        _transport.send(reject, event.address, event.port, &ignored);
        return;
    }

    if (_state != SessionState::Hosting)
    {
        Message reject;
        reject.type = MessageType::JoinReject;
        reject.sequence = _nextSequence++;
        const char text[] = "match in progress";
        reject.payload.assign(text, text + sizeof(text) - 1);
        std::string ignored;
        _transport.send(reject, event.address, event.port, &ignored);
        return;
    }

    _remoteAddress = event.address;
    _remotePort = event.port;
    _remotePlayerName = name;
    _remoteConnected = true;
    _lastReceiveMs = nowMs();

    if (_config.slots.size() < 2)
        _config.slots.resize(2);
    _config.slots[1].slot = 1;
    _config.slots[1].group = GroupId::Akatsuki;
    _config.slots[1].playerName = name;
    _config.slots[1].remote = true;
    _config.slots[1].ready = false;

    setState(SessionState::Lobby, "Pemain bergabung: " + name);
    updateDiscoveryCapacity(2);

    Message accept;
    accept.type = MessageType::JoinAccept;
    accept.sequence = _nextSequence++;
    std::string error;
    if (!encodeMatchConfig(_config, accept.payload, &error) ||
        !sendReliable(std::move(accept), true, true, &error))
    {
        setState(SessionState::Error, "Gagal menerima client: " + error);
        return;
    }
    sendLobbyUpdate();
}

void LanSession::handleMessage(const TransportEvent &event)
{
    const Message &message = event.message;

    if (_role == SessionRole::Host && message.type == MessageType::Hello)
    {
        handleHello(event);
        return;
    }

    if (!isExpectedPeer(event))
        return;

    _lastReceiveMs = nowMs();

    if (message.type == MessageType::Heartbeat)
        return;

    if (message.type == MessageType::JoinReject && _role == SessionRole::Client &&
        (_state == SessionState::Connecting || _state == SessionState::Lobby))
    {
        const std::string reason(message.payload.begin(), message.payload.end());
        _remoteConnected = false;
        setState(SessionState::Error, reason.empty() ? "Host menolak koneksi." : "Host menolak koneksi: " + reason);
        return;
    }

    if (message.type == MessageType::Ack)
    {
        uint8_t kind = 0;
        uint32_t value = 0;
        if (!decodeAckPayload(message, kind, value))
            return;
        if (kind == kAckDelivery)
        {
            acknowledgeReliable(value);
            return;
        }
        if (kind == kAckBattleReady && _role == SessionRole::Client &&
            (_state == SessionState::Loading || _state == SessionState::Battle) &&
            value == _config.matchId)
        {
            sendDeliveryAck(message.sequence);
            if (_state == SessionState::Loading)
                setState(SessionState::Battle, "Host mengonfirmasi loaded barrier. Match berjalan.");
        }
        return;
    }

    if (_role == SessionRole::Client && message.type == MessageType::JoinAccept &&
        (_state == SessionState::Connecting || _state == SessionState::Lobby))
    {
        MatchConfig config;
        std::string error;
        if (!decodeMatchConfig(message.payload, config, &error))
        {
            setState(SessionState::Error, "Config host tidak valid: " + error);
            return;
        }
        sendDeliveryAck(message.sequence);
        if (!tryAcceptRemoteSequence(message.sequence))
            return;

        _config = std::move(config);
        _remoteConnected = true;
        if (_config.slots.size() > _localSlot)
            _config.slots[_localSlot].playerName = _localPlayerName;
        if (_state == SessionState::Connecting)
            setState(SessionState::Lobby, "Terhubung ke lobby host.");
        return;
    }

    if (message.type == MessageType::LobbyUpdate && _role == SessionRole::Client &&
        _state == SessionState::Lobby)
    {
        MatchConfig config;
        std::string error;
        if (!decodeMatchConfig(message.payload, config, &error))
            return;
        sendDeliveryAck(message.sequence);
        if (!tryAcceptRemoteSequence(message.sequence) || message.sequence <= _lastLobbyUpdateSequence)
            return;
        _lastLobbyUpdateSequence = message.sequence;
        _config = std::move(config);
        setState(SessionState::Lobby, "Lobby diperbarui.");
        return;
    }

    if (_role == SessionRole::Host && message.type == MessageType::SelectHero &&
        _remoteConnected && _state == SessionState::Lobby)
    {
        sendDeliveryAck(message.sequence);
        if (!tryAcceptRemoteSequence(message.sequence) || message.sequence <= _lastRemoteHeroSequence)
            return;

        // SelectHero and Ready are separate UDP messages. Preserve a Ready
        // action that was sent later (higher sender sequence) but happened to
        // arrive first; conversely, a genuinely newer hero selection still
        // clears ready as intended.
        const bool preserveLaterReady = _lastRemoteReadySequence > message.sequence;
        const bool laterReadyValue = _config.slots.size() >= 2 ? _config.slots[1].ready : false;
        _lastRemoteHeroSequence = message.sequence;

        std::string hero;
        if (readString(message.payload, hero, kMaxHeroName) && updateRemoteHero(hero))
        {
            if (preserveLaterReady && _config.slots.size() >= 2)
                _config.slots[1].ready = laterReadyValue;
            setState(SessionState::Lobby, "Pilihan hero client diterima.");
        }
        sendLobbyUpdate();
        return;
    }

    if (_role == SessionRole::Host && message.type == MessageType::Ready &&
        _remoteConnected && _state == SessionState::Lobby)
    {
        sendDeliveryAck(message.sequence);
        if (!tryAcceptRemoteSequence(message.sequence) || message.sequence <= _lastRemoteReadySequence)
            return;
        _lastRemoteReadySequence = message.sequence;
        if (message.payload.size() != 1 || message.payload[0] > 1 || _config.slots.size() < 2)
            return;

        // A Ready packet that predates the latest hero selection is obsolete:
        // selecting a new hero invalidates readiness even if UDP reorders them.
        if (message.sequence > _lastRemoteHeroSequence)
            _config.slots[1].ready = message.payload[0] != 0;
        sendLobbyUpdate();
        return;
    }

    if (message.type == MessageType::MatchStart && _role == SessionRole::Client &&
        (_state == SessionState::Lobby || _state == SessionState::Loading))
    {
        MatchConfig config;
        std::string error;
        if (!decodeMatchConfig(message.payload, config, &error))
            return;
        sendDeliveryAck(message.sequence);
        if (!tryAcceptRemoteSequence(message.sequence) || message.sequence <= _lastMatchStartSequence)
            return;

        clearBattleQueues();
        _lastMatchStartSequence = message.sequence;
        _config = std::move(config);
        _localLoaded = false;
        _remoteLoaded = false;
        _battleReadySequence = 0;
        _matchEndSequence = 0;
        _matchEndAcknowledged = false;
        setState(SessionState::Loading, "Match diterima. Memuat resource...");
        return;
    }

    if (message.type == MessageType::Loaded && _role == SessionRole::Host &&
        (_state == SessionState::Loading || _state == SessionState::Battle))
    {
        sendDeliveryAck(message.sequence);
        if (_state == SessionState::Battle)
            return;
        if (message.payload.size() != 1 || message.payload[0] != 1)
            return;
        if (!tryAcceptRemoteSequence(message.sequence))
            return;
        _remoteLoaded = true;
        std::string error;
        maybeSendBattleReady(&error);
        return;
    }

    if (message.type == MessageType::Input && _remoteConnected && _state == SessionState::Battle)
    {
        InputCommand command;
        std::string error;
        const uint8_t expectedSlot = (_role == SessionRole::Host) ? 1 : 0;
        if (!decodeInputCommand(message.payload, command, &error) ||
            message.sequence != command.sequence ||
            command.playerSlot != expectedSlot || !validateInput(command, &error))
            return;

        const bool reliable = command.action != ActionType::Move || command.isDiscreteAction;
        if (reliable)
        {
            sendDeliveryAck(message.sequence);
            if (!tryAcceptRemoteSequence(command.sequence))
                return;
        }
        else
        {
            if (command.sequence <= _lastRemoteMoveSequence)
                return;
            _lastRemoteMoveSequence = command.sequence;
        }

        _lastRemoteInputSequence = std::max(_lastRemoteInputSequence, command.sequence);
        _inputCommands.push_back(std::move(command));
        return;
    }

    if (_role == SessionRole::Host && message.type == MessageType::ClientState &&
        _remoteConnected && _state == SessionState::Battle)
    {
        StateSnapshot state;
        std::string error;
        if (!decodeStateSnapshot(message.payload, state, &error) ||
            state.matchId != _config.matchId ||
            state.sessionEpoch != computeSessionEpoch(_config.matchId, _config.seed) ||
            state.stateChecksum != computeStateChecksum(state) ||
            state.characters.size() != 1 || state.characters[0].slot != 1 ||
            !state.units.empty() || !state.combatEvents.empty() ||
            state.tick <= _lastClientStateTick)
            return;

        _lastClientStateTick = state.tick;
        _clientStates.clear();
        _clientStates.push_back(std::move(state));
        return;
    }

    if (message.type == MessageType::MatchEnd && _role == SessionRole::Client &&
        _state == SessionState::Battle && message.payload.size() == 1 && message.payload[0] <= 1)
    {
        sendDeliveryAck(message.sequence);
        if (!tryAcceptRemoteSequence(message.sequence))
            return;
        _matchEnds.push_back(message.payload[0]);
        return;
    }

    if (message.type == MessageType::Snapshot && _role == SessionRole::Client &&
        _state == SessionState::Battle)
    {
        StateSnapshot snapshot;
        std::string error;
        if (!decodeStateSnapshot(message.payload, snapshot, &error) ||
            snapshot.matchId != _config.matchId ||
            snapshot.sessionEpoch != computeSessionEpoch(_config.matchId, _config.seed) ||
            snapshot.stateChecksum != computeStateChecksum(snapshot) ||
            snapshot.tick <= _lastSnapshotTick)
            return;

        _lastSnapshotTick = snapshot.tick;
        _snapshots.clear();
        _snapshots.push_back(std::move(snapshot));
        return;
    }

    if (message.type == MessageType::Leave || message.type == MessageType::Disconnect)
    {
        _remoteConnected = false;
        _remoteLoaded = false;
        if (_role == SessionRole::Host)
        {
            if (_matchEndSequence != 0)
                _matchEndAcknowledged = true;

            if (_state == SessionState::Lobby)
            {
                if (_config.slots.size() >= 2)
                    _config.slots[1] = {1, GroupId::Akatsuki, false, true, "", ""};
                updateDiscoveryCapacity(1);
                setState(SessionState::Hosting, "Pemain keluar dari room.");
            }
            else
            {
                setState(SessionState::Finished, "Client meninggalkan match.");
            }
        }
        else
        {
            setState(SessionState::Finished, "Koneksi ke host berakhir.");
        }
        return;
    }
}

void LanSession::handleTransportEvents(const std::vector<TransportEvent> &events)
{
    for (const auto &event : events)
    {
        if (event.type == TransportEventType::Message)
            handleMessage(event);
    }
}

void LanSession::processReliableQueue(uint64_t currentMs)
{
    if (_pendingReliable.empty())
        return;

    bool criticalTimedOut = false;
    MessageType timedOutType = MessageType::Error;
    std::vector<PendingReliable> stillPending;
    stillPending.reserve(_pendingReliable.size());

    for (auto &pending : _pendingReliable)
    {
        if (pending.attempts >= kReliableMaxAttempts)
        {
            if (pending.critical)
            {
                criticalTimedOut = true;
                timedOutType = pending.message.type;
            }
            continue;
        }

        if (currentMs >= pending.lastSendMs &&
            currentMs - pending.lastSendMs >= kReliableResendIntervalMs)
        {
            std::string ignored;
            sendToRemote(pending.message, &ignored);
            pending.lastSendMs = currentMs;
            ++pending.attempts;
        }
        stillPending.push_back(std::move(pending));
    }

    _pendingReliable = std::move(stillPending);
    if (criticalTimedOut && _state != SessionState::Finished && _state != SessionState::Error)
    {
        _remoteConnected = false;
        _pendingReliable.clear();
        setState(SessionState::Error,
                 "Reliable LAN packet timeout (type " +
                     std::to_string(static_cast<uint16_t>(timedOutType)) + ").");
    }
}

void LanSession::poll()
{
    if (!networkActive())
        return;

    std::vector<TransportEvent> events;
    _transport.poll(events);
    handleTransportEvents(events);
    std::vector<RoomAdvertisement> ignoredRooms;
    _discovery.poll(ignoredRooms);

    const uint64_t currentMs = nowMs();
    processReliableQueue(currentMs);

    if (_remoteConnected && currentMs >= _lastHeartbeatMs &&
        currentMs - _lastHeartbeatMs >= 1000)
    {
        Message heartbeat;
        heartbeat.type = MessageType::Heartbeat;
        heartbeat.sequence = _nextSequence++;
        std::string ignored;
        sendToRemote(heartbeat, &ignored);
        _lastHeartbeatMs = currentMs;
    }

    if (_state == SessionState::Connecting)
    {
        if (currentMs >= _lastHelloSendMs && currentMs - _lastHelloSendMs >= 600)
        {
            Message hello;
            hello.type = MessageType::Hello;
            hello.sequence = _nextSequence++;
            encodeString(_localPlayerName, hello.payload, kMaxPlayerName);
            std::string ignored;
            sendToRemote(hello, &ignored);
            _lastHelloSendMs = currentMs;
        }
        if (currentMs >= _sessionStartedMs && currentMs - _sessionStartedMs >= kHandshakeTimeoutMs)
        {
            _remoteConnected = false;
            setState(SessionState::Error, "Timeout handshake: host tidak merespons.");
        }
    }
    else if (_state == SessionState::Lobby && _remoteConnected)
    {
        if (_lastReceiveMs > 0 && currentMs >= _lastReceiveMs &&
            currentMs - _lastReceiveMs >= kPeerTimeoutMs)
        {
            _remoteConnected = false;
            if (_role == SessionRole::Host)
            {
                if (_config.slots.size() >= 2)
                    _config.slots[1] = {1, GroupId::Akatsuki, false, true, "", ""};
                updateDiscoveryCapacity(1);
                setState(SessionState::Hosting, "Client terputus dari lobby.");
            }
            else
            {
                setState(SessionState::Finished, "Host terputus dari lobby.");
            }
        }
    }
    else if (_state == SessionState::Loading && _remoteConnected)
    {
        if (_lastReceiveMs > 0 && currentMs >= _lastReceiveMs &&
            currentMs - _lastReceiveMs >= kPeerTimeoutMs)
        {
            _remoteConnected = false;
            setState(SessionState::Finished, "Peer terputus saat loading.");
        }
        else if (_stateEnteredMs > 0 && currentMs >= _stateEnteredMs &&
                 currentMs - _stateEnteredMs >= kLoadingDeadlineMs)
        {
            _remoteConnected = false;
            setState(SessionState::Error, "Timeout sinkronisasi loading.");
        }
    }
    else if (_state == SessionState::Battle && _remoteConnected)
    {
        if (_lastReceiveMs > 0 && currentMs >= _lastReceiveMs &&
            currentMs - _lastReceiveMs >= kBattlePeerTimeoutMs)
        {
            _remoteConnected = false;
            setState(SessionState::Finished, "Koneksi battle terputus; match berakhir.");
        }
    }
}

bool LanSession::startScan(std::string *error)
{
    return _discovery.startScanning(error);
}

void LanSession::stopScan()
{
    if (_discovery.isScanning())
        _discovery.stop();
}

void LanSession::getRooms(std::vector<RoomAdvertisement> &rooms)
{
    rooms.clear();
    if (!networkActive())
        return;
    _discovery.poll(rooms);
}

void LanSession::drainNotices(std::vector<SessionNotice> &notices)
{
    while (!_notices.empty())
    {
        notices.push_back(std::move(_notices.front()));
        _notices.pop_front();
    }
}

void LanSession::getDiagnostics(SessionDiagnostics &out) const
{
    out.matchId = _config.matchId;
    out.role = _role;
    out.state = _state;
    out.remoteAddress = _remoteAddress;
    out.remotePort = _remotePort;
    out.remoteConnected = _remoteConnected;
    out.lastRemoteInputSequence = _lastRemoteInputSequence;
    out.lastAckedByRemote = _lastAckedByRemote;
    out.remoteSequenceWatermark = _remoteSequenceWatermark;
    out.lastClientStateTick = _lastClientStateTick;
    out.pendingReliableCount = _pendingReliable.size();
    out.inputQueueDepth = _inputCommands.size();
    out.snapshotQueueDepth = _snapshots.size();
    out.lastReceiveMs = _lastReceiveMs;
    out.lastHeartbeatMs = _lastHeartbeatMs;
}

void LanSession::clearBattleQueues()
{
    _inputCommands.clear();
    _snapshots.clear();
    _clientStates.clear();
    _matchEnds.clear();
    _pendingReliable.clear();
    _lastRemoteInputSequence = 0;
    _lastAckedByRemote = 0;
    _remoteRecentMask = 0;
    _remoteSequenceWatermark = 0;
    _lastRemoteMoveSequence = 0;
    _lastClientStateTick = 0;
    _lastSnapshotTick = 0;
}

void LanSession::stop()
{
    if (_role == SessionRole::Host && _remoteConnected &&
        _matchEndSequence != 0 && !_matchEndAcknowledged)
    {
        const uint64_t deadline = nowMs() + kFinalVerdictFlushMs;
        while (!_matchEndAcknowledged && nowMs() < deadline)
        {
            auto pending = std::find_if(_pendingReliable.begin(), _pendingReliable.end(),
                                        [&](const PendingReliable &entry) {
                                            return entry.message.sequence == _matchEndSequence;
                                        });
            if (pending != _pendingReliable.end())
            {
                std::string ignored;
                sendToRemote(pending->message, &ignored);
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            std::vector<TransportEvent> events;
            _transport.poll(events);
            handleTransportEvents(events);
        }
    }

    if (_role != SessionRole::None && _remoteConnected &&
        !_remoteAddress.empty() && _remotePort != 0)
    {
        Message leave;
        leave.type = MessageType::Leave;
        leave.sequence = _nextSequence++;
        std::string ignored;
        sendToRemote(leave, &ignored);
    }

    _transport.stop();
    _discovery.stop();
    _role = SessionRole::None;
    _state = SessionState::Idle;
    _remoteConnected = false;
    _localLoaded = false;
    _remoteLoaded = false;
    _localReady = false;
    _remoteAddress.clear();
    _remotePort = 0;
    _remotePlayerName.clear();
    _config = {};
    _roomAdvertisement = {};
    clearBattleQueues();
    _notices.clear();
    _nextSequence = 1;
    _lastRemoteHeroSequence = 0;
    _lastRemoteReadySequence = 0;
    _lastLobbyUpdateSequence = 0;
    _lastMatchStartSequence = 0;
    _battleReadySequence = 0;
    _matchEndSequence = 0;
    _matchEndAcknowledged = false;
    _sessionStartedMs = 0;
    _stateEnteredMs = 0;
    _lastReceiveMs = 0;
    _lastHeartbeatMs = 0;
    _lastHelloSendMs = 0;
}

} // namespace nsv2::network

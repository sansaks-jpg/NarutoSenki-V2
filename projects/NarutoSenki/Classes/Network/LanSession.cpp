#include "LanSession.hpp"

#include <algorithm>
#include <chrono>

namespace nsv2::network
{
namespace
{
constexpr size_t kMaxPlayerName = 64;
constexpr size_t kMaxHeroName = 64;

void putU16(std::vector<uint8_t> &out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value & 0xFF));
    out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
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

uint64_t nowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
}

uint32_t makeMatchId()
{
    return static_cast<uint32_t>(nowMs());
}
} // namespace

LanSession::LanSession() = default;

LanSession::~LanSession()
{
    stop();
}

void LanSession::setState(SessionState state, const std::string &notice)
{
    _state = state;
    _lastReceiveMs = nowMs();
    if (!notice.empty())
        _notices.push_back({state, notice});
}

void LanSession::initializeConfig()
{
    _config = {};
    _config.matchId = makeMatchId();
    _config.mode = 0; // GameMode::OneVsOne
    _config.mapId = 1;
    _config.seed = _config.matchId ^ 0x4E535632u;
    _config.tickRate = 30;
    _config.maxPlayers = 2;
    _config.enableGear = true;
    _config.enableReborn = true;
    _config.slots = {
        {0, GroupId::Konoha, false, false, _localPlayerName, "Naruto"},
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

    RoomAdvertisement room;
    room.roomId = std::to_string(_config.matchId);
    room.roomName = roomName.empty() ? "Naruto Senki Room" : roomName.substr(0, kMaxPlayerName);
    room.hostName = _localPlayerName;
    room.port = port;
    room.playerCount = 1;
    room.maxPlayers = _config.maxPlayers;
    room.mode = _config.mode;
    room.mapId = _config.mapId;

    if (!_discovery.startAdvertising(room, error))
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
    _lastReceiveMs = _sessionStartedMs;
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

    Message hello;
    hello.type = MessageType::Hello;
    hello.sequence = _nextSequence++;
    if (!encodeString(_localPlayerName, hello.payload, kMaxPlayerName) || !sendToRemote(hello, error))
    {
        stop();
        return false;
    }
    _localReady = false;
    _remoteConnected = false;
    _localLoaded = false;
    _remoteLoaded = false;
    _sessionStartedMs = nowMs();
    _lastReceiveMs = _sessionStartedMs;
    _lastHeartbeatMs = _sessionStartedMs;
    _lastHelloSendMs = _sessionStartedMs;
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
    // Piggyback the cumulative input ack on every outbound message so the
    // peer's reliable queue can be cleared without dedicated Ack packets.
    Message stamped = message;
    stamped.ack = _lastRemoteInputSequence;
    return _transport.send(stamped, _remoteAddress, _remotePort, error);
}

void LanSession::sendLobbyUpdate()
{
    if (!_remoteConnected)
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
    if (!sendToRemote(update, &error))
        setState(SessionState::Error, "Gagal mengirim lobby: " + error);
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
    if (_config.slots.size() < 2)
        initializeConfig();
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
        if (!sendToRemote(select, &error))
        {
            setState(SessionState::Error, "Gagal mengirim pilihan hero: " + error);
            return false;
        }
    }
    else if (_role == SessionRole::Host)
    {
        sendLobbyUpdate();
    }
    return true;
}

bool LanSession::setLocalReady(bool ready)
{
    if (_role == SessionRole::None || (_state != SessionState::Lobby && _state != SessionState::Hosting))
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
        if (!sendToRemote(message, &error))
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
        !_localReady || !_config.slots[1].ready || _config.slots[0].heroName.empty() ||
        _config.slots[1].heroName.empty())
    {
        if (error)
            *error = "kedua pemain harus memilih hero dan ready";
        return false;
    }

    _config.matchId = makeMatchId();
    _config.seed = _config.matchId ^ 0x4E535632u;
    Message start;
    start.type = MessageType::MatchStart;
    start.sequence = _nextSequence++;
    start.tick = 0;
    if (!encodeMatchConfig(_config, start.payload, error) || !sendToRemote(start, error))
        return false;
    _localLoaded = false;
    _remoteLoaded = false;
    setState(SessionState::Loading, "Match dimulai. Memuat resource...");
    return true;
}

bool LanSession::sendLoadedToHost(std::string *error)
{
    Message loaded;
    loaded.type = MessageType::Loaded;
    loaded.sequence = _nextSequence++;
    loaded.payload.push_back(1);
    return sendToRemote(loaded, error);
}

bool LanSession::markLoaded(std::string *error)
{
    if (_state != SessionState::Loading)
        return false;
    _localLoaded = true;
    if (_role == SessionRole::Client)
    {
        if (!sendLoadedToHost(error))
            return false;
    }
    else if (_remoteLoaded)
    {
        Message ready;
        ready.type = MessageType::Ack;
        ready.sequence = _nextSequence++;
        ready.payload.push_back(1);
        sendToRemote(ready, error);
        setState(SessionState::Battle, "Semua pemain selesai memuat. Match berjalan.");
    }
    return true;
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
    return true;
}

bool LanSession::tryAcceptRemoteSequence(uint32_t sequence)
{
    if (_remoteSequenceWatermark == 0)
    {
        // First input from the peer.
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
        return false; // ancient replay beyond the window
    const uint64_t bit = 1ULL << offset;
    if (_remoteRecentMask & bit)
        return false; // already accepted/applied
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
        if (encodeInputCommand(normalized, message.payload, error))
        {
            sendToRemote(message, error);
            // Discrete actions (attacks/skills/items) must not be lost to UDP:
            // a silently dropped attack desyncs both simulations. Movement is
            // intentionally fire-and-forget; positions are synced via
            // Snapshot/ClientState instead.
            if (normalized.action != ActionType::Move)
            {
                PendingReliable pending;
                pending.message = std::move(message);
                pending.lastSendMs = nowMs();
                _pendingReliable.push_back(std::move(pending));
            }
        }
    }
    _inputCommands.push_back(normalized);
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
    Message message;
    message.type = MessageType::Snapshot;
    message.sequence = _nextSequence++;
    message.tick = snapshot.tick;
    if (!encodeStateSnapshot(snapshot, message.payload, error))
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
    Message message;
    message.type = MessageType::ClientState;
    message.sequence = _nextSequence++;
    message.tick = state.tick;
    if (!encodeStateSnapshot(state, message.payload, error))
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
    if (_role != SessionRole::Host || _state != SessionState::Battle)
    {
        if (error)
            *error = "match end hanya dapat dikirim host saat battle";
        return false;
    }
    Message message;
    message.type = MessageType::MatchEnd;
    message.sequence = _nextSequence++;
    message.payload.push_back(winnerGroup);
    // Sent best-effort; the battle disconnect path still ends the match on the
    // client if this packet is lost.
    return sendToRemote(message, error);
}

void LanSession::drainMatchEnds(std::vector<uint8_t> &winners)
{
    while (!_matchEnds.empty())
    {
        winners.push_back(std::move(_matchEnds.front()));
        _matchEnds.pop_front();
    }
}

void LanSession::handleMessage(const TransportEvent &event)
{
    _lastReceiveMs = nowMs();
    const Message &message = event.message;
    if (message.ack > _lastAckedByRemote)
        _lastAckedByRemote = message.ack;
    if (message.type == MessageType::Heartbeat)
        return;
    if (_role == SessionRole::Host && message.type == MessageType::Hello && !_remoteConnected)
    {
        std::string name;
        if (!readString(message.payload, name, kMaxPlayerName))
        {
            Message reject;
            reject.type = MessageType::JoinReject;
            reject.payload = {'b', 'a', 'd', ' ', 'h', 'e', 'l', 'l', 'o'};
            std::string ignored;
            _transport.send(reject, event.address, event.port, &ignored);
            return;
        }
        _remoteAddress = event.address;
        _remotePort = event.port;
        _remotePlayerName = name;
        _remoteConnected = true;
        if (_config.slots.size() < 2)
        {
            _config.slots.resize(2);
            _config.slots[1].slot = 1;
            _config.slots[1].group = GroupId::Akatsuki;
        }
        _config.slots[1].playerName = name;
        _config.slots[1].remote = true;
        _config.slots[1].ready = false;

        Message accept;
        accept.type = MessageType::JoinAccept;
        accept.sequence = _nextSequence++;
        std::string error;
        encodeMatchConfig(_config, accept.payload, &error);
        sendToRemote(accept, &error);
        setState(SessionState::Lobby, "Pemain bergabung: " + name);
        sendLobbyUpdate();
        return;
    }

    if (_role == SessionRole::Client && message.type == MessageType::JoinAccept)
    {
        MatchConfig config;
        std::string error;
        if (!decodeMatchConfig(message.payload, config, &error))
        {
            setState(SessionState::Error, "Config host tidak valid: " + error);
            return;
        }
        _config = std::move(config);
        _remoteConnected = true;
        if (_config.slots.size() > _localSlot)
            _config.slots[_localSlot].playerName = _localPlayerName;
        setState(SessionState::Lobby, "Terhubung ke lobby host.");
        return;
    }

    if (message.type == MessageType::LobbyUpdate && _role == SessionRole::Client)
    {
        MatchConfig config;
        std::string error;
        if (!decodeMatchConfig(message.payload, config, &error))
        {
            setState(SessionState::Error, "Lobby update tidak valid: " + error);
            return;
        }
        _config = std::move(config);
        setState(SessionState::Lobby, "Lobby diperbarui.");
        return;
    }

    if (_role == SessionRole::Host && message.type == MessageType::SelectHero && _remoteConnected)
    {
        std::string hero;
        if (readString(message.payload, hero, kMaxHeroName) && updateRemoteHero(hero))
        {
            sendLobbyUpdate();
            setState(SessionState::Lobby, "Pilihan hero client diterima.");
        }
        return;
    }

    if (message.type == MessageType::Input && _remoteConnected && _state == SessionState::Battle)
    {
        InputCommand command;
        std::string error;
        const uint8_t expectedSlot = (_role == SessionRole::Host) ? 1 : 0;
        if (!decodeInputCommand(message.payload, command, &error) ||
            command.playerSlot != expectedSlot || !validateInput(command, &error))
            return;
        // Reject duplicates/replays (including already-applied inputs) while
        // still accepting late retransmits that fill a lost packet's gap.
        if (!tryAcceptRemoteSequence(command.sequence))
            return;
        if (command.sequence > _lastRemoteInputSequence)
            _lastRemoteInputSequence = command.sequence;
        _inputCommands.push_back(std::move(command));
        return;
    }

    if (_role == SessionRole::Host && message.type == MessageType::ClientState &&
        _remoteConnected && _state == SessionState::Battle)
    {
        StateSnapshot state;
        std::string error;
        if (decodeStateSnapshot(message.payload, state, &error) &&
            state.matchId == _config.matchId && !state.characters.empty() &&
            state.characters[0].slot == 1)
            _clientStates.push_back(std::move(state));
        return;
    }

    if (message.type == MessageType::MatchEnd && _state == SessionState::Battle &&
        message.payload.size() == 1 && message.payload[0] <= 1)
    {
        _matchEnds.push_back(message.payload[0]);
        return;
    }

    if (_role == SessionRole::Host && message.type == MessageType::Ready && _remoteConnected)
    {
        if (message.payload.size() != 1 || message.payload[0] > 1 || _config.slots.size() < 2)
            return;
        _config.slots[1].ready = message.payload[0] != 0;
        sendLobbyUpdate();
        return;
    }

    if (message.type == MessageType::Ack && _role == SessionRole::Client &&
        _state == SessionState::Loading && message.payload.size() == 1 && message.payload[0] == 1)
    {
        setState(SessionState::Battle, "Host memulai tick authoritative. Match berjalan.");
        return;
    }

    if (message.type == MessageType::Snapshot && _role == SessionRole::Client &&
        _state == SessionState::Battle)
    {
        StateSnapshot snapshot;
        std::string error;
        if (decodeStateSnapshot(message.payload, snapshot, &error) &&
            snapshot.matchId == _config.matchId)
            _snapshots.push_back(std::move(snapshot));
        return;
    }

    if (message.type == MessageType::MatchStart && _role == SessionRole::Client)
    {
        MatchConfig config;
        std::string error;
        if (!decodeMatchConfig(message.payload, config, &error))
        {
            setState(SessionState::Error, "Match config tidak valid: " + error);
            return;
        }
        _config = std::move(config);
        _localLoaded = false;
        setState(SessionState::Loading, "Match diterima. Memuat resource...");
        return;
    }

    if (message.type == MessageType::Loaded && _role == SessionRole::Host)
    {
        if (message.payload.size() != 1 || message.payload[0] != 1)
            return;
        _remoteLoaded = true;
        if (_localLoaded)
        {
            Message ready;
            ready.type = MessageType::Ack;
            ready.sequence = _nextSequence++;
            ready.payload.push_back(1);
            std::string error;
            sendToRemote(ready, &error);
            setState(SessionState::Battle, "Semua pemain selesai memuat. Match berjalan.");
        }
        return;
    }

    if (message.type == MessageType::Leave || message.type == MessageType::Disconnect)
    {
        if (_remoteConnected && event.address == _remoteAddress && event.port == _remotePort)
        {
            _remoteConnected = false;
            _remoteLoaded = false;
            if (_role == SessionRole::Host)
            {
                if (_config.slots.size() >= 2)
                {
                    _config.slots[1] = {1, GroupId::Akatsuki, false, true, "", ""};
                }
                setState(SessionState::Hosting, "Pemain keluar dari room.");
            }
            else
            {
                setState(SessionState::Finished, "Koneksi ke host berakhir.");
            }
        }
        return;
    }
}

void LanSession::handleTransportEvents(const std::vector<TransportEvent> &events)
{
    for (const auto &event : events)
    {
        if (event.type == TransportEventType::Message)
        {
            handleMessage(event);
        }
        // Non-fatal UDP decode errors are dropped silently without crashing the session (Fix H2)
    }
}

void LanSession::poll()
{
    // LAN is opt-in: offline/training sessions must not touch transport or discovery.
    if (!networkActive())
        return;

    std::vector<TransportEvent> events;
    _transport.poll(events);
    handleTransportEvents(events);
    std::vector<RoomAdvertisement> ignoredRooms;
    _discovery.poll(ignoredRooms);

    const uint64_t currentMs = nowMs();
    processReliableQueue(currentMs);

    if (_remoteConnected && currentMs >= _lastHeartbeatMs && (currentMs - _lastHeartbeatMs >= 1000))
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
        // Retransmit Hello handshake every 600ms (Fix C6)
        if (currentMs >= _lastHelloSendMs && (currentMs - _lastHelloSendMs >= 600))
        {
            Message hello;
            hello.type = MessageType::Hello;
            hello.sequence = _nextSequence++;
            encodeString(_localPlayerName, hello.payload, kMaxPlayerName);
            std::string ignored;
            _transport.send(hello, _remoteAddress, _remotePort, &ignored);
            _lastHelloSendMs = currentMs;
        }
        if (currentMs >= _sessionStartedMs && (currentMs - _sessionStartedMs >= 6000))
        {
            _transport.stop();
            setState(SessionState::Error, "Timeout handshake: host tidak merespons.");
        }
    }
    else if (_state == SessionState::Lobby && _remoteConnected && _role == SessionRole::Host)
    {
        // Lobby heartbeat timeout (10s) to clean up ghost players (Fix H3)
        if (_lastReceiveMs > 0 && currentMs >= _lastReceiveMs && (currentMs - _lastReceiveMs >= 10000))
        {
            _remoteConnected = false;
            if (_config.slots.size() >= 2)
            {
                _config.slots[1] = {1, GroupId::Akatsuki, false, true, "", ""};
            }
            setState(SessionState::Hosting, "Client terputus.");
            sendLobbyUpdate();
        }
    }
    else if (_state == SessionState::Battle && _remoteConnected)
    {
        // Battle disconnect timeout (6s after first packet is received) (Fix C5)
        if (_lastReceiveMs > 0 && currentMs >= _lastReceiveMs && (currentMs - _lastReceiveMs >= 6000))
        {
            _transport.stop();
            _remoteConnected = false;
            setState(SessionState::Finished, "Koneksi battle terputus; match berakhir.");
        }
    }
}

void LanSession::processReliableQueue(uint64_t currentMs)
{
    if (_pendingReliable.empty())
        return;

    constexpr uint64_t kReliableResendIntervalMs = 120;
    constexpr int kReliableMaxAttempts = 25;

    std::vector<PendingReliable> stillPending;
    stillPending.reserve(_pendingReliable.size());
    for (auto &pending : _pendingReliable)
    {
        // Cleared once the peer acks our sequence via any piggybacked ack.
        if (pending.message.sequence <= _lastAckedByRemote ||
            pending.attempts >= kReliableMaxAttempts)
            continue;
        if (currentMs - pending.lastSendMs >= kReliableResendIntervalMs)
        {
            std::string ignored;
            sendToRemote(pending.message, &ignored);
            pending.lastSendMs = currentMs;
            ++pending.attempts;
        }
        stillPending.push_back(std::move(pending));
    }
    _pendingReliable = std::move(stillPending);
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
}

void LanSession::stop()
{
    if (_role != SessionRole::None && _remoteConnected)
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
    _config = {};
    clearBattleQueues();
    _sessionStartedMs = 0;
    _lastReceiveMs = 0;
    _lastHeartbeatMs = 0;
}

} // namespace nsv2::network

#include "LanDiscovery.hpp"

#include <algorithm>
#include <chrono>

namespace nsv2::network
{
namespace
{
uint64_t nowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
}

Message makeDiscoveryMessage()
{
    Message message;
    message.type = MessageType::Hello;
    const char probe[] = "NSV2_DISCOVER";
    message.payload.assign(probe, probe + sizeof(probe) - 1);
    return message;
}
} // namespace

LanDiscovery::~LanDiscovery()
{
    stop();
}

bool LanDiscovery::startAdvertising(const RoomAdvertisement &room, std::string *error)
{
    stop();
    _room = room;
    if (_room.port == 0)
        _room.port = kDefaultLanPort;
    _advertising = _transport.startHost(kDiscoveryPort, error);
    _scanning = false;
    _lastAdMs = 0;
    return _advertising;
}

bool LanDiscovery::startScanning(std::string *error)
{
    stop();
    _trackedRooms.clear();
    _scanning = _transport.startHost(0, error);
    _advertising = false;
    _lastProbeMs = 0;
    if (_scanning)
        sendDiscoveryProbe();
    return _scanning;
}

void LanDiscovery::sendDiscoveryProbe()
{
    if (!_scanning)
        return;
    std::string ignored;
    _transport.broadcast(makeDiscoveryMessage(), kDiscoveryPort, &ignored);
    _lastProbeMs = nowMs();
}

bool LanDiscovery::hasRoom(const std::string &address, uint16_t port) const
{
    return std::any_of(_trackedRooms.begin(), _trackedRooms.end(), [&](const TrackedRoom &tr) {
        return tr.room.address == address && tr.room.port == port;
    });
}

void LanDiscovery::handleEvents(const std::vector<TransportEvent> &events)
{
    for (const auto &event : events)
    {
        if (event.type != TransportEventType::Message)
            continue;

        if (_advertising && event.message.type == MessageType::Hello &&
            event.message.payload.size() == sizeof("NSV2_DISCOVER") - 1)
        {
            const std::string probe(event.message.payload.begin(), event.message.payload.end());
            if (probe != "NSV2_DISCOVER")
                continue;
            Message response;
            response.type = MessageType::RoomAdvertise;
            std::string error;
            if (encodeRoomAdvertisement(_room, response.payload, &error))
                _transport.send(response, event.address, event.port, &error);
            continue;
        }

        if (_scanning && event.message.type == MessageType::RoomAdvertise)
        {
            RoomAdvertisement room;
            std::string error;
            if (!decodeRoomAdvertisement(event.message.payload, room, &error))
                continue;
            room.address = event.address;
            if (room.roomName.empty())
                room.roomName = "Naruto Senki Room";

            const uint64_t now = nowMs();
            auto existing = std::find_if(_trackedRooms.begin(), _trackedRooms.end(), [&](const TrackedRoom &tr) {
                return tr.room.address == room.address && tr.room.port == room.port;
            });
            if (existing == _trackedRooms.end())
            {
                _trackedRooms.push_back({std::move(room), now});
            }
            else
            {
                existing->room = std::move(room);
                existing->lastSeenMs = now;
            }
        }
    }
}

void LanDiscovery::poll(std::vector<RoomAdvertisement> &rooms)
{
    const uint64_t now = nowMs();

    // Broadcast room presence regularly while hosting
    if (_advertising && (now - _lastAdMs >= 1000))
    {
        Message adMsg;
        adMsg.type = MessageType::RoomAdvertise;
        std::string error;
        if (encodeRoomAdvertisement(_room, adMsg.payload, &error))
        {
            std::string ignored;
            _transport.broadcast(adMsg, kDiscoveryPort, &ignored);
        }
        _lastAdMs = now;
    }

    // Send discovery probes regularly while scanning
    if (_scanning && (now - _lastProbeMs >= 600))
    {
        sendDiscoveryProbe();
    }

    std::vector<TransportEvent> events;
    _transport.poll(events);
    handleEvents(events);

    // Clean up stale rooms (older than 4 seconds)
    if (_scanning)
    {
        _trackedRooms.erase(
            std::remove_if(_trackedRooms.begin(), _trackedRooms.end(),
                           [now](const TrackedRoom &tr) { return (now - tr.lastSeenMs) > 4000; }),
            _trackedRooms.end());

        rooms.clear();
        rooms.reserve(_trackedRooms.size());
        for (const auto &tr : _trackedRooms)
            rooms.push_back(tr.room);
    }
}

void LanDiscovery::stop()
{
    _transport.stop();
    _trackedRooms.clear();
    _advertising = false;
    _scanning = false;
    _lastProbeMs = 0;
    _lastAdMs = 0;
}

} // namespace nsv2::network

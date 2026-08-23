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
    return _advertising;
}

bool LanDiscovery::startScanning(std::string *error)
{
    stop();
    _rooms.clear();
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
    return std::any_of(_rooms.begin(), _rooms.end(), [&](const RoomAdvertisement &room) {
        return room.address == address && room.port == port;
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
            auto existing = std::find_if(_rooms.begin(), _rooms.end(), [&](const RoomAdvertisement &current) {
                return current.address == room.address && current.port == room.port;
            });
            if (existing == _rooms.end())
                _rooms.push_back(std::move(room));
            else
                *existing = std::move(room);
        }
    }
}

void LanDiscovery::poll(std::vector<RoomAdvertisement> &rooms)
{
    if (_scanning && nowMs() - _lastProbeMs >= 1000)
        sendDiscoveryProbe();

    std::vector<TransportEvent> events;
    _transport.poll(events);
    handleEvents(events);
    rooms = _rooms;
}

void LanDiscovery::stop()
{
    _transport.stop();
    _rooms.clear();
    _advertising = false;
    _scanning = false;
    _lastProbeMs = 0;
}

} // namespace nsv2::network

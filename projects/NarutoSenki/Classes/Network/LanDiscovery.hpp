#pragma once

#include "LanProtocol.hpp"
#include "LanTransport.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace nsv2::network
{

class LanDiscovery
{
public:
    LanDiscovery() = default;
    ~LanDiscovery();

    LanDiscovery(const LanDiscovery &) = delete;
    LanDiscovery &operator=(const LanDiscovery &) = delete;

    bool startAdvertising(const RoomAdvertisement &room, std::string *error = nullptr);
    bool startScanning(std::string *error = nullptr);
    void poll(std::vector<RoomAdvertisement> &rooms);
    void stop();

    bool isAdvertising() const { return _advertising; }
    bool isScanning() const { return _scanning; }

private:
    void sendDiscoveryProbe();
    void handleEvents(const std::vector<TransportEvent> &events);
    bool hasRoom(const std::string &address, uint16_t port) const;

    LanTransport _transport;
    RoomAdvertisement _room;
    std::vector<RoomAdvertisement> _rooms;
    bool _advertising = false;
    bool _scanning = false;
    uint64_t _lastProbeMs = 0;
};

} // namespace nsv2::network

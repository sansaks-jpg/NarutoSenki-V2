#pragma once

#include "LanProtocol.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nsv2::network
{

enum class TransportEventType
{
    Started,
    Message,
    Error,
    Stopped,
};

struct TransportEvent
{
    TransportEventType type = TransportEventType::Error;
    Message message;
    std::string address;
    uint16_t port = 0;
    std::string error;
};

// Small UDP transport for LAN/loopback. Network I/O happens on the worker thread;
// consumers must call poll() on the Cocos main thread before touching game nodes.
class LanTransport
{
public:
    LanTransport();
    ~LanTransport();

    LanTransport(const LanTransport &) = delete;
    LanTransport &operator=(const LanTransport &) = delete;

    bool startHost(uint16_t port = kDefaultLanPort, std::string *error = nullptr);
    bool connectTo(const std::string &address, uint16_t port, std::string *error = nullptr);

    bool send(const Message &message, const std::string &address = std::string(),
              uint16_t port = 0, std::string *error = nullptr);
    bool broadcast(const Message &message, uint16_t discoveryPort = kDiscoveryPort,
                   std::string *error = nullptr);

    void poll(std::vector<TransportEvent> &events);
    void stop();

    bool isRunning() const { return _running.load(); }
    bool isHost() const { return _isHost; }
    uint16_t localPort() const { return _localPort; }
    const std::string &remoteAddress() const { return _remoteAddress; }
    uint16_t remotePort() const { return _remotePort; }

private:
    void workerLoop();
    void pushEvent(TransportEvent event);
    bool openSocket(uint16_t port, bool enableBroadcast, std::string *error);
    bool sendRaw(const std::vector<uint8_t> &bytes, const std::string &address,
                 uint16_t port, std::string *error);

    std::atomic<bool> _running{false};
    bool _isHost = false;
    intptr_t _socket = -1;
    uint16_t _localPort = 0;
    std::string _remoteAddress;
    uint16_t _remotePort = 0;
    std::thread _worker;
    mutable std::mutex _socketMutex;
    std::mutex _eventMutex;
    std::deque<TransportEvent> _events;
};

} // namespace nsv2::network

#include "LanTransport.hpp"

#include <cerrno>
#include <cstring>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace nsv2::network
{
namespace
{
#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

NativeSocket nativeSocket(intptr_t value)
{
    return static_cast<NativeSocket>(value);
}

intptr_t socketValue(NativeSocket value)
{
    return static_cast<intptr_t>(value);
}

void closeNativeSocket(NativeSocket socket)
{
    if (socket == kInvalidSocket)
        return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

bool wouldBlock()
{
#if defined(_WIN32)
    const int code = WSAGetLastError();
    return code == WSAEWOULDBLOCK || code == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

std::string lastSocketError()
{
#if defined(_WIN32)
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

void setError(std::string *error, const std::string &value)
{
    if (error)
        *error = value;
}
} // namespace

LanTransport::LanTransport() = default;

LanTransport::~LanTransport()
{
    stop();
}

bool LanTransport::openSocket(uint16_t port, bool enableBroadcast, std::string *error)
{
#if defined(_WIN32)
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        setError(error, "WSAStartup failed");
        return false;
    }
#endif

    NativeSocket socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == kInvalidSocket)
    {
        setError(error, "socket(): " + lastSocketError());
        return false;
    }

    int reuse = 1;
    if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                   reinterpret_cast<const char *>(&reuse), sizeof(reuse)) != 0)
    {
        setError(error, "setsockopt(SO_REUSEADDR): " + lastSocketError());
        closeNativeSocket(socket);
        return false;
    }

    if (enableBroadcast)
    {
        int broadcast = 1;
        if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST,
                       reinterpret_cast<const char *>(&broadcast), sizeof(broadcast)) != 0)
        {
            setError(error, "setsockopt(SO_BROADCAST): " + lastSocketError());
            closeNativeSocket(socket);
            return false;
        }
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (::bind(socket, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0)
    {
        setError(error, "bind(): " + lastSocketError());
        closeNativeSocket(socket);
        return false;
    }

#if defined(_WIN32)
    u_long nonBlocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonBlocking) != 0)
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) != 0)
#endif
    {
        setError(error, "non-blocking socket setup failed: " + lastSocketError());
        closeNativeSocket(socket);
        return false;
    }

    sockaddr_in bound{};
#if defined(_WIN32)
    int boundLength = sizeof(bound);
#else
    socklen_t boundLength = sizeof(bound);
#endif
    if (getsockname(socket, reinterpret_cast<sockaddr *>(&bound), &boundLength) == 0)
        _localPort = ntohs(bound.sin_port);
    else
        _localPort = port;

    {
        std::lock_guard<std::mutex> lock(_socketMutex);
        _socket = socketValue(socket);
    }
    return true;
}

bool LanTransport::startHost(uint16_t port, std::string *error)
{
    stop();
    _isHost = true;
    _remoteAddress.clear();
    _remotePort = 0;
    if (!openSocket(port, true, error))
    {
        _isHost = false;
        return false;
    }
    _running.store(true);
    _worker = std::thread(&LanTransport::workerLoop, this);
    pushEvent({TransportEventType::Started, {}, "", _localPort, {}});
    return true;
}

bool LanTransport::connectTo(const std::string &address, uint16_t port, std::string *error)
{
    if (address.empty() || port == 0)
    {
        setError(error, "remote address and port are required");
        return false;
    }

    stop();
    _isHost = false;
    _remoteAddress = address;
    _remotePort = port;
    if (!openSocket(0, false, error))
    {
        _remoteAddress.clear();
        _remotePort = 0;
        return false;
    }
    _running.store(true);
    _worker = std::thread(&LanTransport::workerLoop, this);
    pushEvent({TransportEventType::Started, {}, address, port, {}});
    return true;
}

bool LanTransport::sendRaw(const std::vector<uint8_t> &bytes, const std::string &address,
                           uint16_t port, std::string *error)
{
    if (bytes.empty())
    {
        setError(error, "cannot send empty packet");
        return false;
    }
    if (address.empty() || port == 0)
    {
        setError(error, "destination address and port are required");
        return false;
    }

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &destination.sin_addr) != 1)
    {
        setError(error, "destination must be an IPv4 address");
        return false;
    }

    std::lock_guard<std::mutex> lock(_socketMutex);
    NativeSocket socket = nativeSocket(_socket);
    if (socket == kInvalidSocket)
    {
        setError(error, "transport socket is not open");
        return false;
    }

#if defined(_WIN32)
    const int sent = sendto(socket, reinterpret_cast<const char *>(bytes.data()),
                            static_cast<int>(bytes.size()), 0,
                            reinterpret_cast<const sockaddr *>(&destination), sizeof(destination));
#else
    const ssize_t sent = sendto(socket, bytes.data(), bytes.size(), 0,
                                reinterpret_cast<const sockaddr *>(&destination), sizeof(destination));
#endif
    if (sent < 0 || static_cast<size_t>(sent) != bytes.size())
    {
        setError(error, "sendto(): " + lastSocketError());
        return false;
    }
    return true;
}

bool LanTransport::send(const Message &message, const std::string &address,
                        uint16_t port, std::string *error)
{
    std::vector<uint8_t> bytes;
    if (!encodeMessage(message, bytes, error))
        return false;

    const std::string targetAddress = address.empty() ? _remoteAddress : address;
    const uint16_t targetPort = port == 0 ? _remotePort : port;
    return sendRaw(bytes, targetAddress, targetPort, error);
}

bool LanTransport::broadcast(const Message &message, uint16_t discoveryPort, std::string *error)
{
    return send(message, "255.255.255.255", discoveryPort, error);
}

void LanTransport::pushEvent(TransportEvent event)
{
    std::lock_guard<std::mutex> lock(_eventMutex);
    _events.push_back(std::move(event));
}

void LanTransport::workerLoop()
{
    while (_running.load())
    {
        NativeSocket socket;
        {
            std::lock_guard<std::mutex> lock(_socketMutex);
            socket = nativeSocket(_socket);
        }
        if (socket == kInvalidSocket)
            break;

        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket, &readSet);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 50000;

#if defined(_WIN32)
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
        const int ready = select(socket + 1, &readSet, nullptr, nullptr, &timeout);
#endif
        if (ready <= 0 || !FD_ISSET(socket, &readSet))
            continue;

        uint8_t buffer[65535];
        sockaddr_in source{};
#if defined(_WIN32)
        int sourceLength = sizeof(source);
        const int received = recvfrom(socket, reinterpret_cast<char *>(buffer), sizeof(buffer), 0,
                                      reinterpret_cast<sockaddr *>(&source), &sourceLength);
#else
        socklen_t sourceLength = sizeof(source);
        const ssize_t received = recvfrom(socket, buffer, sizeof(buffer), 0,
                                          reinterpret_cast<sockaddr *>(&source), &sourceLength);
#endif
        if (received <= 0)
        {
            if (!wouldBlock())
                pushEvent({TransportEventType::Error, {}, "", 0, "recvfrom(): " + lastSocketError()});
            continue;
        }

        char addressBuffer[INET_ADDRSTRLEN] = {};
        const char *address = inet_ntop(AF_INET, &source.sin_addr, addressBuffer, sizeof(addressBuffer));
        Message message;
        size_t consumed = 0;
        std::string error;
        if (!decodeMessage(buffer, static_cast<size_t>(received), message, consumed, &error) ||
            consumed != static_cast<size_t>(received))
        {
            pushEvent({TransportEventType::Error, {}, address ? address : "", ntohs(source.sin_port),
                       error.empty() ? "invalid datagram" : error});
            continue;
        }
        pushEvent({TransportEventType::Message, std::move(message), address ? address : "",
                   ntohs(source.sin_port), {}});
    }
}

void LanTransport::poll(std::vector<TransportEvent> &events)
{
    std::lock_guard<std::mutex> lock(_eventMutex);
    while (!_events.empty())
    {
        events.push_back(std::move(_events.front()));
        _events.pop_front();
    }
}

void LanTransport::stop()
{
    _running.store(false);
    if (_worker.joinable())
        _worker.join();

    NativeSocket socket;
    {
        std::lock_guard<std::mutex> lock(_socketMutex);
        socket = nativeSocket(_socket);
        _socket = -1;
    }
    closeNativeSocket(socket);
    _localPort = 0;
    _isHost = false;
    _remoteAddress.clear();
    _remotePort = 0;
}

} // namespace nsv2::network

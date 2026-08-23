#include "LanNetworkRuntime.hpp"

namespace nsv2::network
{

LanSession &sharedLanSession()
{
    static LanSession session;
    return session;
}

} // namespace nsv2::network

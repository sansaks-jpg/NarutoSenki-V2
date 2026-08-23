#pragma once

#include "LanSession.hpp"

namespace nsv2::network
{

// The battle scene replaces the lobby scene, so the active session must outlive
// NetworkLobbyLayer. Access it only from the main thread except for transport internals.
LanSession &sharedLanSession();

} // namespace nsv2::network

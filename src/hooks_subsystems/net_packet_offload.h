#pragma once

#ifndef NET_PACKET_OFFLOAD_H
#define NET_PACKET_OFFLOAD_H

#include <cstdint>

namespace NetPacketOffload {

// Initialize network packet offloading detours and spawn helper threads
bool Init();

// Shut down hooks and join threads
void Shutdown();


} // namespace NetPacketOffload

#endif // NET_PACKET_OFFLOAD_H

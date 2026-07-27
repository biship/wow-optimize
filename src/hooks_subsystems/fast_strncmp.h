#pragma once

// ============================================================================
// Module: fast_strncmp.h
// ============================================================================









#include <cstdint>

bool InstallFastStrncmp();
void UninstallFastStrncmp();
void GetFastStrncmpStats(uint64_t* calls);

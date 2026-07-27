#pragma once

// ============================================================================
// Module: version_checker.h
// ============================================================================









#include <windows.h>

bool VersionChecker_Init();
void VersionChecker_Shutdown();
bool VersionChecker_GetLatestVersion(char* out, size_t outLen);

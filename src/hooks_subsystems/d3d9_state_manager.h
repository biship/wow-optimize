#pragma once

// ============================================================================
// Module: d3d9_state_manager.h
// ============================================================================










bool InstallD3D9StateManager(void);
void ShutdownD3D9StateManager(void);
void OnFrameD3D9StateManager(DWORD mainThreadId);
bool IsD3D9DeviceHooked(void);

extern volatile LONG g_deviceResetCounter;

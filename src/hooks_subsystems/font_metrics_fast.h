#pragma once

// ============================================================================
// Module: font_metrics_fast.h
// ============================================================================










#include <cstdint>

bool InstallFontMetricsFast();
void ShutdownFontMetricsFast();
void FontMetrics_OnFrame();
extern "C" void FontMetrics_GetStats(long* widthCalls, long* heightCalls);

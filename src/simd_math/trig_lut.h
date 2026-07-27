#pragma once

// ============================================================================
// Module: trig_lut.h
// ============================================================================










void InitTrigLUT();
bool IsTrigLutInitialized();
void FastSinCos4(const float* angles, float* outSin, float* outCos);
#pragma once

// ============================================================================
// Module: ui_frame_batch.h
// ============================================================================










// UI Frame Update Batching
bool InstallUIFrameBatching();
void GetUIFrameBatchStats(long* batched, long* individual, long* frames);
void ShutdownUIFrameBatching();

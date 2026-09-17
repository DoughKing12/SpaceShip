#pragma once

#include "types.h"

// Synthesized PCM byte sounds (PlaySound).
void initSounds();
void playSound(SoundId id);
void playPrioritySound(SoundId id, int blockFrames);   // unused — kept for API completeness

// MP3 effects loaded from the /sounds folder (MCI), played on a background thread.
void initMciSounds();
void playMci(const char* alias, int limitMs = 0);

// Background thread that drains shootSfxPending.
void initSoundThread();

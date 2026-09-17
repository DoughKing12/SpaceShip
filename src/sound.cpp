#include "sound.h"
#include <atomic>

std::atomic<bool> shootSfxPending{false};
int prioritySoundTimer = 0;
int laserSoundCooldown = 0;

// --- Sound data (file-private) ---
static std::vector<BYTE> soundData[SND_COUNT];

// --- Wav builder ---
static void buildWav(std::vector<BYTE>& out, const std::vector<double>& samples) {
    const int sampleRate = 22050;
    const WORD channels = 1, bits = 8;
    const DWORD dataSize = (DWORD)samples.size();
    const DWORD byteRate = sampleRate * channels * bits / 8;
    const WORD blockAlign = channels * bits / 8;
    out.resize(44 + dataSize);
    memcpy(&out[0], "RIFF", 4);
    DWORD riff = 36 + dataSize; memcpy(&out[4], &riff, 4);
    memcpy(&out[8], "WAVE", 4);
    memcpy(&out[12], "fmt ", 4);
    DWORD fmt = 16; memcpy(&out[16], &fmt, 4);
    WORD pcm = 1; memcpy(&out[20], &pcm, 2);
    memcpy(&out[22], &channels, 2);
    DWORD sr = sampleRate; memcpy(&out[24], &sr, 4);
    memcpy(&out[28], &byteRate, 4);
    memcpy(&out[32], &blockAlign, 2);
    memcpy(&out[34], &bits, 2);
    memcpy(&out[36], "data", 4);
    memcpy(&out[40], &dataSize, 4);
    for (size_t i = 0; i < samples.size(); i++) {
        double v = samples[i];
        if (v > 1.0) v = 1.0;
        if (v < -1.0) v = -1.0;
        out[44 + i] = (BYTE)((v + 1.0) * 127.5);
    }
}

static void appendTone(std::vector<double>& s, double freq, double dur, double vol = 0.6, double fade = 0.04) {
    const int sr = 22050;
    int n = (int)(sr * dur);
    int envN = (int)(fade * sr);
    double ph = 0;
    for (int i = 0; i < n; i++) {
        ph += 2.0 * PI * freq / sr;
        double e = 1.0;
        if (i < envN) e = (double)i / envN;
        if (i > n - envN) e = (double)(n - i) / envN;
        double v = sin(ph) * e + 0.2 * sin(2.0 * ph) * e;
        s.push_back(v * vol);
    }
}

static void appendNoteSeq(std::vector<double>& s, const double* notes, int count, double dur, double gap) {
    const int sr = 22050;
    for (int i = 0; i < count; i++) {
        appendTone(s, notes[i], dur, 0.6);
        s.insert(s.end(), (int)(sr * gap), 0.0);
    }
}

// --- Synthesized PCM sounds ---
void initSounds() {
    const int sr = 22050;

    // Laser: descending zap with a bit of noise
    {
        std::vector<double> s;
        int n = (int)(sr * 0.14);
        double ph = 0;
        for (int i = 0; i < n; i++) {
            double p = i / (double)n;
            double f = 1500.0 - 1200.0 * p;
            ph += 2.0 * PI * f / sr;
            double env = 1.0 - p;
            env *= env;
            double v = sin(ph) * env + (randf() - 0.5) * 0.25 * env;
            s.push_back(v * 0.7);
        }
        buildWav(soundData[SND_LASER], s);
    }

    // Boss warning: urgent rising siren chirps
    {
        std::vector<double> s;
        double notes[4] = { 500, 675, 850, 1050 };
        for (int k = 0; k < 8; k++) {
            double mult = (k >= 4 ? 1.3 : 1.0);
            appendTone(s, notes[k % 4] * mult, 0.16, 0.55, 0.02);
            s.insert(s.end(), (int)(sr * 0.03), 0.0);
        }
        buildWav(soundData[SND_BOSS_WARN], s);
    }

    // Level up: ascending arpeggio
    {
        std::vector<double> s;
        double notes[4] = { 523.25, 659.25, 783.99, 1046.50 };
        appendNoteSeq(s, notes, 4, 0.13, 0.03);
        buildWav(soundData[SND_LEVEL_UP], s);
    }

    // High score: happy fanfare melody
    {
        std::vector<double> s;
        double notes[6] = { 261.63, 329.63, 392.00, 523.25, 659.25, 783.99 };
        appendNoteSeq(s, notes, 6, 0.15, 0.02);
        buildWav(soundData[SND_HISCORE], s);
    }

    // Explosion: filtered noise burst with rumble (kept quiet)
    {
        std::vector<double> s;
        int n = (int)(sr * 0.4);
        double lp = 0;
        for (int i = 0; i < n; i++) {
            double p = i / (double)n;
            double e = 1.0 - p;
            e *= e;
            double noise = (randf() - 0.5);
            lp += (noise - lp) * 0.12;
            double v = lp * 2.0 * e;
            v += sin(2.0 * PI * 50.0 * i / sr) * e * 0.25;
            s.push_back(v * 0.35);
        }
        buildWav(soundData[SND_EXPLOSION], s);
    }

    // Power-up pickup: double blip
    {
        std::vector<double> s;
        double notes[2] = { 880.0, 1318.0 };
        appendTone(s, notes[0], 0.07, 0.6);
        appendTone(s, notes[1], 0.10, 0.6);
        buildWav(soundData[SND_POWERUP], s);
    }
}

void playSound(SoundId id) {
    if (prioritySoundTimer > 0) return;
    if (soundData[id].empty()) return;
    PlaySoundA((LPCSTR)soundData[id].data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void playPrioritySound(SoundId id, int blockFrames) {
    if (soundData[id].empty()) return;
    prioritySoundTimer = blockFrames;
    PlaySoundA((LPCSTR)soundData[id].data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// --- MP3 Sound Effects (from /sounds folder) ---
void initMciSounds() {
    if (soundDir[0] == 0) return;
    char path[MAX_PATH];
    char cmd[MAX_PATH + 32];
    snprintf(path, sizeof(path), "%sshoot.mp3", soundDir);
    snprintf(cmd, sizeof(cmd), "open \"%s\" type mpegvideo alias sfxshoot", path);
    mciSendStringA(cmd, NULL, 0, NULL);
    mciSendStringA("setaudio sfxshoot volume to 250", NULL, 0, NULL);
    snprintf(path, sizeof(path), "%swarning.mp3", soundDir);
    snprintf(cmd, sizeof(cmd), "open \"%s\" type mpegvideo alias sfxwarn", path);
    mciSendStringA(cmd, NULL, 0, NULL);
}

void playMci(const char* alias, int limitMs) {
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "seek %s to 0", alias);
    mciSendStringA(cmd, NULL, 0, NULL);
    if (limitMs > 0) {
        snprintf(cmd, sizeof(cmd), "play %s to %d", alias, limitMs);
    } else {
        snprintf(cmd, sizeof(cmd), "play %s", alias);
    }
    mciSendStringA(cmd, NULL, 0, NULL);
}

// --- Off-thread sound playback (keeps MCI calls off the game loop) ---
static HANDLE hSoundThread = NULL;

static DWORD WINAPI soundThreadProc(LPVOID) {
    for (;;) {
        if (shootSfxPending.load(std::memory_order_relaxed)) {
            shootSfxPending.store(false, std::memory_order_relaxed);
            playMci("sfxshoot");
        }
        Sleep(1);
    }
    return 0;
}

void initSoundThread() {
    hSoundThread = CreateThread(NULL, 0, soundThreadProc, NULL, 0, NULL);
}

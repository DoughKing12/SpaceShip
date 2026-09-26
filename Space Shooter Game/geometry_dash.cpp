#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <commdlg.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <vector>
#include <map>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>

static const int WINDOW_W = 960;
static const int WINDOW_H = 540;
static const int CELL = 40;
static const int ROWS = 12;
static const int FLOOR_Y = ROWS * CELL;
static const int SR = 22050;
static const double PHYS_DT = 1.0 / 240.0;
static const double SPEED = 430.0;
static const double GRAV = 2600.0;
static const double JUMP_V = 720.0;
static const double TERM_V = 1500.0;
static const double SHIP_GRAV = 1800.0;
static const double SHIP_UP = -3800.0;
static const double SHIP_VMAX = 600.0;
static const double UFO_BURST = -560.0;
static const double UFO_FALL = 850.0;
static const double P_HALF = 18.0;
static const double SCREEN_PX = 200.0;
static const double START_X = 60.0;
static const double ORB_R = 48.0;
static const int BPM = 140;
static const int LEVELS = 50;
static const double PI = 3.14159265358979;

static double clampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static double frand() { return (double)rand() / RAND_MAX; }

static COLORREF hslToColor(int h, int s, int l) {
    h = ((h % 360) + 360) % 360;
    s = imin(imax(s, 0), 100);
    l = imin(imax(l, 0), 100);
    double ss = s / 100.0, ll = l / 100.0;
    double c = (1.0 - fabs(2.0 * ll - 1.0)) * ss;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double m = ll - c / 2.0;
    double r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return RGB((int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255));
}

static COLORREF lerpColor(COLORREF a, COLORREF b, double t) {
    t = clampD(t, 0.0, 1.0);
    int r = (int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t);
    int g = (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t);
    int bl = (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t);
    return RGB(r, g, bl);
}

static COLORREF brighten(COLORREF c, int amt) {
    int r = imin(255, GetRValue(c) + amt);
    int g = imin(255, GetGValue(c) + amt);
    int b = imin(255, GetBValue(c) + amt);
    return RGB(r, g, b);
}

static void buildWav(std::vector<BYTE>& out, const std::vector<double>& samples) {
    const WORD channels = 1, bits = 8;
    const DWORD dataSize = (DWORD)samples.size();
    const DWORD byteRate = SR * channels * bits / 8;
    const WORD blockAlign = channels * bits / 8;
    out.resize(44 + dataSize);
    memcpy(&out[0], "RIFF", 4);
    DWORD riff = 36 + dataSize; memcpy(&out[4], &riff, 4);
    memcpy(&out[8], "WAVE", 4);
    memcpy(&out[12], "fmt ", 4);
    DWORD fmt = 16; memcpy(&out[16], &fmt, 4);
    WORD pcm = 1; memcpy(&out[20], &pcm, 2);
    memcpy(&out[22], &channels, 2);
    DWORD sr = SR; memcpy(&out[24], &sr, 4);
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

static void appendSweep(std::vector<double>& s, double f0, double f1, double dur, double vol, double fade, bool square) {
    int n = (int)(SR * dur);
    int fn = (int)(SR * fade);
    if (fn > n) fn = n;
    double ph = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / (double)n;
        double f = f0 + (f1 - f0) * t;
        ph += f / SR;
        double e = 1.0;
        if (i < fn) e = (double)i / fn;
        if (i > n - fn) e = (double)(n - i) / fn;
        double v = square ? (fmod(ph, 1.0) < 0.5 ? 1.0 : -1.0) : sin(2.0 * PI * ph);
        s.push_back(v * e * vol);
    }
}

static void appendSilence(std::vector<double>& s, double dur) {
    s.insert(s.end(), (size_t)(SR * dur), 0.0);
}

enum SfxId { SFX_JUMP, SFX_DIE, SFX_ORB, SFX_PORTAL, SFX_WIN, SFX_CLICK, SFX_COIN, SFX_FLIP, SFX_BUY, SFX_LOCKED, SFX_COUNT };
static std::vector<BYTE> sfxData[SFX_COUNT];
static bool muted = false;

static void genSounds() {
    { std::vector<double> s; appendSweep(s, 420, 980, 0.09, 0.5, 0.02, true); buildWav(sfxData[SFX_JUMP], s); }
    {
        std::vector<double> s;
        int n = (int)(SR * 0.5);
        double ph = 0;
        for (int i = 0; i < n; i++) {
            double t = (double)i / SR;
            double f = 420.0 * exp(-t * 6.0) + 55.0;
            ph += f / SR;
            double sq = fmod(ph, 1.0) < 0.5 ? 1.0 : -1.0;
            double noise = frand() * 2.0 - 1.0;
            s.push_back((sq * 0.45 + noise * 0.55) * exp(-t * 7.0) * 0.85);
        }
        buildWav(sfxData[SFX_DIE], s);
    }
    {
        std::vector<double> s;
        appendSweep(s, 988, 988, 0.07, 0.5, 0.015, false);
        appendSilence(s, 0.02);
        appendSweep(s, 1319, 1319, 0.13, 0.45, 0.03, false);
        buildWav(sfxData[SFX_ORB], s);
    }
    { std::vector<double> s; appendSweep(s, 300, 1500, 0.35, 0.45, 0.05, false); buildWav(sfxData[SFX_PORTAL], s); }
    {
        std::vector<double> s;
        double notes[4] = { 523.25, 659.25, 784.0, 1046.5 };
        for (int i = 0; i < 4; i++) {
            appendSweep(s, notes[i], notes[i], 0.14, 0.5, 0.02, false);
            appendSilence(s, 0.02);
        }
        buildWav(sfxData[SFX_WIN], s);
    }
    { std::vector<double> s; appendSweep(s, 1000, 1000, 0.05, 0.3, 0.01, true); buildWav(sfxData[SFX_CLICK], s); }
    {
        std::vector<double> s;
        appendSweep(s, 1174, 1174, 0.08, 0.45, 0.02, false);
        appendSilence(s, 0.03);
        appendSweep(s, 1568, 1568, 0.22, 0.45, 0.05, false);
        buildWav(sfxData[SFX_COIN], s);
    }
    {
        std::vector<double> s;
        appendSweep(s, 659.25, 659.25, 0.08, 0.42, 0.015, false);
        appendSilence(s, 0.01);
        appendSweep(s, 880.0, 880.0, 0.08, 0.42, 0.015, false);
        appendSilence(s, 0.01);
        appendSweep(s, 1318.5, 1318.5, 0.2, 0.45, 0.05, false);
        buildWav(sfxData[SFX_BUY], s);
    }
    {
        std::vector<double> s;
        appendSweep(s, 180, 140, 0.22, 0.45, 0.03, true);
        buildWav(sfxData[SFX_LOCKED], s);
    }
    {
        std::vector<double> s;
        appendSweep(s, 620, 1240, 0.1, 0.4, 0.02, false);
        appendSilence(s, 0.01);
        appendSweep(s, 1240, 620, 0.14, 0.4, 0.02, false);
        buildWav(sfxData[SFX_FLIP], s);
    }
}

static void playSfx(int id) {
    if (muted) return;
    if (sfxData[id].empty()) return;
    PlaySoundA((LPCSTR)sfxData[id].data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

static void genChiptune(std::vector<double>& s, int bpm, double semi, int variant) {
    const double spb = 60.0 / (double)bpm;
    const double st = spb / 4.0;
    const int bars = 8;
    const int steps = bars * 16;
    int total = (int)(SR * st * steps);
    s.assign(total + SR / 4, 0.0);
    double k = pow(2.0, semi / 12.0);
    std::vector<double> duck(s.size(), 1.0);
    auto at = [&](int step) { return (int)(SR * st * step); };
    auto markKick = [&](int p) {
        int n = (int)(SR * 0.24);
        for (int i = 0; i < n && p + i < (int)duck.size(); i++) {
            double target = 0.40 + 0.60 * pow((double)i / (double)n, 0.65);
            if (duck[p + i] > target) duck[p + i] = target;
        }
    };
    auto addKick = [&](int p) {
        markKick(p);
        int n = (int)(SR * 0.20);
        double ph = 0;
        for (int i = 0; i < n && p + i < (int)s.size(); i++) {
            double t = (double)i / SR;
            double f = 44.0 + 150.0 * exp(-t * 42.0);
            ph += 2.0 * PI * f / SR;
            double body = sin(ph) * exp(-t * 21.0);
            double click = (frand() * 2.0 - 1.0) * exp(-t * 480.0) * 0.55;
            s[p + i] += (body * 0.95 + click) * 0.88;
        }
    };
    auto addSnare = [&](int p) {
        int n = (int)(SR * 0.22);
        double ph = 0, ph2 = 0;
        for (int i = 0; i < n && p + i < (int)s.size(); i++) {
            double t = (double)i / SR;
            double noise = frand() * 2.0 - 1.0;
            ph += 2.0 * PI * 190.0 / SR;
            ph2 += 2.0 * PI * 330.0 / SR;
            double tone = sin(ph) * 0.35 + sin(ph2) * 0.18;
            s[p + i] += (noise * 0.78 + tone) * exp(-t * 17.0) * 0.56;
        }
    };
    auto addClap = [&](int p) {
        for (int r = 0; r < 3; r++) {
            int q = p + (int)(SR * 0.009 * r);
            int n = (int)(SR * 0.08);
            double prev = 0;
            for (int i = 0; i < n && q + i < (int)s.size(); i++) {
                double noise = frand() * 2.0 - 1.0;
                double hp = noise - prev;
                prev = noise;
                s[q + i] += hp * exp(-(double)i / SR * 50.0) * 0.30;
            }
        }
    };
    auto addHat = [&](int p, double vol, bool open) {
        int n = (int)(SR * (open ? 0.15 : 0.045));
        double prev = 0;
        for (int i = 0; i < n && p + i < (int)s.size(); i++) {
            double noise = frand() * 2.0 - 1.0;
            double hp = noise - prev;
            prev = noise;
            s[p + i] += hp * exp(-(double)i / SR * (open ? 24.0 : 95.0)) * vol;
        }
    };
    auto addBass = [&](int p, double f, double dur) {
        int n = (int)(SR * dur);
        double ph = 0;
        for (int i = 0; i < n && p + i < (int)s.size(); i++) {
            ph += f / SR;
            double saw = 2.0 * fmod(ph, 1.0) - 1.0;
            double sq = fmod(ph, 1.0) < 0.5 ? 1.0 : -1.0;
            double env = (i < 90 ? (double)i / 90.0 : 1.0) * (1.0 - (double)i / n * 0.30);
            s[p + i] += (saw * 0.30 + sq * 0.28 + sin(2.0 * PI * ph) * 0.38) * env * 0.52 * duck[p + i];
        }
    };
    auto addArp = [&](int p, double f, double dur) {
        int n = (int)(SR * dur);
        double ph = 0;
        for (int i = 0; i < n && p + i < (int)s.size(); i++) {
            ph += f / SR;
            double sq = fmod(ph, 1.0) < 0.32 ? 1.0 : -1.0;
            double env = (i < 60 ? (double)i / 60.0 : 1.0) * (1.0 - (double)i / n * 0.5);
            s[p + i] += sq * env * 0.13 * duck[p + i];
        }
    };
    auto addLead = [&](int p, double f, double dur) {
        int n = (int)(SR * dur);
        double ph = 0;
        for (int i = 0; i < n && p + i < (int)s.size(); i++) {
            double t = (double)i / SR;
            double fv = f * (1.0 + 0.005 * sin(2.0 * PI * 6.5 * t));
            ph += fv / SR;
            double sq = fmod(ph, 1.0) < 0.46 ? 1.0 : -1.0;
            double att = i < 260 ? (double)i / 260.0 : 1.0;
            double env = att * (1.0 - (double)i / n * 0.50);
            s[p + i] += (sq * 0.72 + sin(2.0 * PI * ph) * 0.28) * env * 0.175 * duck[p + i];
        }
    };
    double roots[4] = { 110.0 * k, 87.31 * k, 65.41 * k, 98.0 * k };
    double chord[4][3] = { { 440.0 * k, 523.25 * k, 659.25 * k }, { 349.23 * k, 440.0 * k, 523.25 * k },
                           { 523.25 * k, 659.25 * k, 783.99 * k }, { 392.0 * k, 493.88 * k, 587.33 * k } };
    const double scale[7] = { 0.0, 2.0, 3.0, 5.0, 7.0, 8.0, 10.0 };
    int mel[4][8] = { { 4, 7, 6, 4, 2, 4, 2, 0 }, { 4, 5, 4, 2, 4, 5, 7, -1 },
                      { 7, 6, 4, 6, 4, 2, 4, -1 }, { 6, 4, 6, 7, 6, 4, 2, 4 } };
    auto degFreq = [&](int d, int oct) {
        int idx = ((d % 7) + 7) % 7;
        int up = d / 7;
        double semi2 = scale[idx] + 12.0 * (double)(up + oct);
        return 440.0 * k * pow(2.0, semi2 / 12.0);
    };
    for (int b = 0; b < bars; b++) {
        int ci = b % 4;
        int oct = (b >= 4) ? 1 : 0;
        for (int kk = 0; kk < 16; kk++) {
            int p = at(b * 16 + kk);
            bool kOn = (variant == 0) ? (kk % 4 == 0) : (kk % 4 == 0 || (variant == 2 && kk % 8 == 6));
            if (kOn) addKick(p);
            if (kk == 4 || kk == 12) {
                addSnare(p);
                if (variant == 2) addClap(p);
            }
            if (b == bars - 1 && kk >= 10 && kk % 2 == 0) addSnare(p);
            addHat(p, kk % 2 == 1 ? 0.10 : 0.05, false);
            if (variant >= 1 && kk == 14) addHat(p, 0.075, true);
            if (kk % 2 == 0) addBass(p, roots[ci], st * 2.0 * 0.85);
            int idx = (kk % 8 < 4) ? (kk % 4) : (3 - (kk % 4));
            if (variant != 1 || kk % 2 == 0) addArp(p, chord[ci][idx] * (kk >= 8 ? 2.0 : 1.0), st * 0.9);
            if (kk % 2 == 0) {
                int d = mel[ci][kk / 2];
                if (d >= 0) addLead(p, degFreq(d, oct), st * 1.9);
            }
        }
    }
    int d = (int)(SR * st * 3.0);
    for (int i = d; i < (int)s.size(); i++) s[i] += 0.32 * s[i - d];
    double mx = 0;
    for (size_t i = 0; i < s.size(); i++) { double a = fabs(s[i]); if (a > mx) mx = a; }
    if (mx > 0) for (size_t i = 0; i < s.size(); i++) s[i] = s[i] / mx * 0.92;
}

static bool musicReady = false;
static char musicPath[3][MAX_PATH];
static DWORD musicStartMs = 0;
static DWORD musicPauseMs = 0;
static bool musicWant = false;
static DWORD lastMusicCheck = 0;
static int curTrack = 0;
static const char* trackAlias[3] = { "bgm0", "bgm1", "bgm2" };
static int levelBpm[3] = { 140, 152, 126 };
static double levelSemi[3] = { 0.0, 3.0, -2.0 };

static void initMusic() {
    for (int t = 0; t < 3; t++) {
        std::vector<double> s;
        genChiptune(s, levelBpm[t], levelSemi[t], t);
        std::vector<BYTE> wav;
        buildWav(wav, s);
        char tmp[MAX_PATH];
        if (!GetTempPathA(MAX_PATH, tmp)) return;
        snprintf(musicPath[t], MAX_PATH, "%sgeometry_dash_music%d.wav", tmp, t);
        FILE* f = fopen(musicPath[t], "wb");
        if (!f) continue;
        fwrite(wav.data(), 1, wav.size(), f);
        fclose(f);
        char cmd[MAX_PATH + 64];
        snprintf(cmd, sizeof(cmd), "open \"%s\" type waveaudio alias %s", musicPath[t], trackAlias[t]);
        if (mciSendStringA(cmd, NULL, 0, NULL) == 0) musicReady = true;
    }
}

static void startMusic(int track) {
    musicWant = true;
    if (!musicReady || muted) return;
    curTrack = track % 3;
    mciSendStringA("stop bgm0", NULL, 0, NULL);
    mciSendStringA("stop bgm1", NULL, 0, NULL);
    mciSendStringA("stop bgm2", NULL, 0, NULL);
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "seek %s to 0", trackAlias[curTrack]);
    mciSendStringA(cmd, NULL, 0, NULL);
    snprintf(cmd, sizeof(cmd), "play %s", trackAlias[curTrack]);
    mciSendStringA(cmd, NULL, 0, NULL);
    musicStartMs = GetTickCount();
}

static void pauseMusic() {
    musicWant = false;
    musicPauseMs = GetTickCount();
    if (!musicReady) return;
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "pause %s", trackAlias[curTrack]);
    mciSendStringA(cmd, NULL, 0, NULL);
}

static void resumeMusic() {
    musicWant = true;
    if (!musicReady || muted) return;
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "play %s", trackAlias[curTrack]);
    mciSendStringA(cmd, NULL, 0, NULL);
    musicStartMs += GetTickCount() - musicPauseMs;
}

static void stopMusic() {
    musicWant = false;
    if (!musicReady) return;
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "stop %s", trackAlias[curTrack]);
    mciSendStringA(cmd, NULL, 0, NULL);
}

static bool pausedGlobal = false;

static void pollMusic() {
    if (!musicReady || !musicWant || muted || pausedGlobal) return;
    DWORD now = GetTickCount();
    if (now - lastMusicCheck < 200) return;
    lastMusicCheck = now;
    char cmd[48], buf[64];
    snprintf(cmd, sizeof(cmd), "status %s mode", trackAlias[curTrack]);
    buf[0] = 0;
    mciSendStringA(cmd, buf, sizeof(buf), NULL);
    if (strcmp(buf, "playing") != 0) {
        snprintf(cmd, sizeof(cmd), "seek %s to 0", trackAlias[curTrack]);
        mciSendStringA(cmd, NULL, 0, NULL);
        snprintf(cmd, sizeof(cmd), "play %s", trackAlias[curTrack]);
        mciSendStringA(cmd, NULL, 0, NULL);
        musicStartMs = GetTickCount();
    }
}

enum ObjType { OBJ_BLOCK, OBJ_SPIKE_UP, OBJ_SPIKE_DOWN, OBJ_ORB, OBJ_ORB_BLUE, OBJ_PAD, OBJ_PORTAL, OBJ_FINISH, OBJ_SHARD };
struct Obj { ObjType t; double x, y, w, h; int mode; };

static std::vector<Obj> objs;
static double finishX = 0;

static unsigned int lvlSeed;
static unsigned int rndU() { lvlSeed = lvlSeed * 1664525u + 1013904223u; return lvlSeed >> 8; }
static int rndR(int lo, int hi) { return lo + (int)(rndU() % (unsigned)(hi - lo + 1)); }
static int gLastAtom = -1;

static const char* adjWords[16] = { "Stereo", "Neon", "Pulse", "Cyber", "Turbo", "Pixel", "Blaze", "Frost",
                                    "Storm", "Echo", "Volt", "Lunar", "Solar", "Hyper", "Prime", "Ultra" };
static const char* nounWords[16] = { "Rush", "Circuit", "Dash", "Horizon", "Breaker", "Spiral", "Surge", "Vortex",
                                     "Mirage", "Craze", "Rift", "Bolt", "Fury", "Nova", "Echo", "Strike" };

static int levelTier(int idx) {
    if (idx < 10) return 0;
    if (idx < 20) return 1;
    if (idx < 30) return 2;
    if (idx < 38) return 3;
    if (idx < 46) return 4;
    return 5;
}

static int tierReward(int tier) {
    int r[6] = { 10, 15, 20, 25, 35, 50 };
    return r[imin(tier, 5)];
}

static void levelName(int idx, char* out, int sz) {
    snprintf(out, sz, "%s %s", adjWords[idx % 16], nounWords[(idx * 3) % 16]);
}

static int levelHue(int idx) { return (idx * 67) % 360; }

static void addSpike(int c, int r) { objs.push_back(Obj{ OBJ_SPIKE_UP, (double)c * CELL, (double)r * CELL, (double)CELL, (double)CELL, 0 }); }
static void addSpikeD(int c, int r) { objs.push_back(Obj{ OBJ_SPIKE_DOWN, (double)c * CELL, (double)r * CELL, (double)CELL, (double)CELL, 0 }); }
static void addBlock(int c0, int r0, int c1, int r1) {
    objs.push_back(Obj{ OBJ_BLOCK, (double)c0 * CELL, (double)r0 * CELL, (double)(c1 - c0 + 1) * CELL, (double)(r1 - r0 + 1) * CELL, 0 });
}
static void addOrb(int c, int r) { objs.push_back(Obj{ OBJ_ORB, (double)c * CELL, (double)r * CELL, (double)CELL, (double)CELL, 0 }); }
static void addOrbBlue(int c, int r) { objs.push_back(Obj{ OBJ_ORB_BLUE, (double)c * CELL, (double)r * CELL, (double)CELL, (double)CELL, 0 }); }
static void addPad(int c, int r) { objs.push_back(Obj{ OBJ_PAD, (double)c * CELL, (double)r * CELL, (double)CELL, (double)CELL / 2.0, 0 }); }
static void addPortal(int mode, int c) { objs.push_back(Obj{ OBJ_PORTAL, (double)c * CELL, 0.0, (double)CELL, (double)FLOOR_Y, mode }); }
static void addShard(int c, int r) { objs.push_back(Obj{ OBJ_SHARD, (double)c * CELL, (double)r * CELL, (double)CELL, (double)CELL, 0 }); }

static int emitCubeAtom(int s, int tier, int lastAtom) {
    int pool[28];
    int n = 0;
    auto add = [&](int id) { if (id != lastAtom) pool[n++] = id; };
    add(0); add(1); add(2); add(3); add(4); add(13); add(14); add(15); add(16);
    if (tier >= 1) { add(5); add(17); }
    if (tier >= 2) { add(6); add(9); add(12); add(18); add(19); }
    if (tier >= 3) { add(7); add(8); add(20); add(21); }
    if (tier >= 4) { add(10); add(11); }
    int pick = pool[rndR(0, n - 1)];
    gLastAtom = pick;
    switch (pick) {
    case 0: addSpike(s + 2, ROWS - 1); return s + 3;
    case 1: addSpike(s + 2, ROWS - 1); addSpike(s + 3, ROWS - 1); return s + 4;
    case 2: addSpike(s + 2, ROWS - 1); addSpike(s + 8, ROWS - 1); return s + 9;
    case 3: addSpike(s + 2, ROWS - 1); addSpike(s + 3, ROWS - 1); addSpike(s + 4, ROWS - 1); return s + 5;
    case 4: addBlock(s + 4, ROWS - 1, s + 8, ROWS - 1); return s + 9;
    case 5: addBlock(s + 4, ROWS - 2, s + 9, ROWS - 1); return s + 10;
    case 6: addBlock(s + 4, ROWS - 1, s + 6, ROWS - 1); addBlock(s + 8, ROWS - 2, s + 11, ROWS - 1); return s + 12;
    case 7: addBlock(s + 4, ROWS - 1, s + 7, ROWS - 1); addSpike(s + 9, ROWS - 1); addSpike(s + 10, ROWS - 1);
            addBlock(s + 12, ROWS - 1, s + 15, ROWS - 1); return s + 16;
    case 8: for (int i = 0; i < 5; i++) addSpike(s + 6 + i, ROWS - 1); addOrb(s + 6, 9); return s + 11;
    case 9: addPad(s + 6, ROWS - 1); addBlock(s + 10, ROWS - 2, s + 14, ROWS - 1); return s + 15;
    case 10: addBlock(s + 4, ROWS - 2, s + 6, ROWS - 1); addSpike(s + 8, ROWS - 1); addSpike(s + 9, ROWS - 1);
             addBlock(s + 11, ROWS - 2, s + 13, ROWS - 1); return s + 14;
    case 11: addSpike(s + 2, ROWS - 1); addSpike(s + 9, ROWS - 1); addSpike(s + 11, ROWS - 1); return s + 12;
    case 12: addBlock(s + 5, 0, s + 15, 0); addOrbBlue(s + 6, 10); addOrbBlue(s + 11, 2); addOrbBlue(s + 13, 2);
             return s + 21;
    case 13: addBlock(s + 4, ROWS - 1, s + 6, ROWS - 1); addSpike(s + 9, ROWS - 1); return s + 10;
    case 14: addSpike(s + 2, ROWS - 1); addSpike(s + 5, ROWS - 1); addSpike(s + 8, ROWS - 1); return s + 9;
    case 15: addBlock(s + 4, ROWS - 1, s + 7, ROWS - 1); addSpike(s + 5, ROWS - 2); return s + 8;
    case 16: addBlock(s + 4, ROWS - 1, s + 6, ROWS - 1); addBlock(s + 8, ROWS - 2, s + 10, ROWS - 1); return s + 11;
    case 17: addPad(s + 3, ROWS - 1); for (int i = 0; i < 4; i++) addSpike(s + 11 + i, ROWS - 1); return s + 16;
    case 18: addSpike(s + 4, ROWS - 1); addSpike(s + 5, ROWS - 1); addOrb(s + 9, 9); addSpike(s + 10, ROWS - 1); addSpike(s + 11, ROWS - 1); return s + 14;
    case 19: addSpikeD(s + 5, 9); addSpike(s + 9, ROWS - 1); addSpikeD(s + 15, 9); addSpike(s + 19, ROWS - 1); return s + 21;
    case 20: addPad(s + 3, ROWS - 1); addOrb(s + 6, 8); for (int i = 0; i < 5; i++) addSpike(s + 15 + i, ROWS - 1); return s + 21;
    case 21: addBlock(s, 0, s + 14, 1); addSpike(s + 4, ROWS - 1); addSpikeD(s + 9, 9); addSpike(s + 12, ROWS - 1); return s + 15;
    }
    return s + 3;
}

static void emitSpecial(int s, int len, int mode, int tier) {
    addBlock(s, 0, s + len, 1);
    if (mode != 0) addPortal(mode, s + 2);
    int c = s + 9;
    int end = s + len - 7;
    if (mode == 1) {
        int i = 0;
        while (c < end) {
            if (i % 2 == 0) { addSpike(c, ROWS - 1); if (tier >= 2 && rndR(0, 99) < 55) addSpike(c + 1, ROWS - 1); }
            else { addSpikeD(c, 2); if (tier >= 2 && rndR(0, 99) < 55) addSpikeD(c + 1, 2); }
            c += (tier >= 4 ? 5 : tier >= 1 ? 6 : 7);
            i++;
        }
    } else if (mode == 3) {
        int i = 0;
        while (c < end) {
            if (i % 2 == 0) addBlock(c, ROWS - 3, c + 1, ROWS - 1);
            else addBlock(c, 0, c + 1, 5);
            c += (tier >= 4 ? 5 : tier >= 1 ? 6 : 7);
            i++;
        }
    } else if (mode == 2) {
        while (c < end) {
            addSpike(c, ROWS - 1);
            if (tier >= 2 && rndR(0, 99) < 55) addSpike(c + 1, ROWS - 1);
            c += (tier >= 4 ? 6 : tier >= 1 ? 7 : 9);
        }
    } else {
        int i = 0;
        while (c < end) {
            int kind = rndR(0, 2);
            if (i % 2 == 0) {
                if (kind == 0) addBlock(c, 8, c + 1, ROWS - 1);
                else if (kind == 1) addSpike(c, ROWS - 1);
                else { addBlock(c, 9, c + 2, ROWS - 1); addSpike(c + 1, ROWS - 1); }
            } else {
                if (kind == 0) addBlock(c, 0, c + 1, 5);
                else addSpikeD(c, 2);
            }
            c += (tier >= 4 ? 5 : 6);
            i++;
        }
    }
    if (mode != 0) addPortal(0, s + len - 4);
}

static void genLevel(int idx) {
    objs.clear();
    lvlSeed = 2654435761u ^ ((unsigned)idx * 40503u + 98765u);
    int tier = levelTier(idx);
    int len = 300 + idx * 13;
    if (len > 920) len = 920;
    int gap = imax(3, 4 - tier / 2);
    if (idx % 10 >= 7) gap = imax(3, gap - 1);
    int cursor = 10;
    int lastSpecial = -200;
    while (cursor < len - 24) {
        int modesAllowed[4];
        int nm = 0;
        if (idx >= 3) modesAllowed[nm++] = 1;
        if (idx >= 8) modesAllowed[nm++] = 2;
        if (idx >= 12) modesAllowed[nm++] = 3;
        if (idx >= 16) modesAllowed[nm++] = 0;
        bool wantSpecial = nm > 0 && (cursor - lastSpecial) > 26 && (cursor < len - 64) && (rndR(0, 99) < 55 + tier * 5);
        if (wantSpecial) {
            int mode = modesAllowed[rndR(0, nm - 1)];
            int slen = rndR(26, 46);
            if (cursor + slen < len - 14) {
                emitSpecial(cursor, slen, mode, tier);
                if (rndR(0, 99) < 70) addShard(cursor + slen + 1, 9);
                lastSpecial = cursor;
                cursor += slen + gap + 2;
                gLastAtom = -1;
                continue;
            }
        }
        int end = emitCubeAtom(cursor, tier, gLastAtom);
        cursor = end + gap;
        if (rndR(0, 99) < 45 && cursor < len - 8) addShard(end + gap / 2, rndR(9, 10));
    }
    finishX = (double)(len - 6) * CELL;
    objs.push_back(Obj{ OBJ_FINISH, finishX, 0.0, (double)(CELL * 2), (double)FLOOR_Y, 0 });
}

enum SkinStyle {
    SK_SQUARE, SK_ROUNDED, SK_BOLT, SK_STAR, SK_SKULL, SK_SWIRL,
    SK_PIXEL, SK_ARROW, SK_EYE, SK_RING, SK_CROWN, SK_DIAMOND,
    SK_TRIANGLE, SK_HEX, SK_HEART, SK_CIRCLE
};

struct Skin {
    const char* name;
    int price;
    int style;
    COLORREF main, dark, accent;
};

static Skin skins[16] = {
    { "Classic",  0,   SK_SQUARE,  RGB(0, 220, 120),  RGB(0, 140, 80),   RGB(255, 255, 255) },
    { "Rounded",  20,  SK_ROUNDED, RGB(60, 160, 255),  RGB(30, 90, 180),  RGB(200, 240, 255) },
    { "Bolt",     35,  SK_BOLT,    RGB(255, 210, 40),  RGB(180, 130, 10), RGB(40, 30, 10) },
    { "Star",     50,  SK_STAR,    RGB(255, 110, 200), RGB(180, 50, 130), RGB(255, 245, 160) },
    { "Skull",    70,  SK_SKULL,   RGB(235, 235, 240), RGB(150, 150, 165), RGB(30, 30, 40) },
    { "Swirl",    90,  SK_SWIRL,   RGB(150, 90, 255),  RGB(90, 40, 180),  RGB(120, 255, 255) },
    { "Pixel",    110, SK_PIXEL,   RGB(0, 255, 200),   RGB(0, 150, 130),  RGB(255, 80, 160) },
    { "Arrow",    140, SK_ARROW,   RGB(255, 90, 60),   RGB(180, 40, 20),  RGB(255, 240, 220) },
    { "Eye",      170, SK_EYE,     RGB(40, 255, 120),  RGB(10, 140, 60),  RGB(255, 255, 255) },
    { "Neon Ring",210, SK_RING,    RGB(20, 20, 35),    RGB(10, 10, 20),   RGB(0, 240, 255) },
    { "Crown",    260, SK_CROWN,   RGB(255, 190, 30),  RGB(170, 120, 10), RGB(255, 255, 255) },
    { "Diamond",  320, SK_DIAMOND, RGB(80, 200, 255),  RGB(20, 110, 180), RGB(240, 255, 255) },
    { "Triangle", 400, SK_TRIANGLE, RGB(255, 140, 50), RGB(170, 80, 10),  RGB(255, 245, 210) },
    { "Hexagon",  520, SK_HEX,     RGB(0, 230, 170),   RGB(0, 130, 100),  RGB(255, 255, 255) },
    { "Heart",    680, SK_HEART,   RGB(255, 80, 110),  RGB(180, 30, 60),  RGB(255, 205, 215) },
    { "Orb",      850, SK_CIRCLE,  RGB(130, 95, 255),  RGB(75, 40, 190),  RGB(255, 255, 255) },
};

static HBITMAP g_photo = NULL;
static int g_photoW = 0, g_photoH = 0;
static HWND g_hwnd = NULL;

static void freePhoto() {
    if (g_photo) { DeleteObject(g_photo); g_photo = NULL; }
    g_photoW = 0;
    g_photoH = 0;
}

static bool loadPhotoFile(const char* path) {
    WCHAR wpath[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wpath, MAX_PATH)) return false;
    ULONG_PTR gpt = 0;
    Gdiplus::GdiplusStartupInput gsi;
    if (Gdiplus::GdiplusStartup(&gpt, &gsi, NULL) != Gdiplus::Ok) return false;
    bool ok = false;
    Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(wpath, FALSE);
    if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
        HBITMAP h = NULL;
        if (bmp->GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &h) == Gdiplus::Ok && h) {
            freePhoto();
            g_photo = h;
            g_photoW = (int)bmp->GetWidth();
            g_photoH = (int)bmp->GetHeight();
            ok = g_photoW > 0 && g_photoH > 0;
        }
    }
    delete bmp;
    Gdiplus::GdiplusShutdown(gpt);
    if (!ok) freePhoto();
    return ok;
}

struct SaveData {
    int magic;
    int version;
    int best[LEVELS];
    int complete[LEVELS];
    int shards;
    int unlocked;
    unsigned int owned;
    int equipped;
    int muted;
    int customHue[3];
    char photoPath[260];
    int labOwned;
    unsigned char drawCells[144];
};
static SaveData save;

static void saveGame() {
    save.magic = 0x47445356;
    save.version = 2;
    save.muted = muted ? 1 : 0;
    FILE* f = fopen("geometry_dash_save.dat", "wb");
    if (!f) return;
    fwrite(&save, sizeof(save), 1, f);
    fclose(f);
}

static void loadGame() {
    memset(&save, 0, sizeof(save));
    save.magic = 0x47445356;
    save.unlocked = 0;
    save.owned = 1;
    save.equipped = 0;
    save.customHue[0] = 145;
    save.customHue[1] = 170;
    save.customHue[2] = 200;
    save.labOwned = 0;
    FILE* f = fopen("geometry_dash_save.dat", "rb");
    if (!f) return;
    size_t n = fread(&save, 1, sizeof(save), f);
    fclose(f);
    if (n < 8 || save.magic != 0x47445356) {
        memset(&save, 0, sizeof(save));
        save.owned = 1;
        save.customHue[0] = 145;
        save.customHue[1] = 170;
        save.customHue[2] = 200;
        return;
    }
    const size_t v2Size = 700;
    if (n < v2Size) {
        save.customHue[0] = 145;
        save.customHue[1] = 170;
        save.customHue[2] = 200;
        save.photoPath[0] = 0;
        save.labOwned = 0;
    } else if (n == v2Size) {
        save.labOwned = (save.owned & (1u << 12)) ? 1 : 0;
        save.owned &= ~(1u << 12);
        if (save.equipped == 12) save.equipped = 16;
    } else {
        save.labOwned = save.labOwned ? 1 : 0;
    }
    save.photoPath[sizeof(save.photoPath) - 1] = 0;
    save.version = 2;
    save.unlocked = imin(imax(save.unlocked, 0), LEVELS - 1);
    if (save.equipped == 16) {
        if (!save.labOwned) save.equipped = 0;
    } else {
        save.equipped = imin(imax(save.equipped, 0), 15);
    }
    save.owned |= 1;
    muted = save.muted != 0;
    if (save.photoPath[0]) loadPhotoFile(save.photoPath);
}

enum GameState { ST_MENU, ST_SELECT, ST_SHOP, ST_PLAY, ST_DEAD, ST_WIN, ST_LAB };
enum Mode { MODE_CUBE = 0, MODE_SHIP = 1, MODE_BALL = 2, MODE_UFO = 3 };

struct Player {
    double x, y, vy, rot;
    bool onGround;
    int mode;
    double grav;
};
static Player pl;

struct Particle {
    double x, y, vx, vy, life, maxLife, size;
    COLORREF col;
    int kind;
};
static std::vector<Particle> parts;

struct BgShape {
    double x, y, size, depth, bright;
    bool filled;
};
static std::vector<BgShape> bgShapes;

static GameState state = ST_MENU;
static int attempt = 1;
static bool paused = false;
static double deadT = 0;
static bool practice = false;
static bool ckptSet = false;
static double ckX = 0, ckY = 0, ckVy = 0, ckRot = 0, ckGrav = 1;
static bool ckGround = true;
static int ckMode = 0;
static double ckPct = 0;
static std::vector<char> ckTrig;
static double attemptT = 0;
static double totalTime = 0;
static double shake = 0;
static double pct = 0;
static int curLevel = 0;
static int shopSel = 0;
static bool spacePrev = false;
static double camX = 0;
static double gSpeed = 430.0;
static int g_drawPal = 1;
static HDC g_memDC = NULL;
static double trailT = 0;
static double plSquash = 0;
static std::vector<char> trigUsed;
static bool keys[256];
static bool keysPrev[256];
static bool mouseDown = false;
static bool jumpHeld = false;
static bool jumpHeldPrev = false;
static bool clickEdge = false;
static int mx = -100, my = -100;
static bool jumpEdgePending = false;
static double lastJumpT = -10.0;
static char toastText[64];
static double toastT = 0;
static int lastEarned = 0;

static void showToast(const char* t) {
    snprintf(toastText, sizeof(toastText), "%s", t);
    toastT = 2.2;
}

static void pickPhoto() {
    char file[MAX_PATH] = "";
    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFilter = "Images\0*.bmp;*.png;*.jpg;*.jpeg;*.gif\0All Files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Pick a cube photo";
    if (!GetOpenFileNameA(&ofn)) return;
    if (loadPhotoFile(file)) {
        snprintf(save.photoPath, sizeof(save.photoPath), "%s", file);
        saveGame();
        playSfx(SFX_CLICK);
        showToast("PHOTO LOADED");
    } else {
        showToast("COULD NOT LOAD THAT IMAGE");
    }
}

static bool g_fullscreen = false;
static RECT g_restoreRect = { 0, 0, 0, 0 };

static void toggleFullscreen() {
    if (!g_hwnd) return;
    if (!g_fullscreen) {
        GetWindowRect(g_hwnd, &g_restoreRect);
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(MonitorFromWindow(g_hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLong(g_hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_fullscreen = true;
        while (ShowCursor(FALSE) >= 0) {}
    } else {
        SetWindowLong(g_hwnd, GWL_STYLE, (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)) | WS_VISIBLE);
        SetWindowPos(g_hwnd, HWND_TOP, g_restoreRect.left, g_restoreRect.top,
                     g_restoreRect.right - g_restoreRect.left,
                     g_restoreRect.bottom - g_restoreRect.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        g_fullscreen = false;
        while (ShowCursor(TRUE) < 0) {}
    }
}

static void spawnParticle(double x, double y, double vx, double vy, double life, double size, COLORREF col, int kind) {
    Particle p;
    p.x = x; p.y = y; p.vx = vx; p.vy = vy;
    p.life = life; p.maxLife = life; p.size = size; p.col = col; p.kind = kind;
    parts.push_back(p);
}

static COLORREF customColor(int i) {
    if (i == 0) return hslToColor(save.customHue[0], 88, 55);
    if (i == 1) return hslToColor(save.customHue[1], 80, 33);
    return hslToColor(save.customHue[2], 100, 70);
}
static COLORREF skinMain() { return save.equipped == 16 ? customColor(0) : skins[save.equipped].main; }
static COLORREF skinDark() { return save.equipped == 16 ? customColor(1) : skins[save.equipped].dark; }
static COLORREF skinAccent() { return save.equipped == 16 ? customColor(2) : skins[save.equipped].accent; }

static void spawnDeath() {
    COLORREF cols[4] = { skinMain(), skinDark(), skinAccent(), RGB(255, 255, 255) };
    for (int i = 0; i < 40; i++) {
        double a = frand() * 2.0 * PI;
        double sp = 140 + frand() * 480;
        spawnParticle(pl.x, pl.y, cos(a) * sp, sin(a) * sp - 120,
                      0.5 + frand() * 0.7, 5 + frand() * 13, cols[rand() % 4], 1);
    }
    for (int i = 0; i < 12; i++) {
        double a = frand() * 2.0 * PI;
        spawnParticle(pl.x, pl.y, cos(a) * 100, sin(a) * 100, 0.4, 5 + frand() * 6, RGB(255, 230, 120), 2);
    }
    spawnParticle(pl.x, pl.y, 0, 0, 0.35, 20, RGB(255, 90, 90), 3);
    spawnParticle(pl.x, pl.y, 0, 0, 0.5, 74, RGB(255, 210, 90), 4);
}

static void spawnDust() {
    for (int i = 0; i < 6; i++) {
        spawnParticle(pl.x + (frand() - 0.5) * 24, FLOOR_Y - 2,
                      (frand() - 0.5) * 120, -60 - frand() * 110,
                      0.3 + frand() * 0.2, 4 + frand() * 5, RGB(230, 230, 240), 2);
    }
}

static void spawnSpark(double x, double y, COLORREF col) {
    for (int i = 0; i < 14; i++) {
        double a = frand() * 2.0 * PI;
        double sp = 60 + frand() * 240;
        spawnParticle(x, y, cos(a) * sp, sin(a) * sp, 0.35 + frand() * 0.3, 4 + frand() * 6, col, 2);
    }
}

static void spawnTrail() {
    int r = rand() % 5;
    COLORREF c = (r == 0) ? skinAccent() : ((r == 1) ? skinDark() : skinMain());
    spawnParticle(pl.x - 6 + (frand() - 0.5) * 10, pl.y + (frand() - 0.5) * 14,
                  -90 - frand() * 80, (frand() - 0.5) * 44,
                  0.42, 13 + frand() * 8, c, 0);
}

static void die() {
    if (state != ST_PLAY) return;
    state = ST_DEAD;
    deadT = 0;
    shake = 1.0;
    stopMusic();
    playSfx(SFX_DIE);
    spawnDeath();
    int p = (int)pct;
    if (p > save.best[curLevel]) { save.best[curLevel] = p; saveGame(); }
}

static void winGame() {
    if (state != ST_PLAY) return;
    state = ST_WIN;
    pct = 100;
    shake = 0.5;
    stopMusic();
    playSfx(SFX_WIN);
    int tier = levelTier(curLevel);
    int full = tierReward(tier);
    int reward = save.complete[curLevel] ? (full / 4 + 1) : full;
    save.shards += reward;
    lastEarned = reward;
    save.best[curLevel] = 100;
    save.complete[curLevel] = 1;
    if (curLevel == save.unlocked && curLevel < LEVELS - 1) save.unlocked = curLevel + 1;
    saveGame();
    for (int i = 0; i < 46; i++) {
        double a = frand() * 2.0 * PI;
        spawnParticle(pl.x, pl.y, cos(a) * (100 + frand() * 340), sin(a) * (100 + frand() * 340) - 150,
                      0.9 + frand() * 0.9, 6 + frand() * 10,
                      RGB(60 + rand() % 195, 60 + rand() % 195, 60 + rand() % 195), 1);
    }
}

static void startLevel(int idx) {
    curLevel = idx;
    genLevel(idx);
    gSpeed = 430.0 + levelTier(idx) * 16.0;
    state = ST_PLAY;
    attempt = 1;
    paused = false;
    pausedGlobal = false;
    parts.clear();
    pl.x = START_X;
    pl.y = FLOOR_Y - P_HALF;
    pl.vy = 0;
    pl.rot = 0;
    pl.onGround = true;
    pl.mode = MODE_CUBE;
    pl.grav = 1;
    attemptT = 0;
    pct = 0;
    trailT = 0;
    trigUsed.assign(objs.size(), 0);
    ckptSet = false;
    startMusic(idx % 3);
    playSfx(SFX_CLICK);
}

static void resetAttempt() {
    pl.x = START_X;
    pl.y = FLOOR_Y - P_HALF;
    pl.vy = 0;
    pl.rot = 0;
    pl.onGround = true;
    pl.mode = MODE_CUBE;
    pl.grav = 1;
    attemptT = 0;
    pct = 0;
    trailT = 0;
    plSquash = 0;
    parts.clear();
    trigUsed.assign(objs.size(), 0);
}

static void restoreCheckpoint() {
    pl.x = ckX; pl.y = ckY; pl.vy = ckVy; pl.rot = ckRot;
    pl.grav = ckGrav; pl.onGround = ckGround; pl.mode = ckMode;
    attemptT = 0;
    pct = ckPct;
    trailT = 0;
    plSquash = 0;
    parts.clear();
    trigUsed = ckTrig;
}

static bool rectOverlap(double ax, double ay, double aw, double ah, double bx, double by, double bw, double bh) {
    return ax < bx + bw - 0.6 && ax + aw > bx + 0.6 && ay < by + bh - 0.6 && ay + ah > by + 0.6;
}

static void moveX(double d) {
    pl.x += d;
    for (size_t i = 0; i < objs.size(); i++) {
        Obj& o = objs[i];
        if (o.t != OBJ_BLOCK) continue;
        if (rectOverlap(pl.x - P_HALF, pl.y - P_HALF, P_HALF * 2, P_HALF * 2, o.x, o.y, o.w, o.h)) {
            die();
            return;
        }
    }
}

static bool moveY(double d) {
    pl.y += d;
    bool landed = false;
    for (int iter = 0; iter < 4; iter++) {
        bool any = false;
        for (size_t i = 0; i < objs.size(); i++) {
            Obj& o = objs[i];
            if (o.t != OBJ_BLOCK) continue;
            if (rectOverlap(pl.x - P_HALF, pl.y - P_HALF, P_HALF * 2, P_HALF * 2, o.x, o.y, o.w, o.h)) {
                any = true;
                if (d > 0) {
                    pl.y = o.y - P_HALF;
                    if (pl.grav > 0) landed = true;
                } else if (d < 0) {
                    pl.y = o.y + o.h + P_HALF;
                    if (pl.grav < 0) landed = true;
                } else {
                    pl.y = (pl.y < o.y + o.h * 0.5) ? o.y - P_HALF : (o.y + o.h + P_HALF);
                }
                pl.vy = 0;
            }
        }
        if (!any) break;
    }
    if (pl.y + P_HALF >= FLOOR_Y) {
        pl.y = FLOOR_Y - P_HALF;
        if ((d > 0 || pl.vy > 0) && pl.grav > 0) landed = true;
        pl.vy = 0;
    }
    if (pl.y - P_HALF <= 0) {
        pl.y = P_HALF;
        if (pl.vy < 0) pl.vy = 0;
        if (pl.grav < 0) landed = true;
    }
    return landed;
}

static void checkHazards() {
    double hl = pl.x - 10, hr = pl.x + 10, ht = pl.y - 10, hb = pl.y + 10;
    for (size_t i = 0; i < objs.size(); i++) {
        Obj& o = objs[i];
        double sx = o.x - camX;
        if (sx > WINDOW_W + 200 || sx + o.w < -200) continue;
        if (o.t == OBJ_SPIKE_UP) {
            if (hl < o.x + 28 && hr > o.x + 12 && ht < o.y + 38 && hb > o.y + 14) { die(); return; }
        } else if (o.t == OBJ_SPIKE_DOWN) {
            if (hl < o.x + 28 && hr > o.x + 12 && ht < o.y + 26 && hb > o.y + 2) { die(); return; }
        }
    }
}

static void changeMode(int mode) {
    if (mode < 0 || mode > 3) return;
    if (pl.mode == mode) return;
    pl.mode = mode;
    pl.grav = 1;
    pl.vy = clampD(pl.vy, -260, 260);
    if (mode == MODE_CUBE) pl.rot = 0;
    playSfx(SFX_PORTAL);
    spawnSpark(pl.x, pl.y, RGB(120, 230, 255));
}

static void checkInteractions() {
    for (size_t i = 0; i < objs.size(); i++) {
        Obj& o = objs[i];
        double sx = o.x - camX;
        if (sx > WINDOW_W + 120 || sx + o.w < -120) continue;
        if (o.t == OBJ_ORB || o.t == OBJ_ORB_BLUE) {
            if (trigUsed[i]) continue;
            double cx = o.x + 20, cy = o.y + 20;
            double dx = pl.x - cx, dy = pl.y - cy;
            if (dx * dx + dy * dy < ORB_R * ORB_R && (jumpHeld || totalTime - lastJumpT < 0.16)) {
                trigUsed[i] = 1;
                if (o.t == OBJ_ORB) {
                    pl.vy = -JUMP_V * pl.grav;
                    playSfx(SFX_ORB);
                    spawnSpark(cx, cy, RGB(255, 225, 80));
                } else {
                    pl.grav = -pl.grav;
                    pl.vy = -120 * pl.grav;
                    playSfx(SFX_FLIP);
                    spawnSpark(cx, cy, RGB(90, 160, 255));
                }
                pl.onGround = false;
            }
        } else if (o.t == OBJ_PAD) {
            bool over = rectOverlap(pl.x - P_HALF, pl.y - P_HALF, P_HALF * 2, P_HALF * 2, o.x, o.y, o.w, o.h);
            if (over && !trigUsed[i]) {
                trigUsed[i] = 1;
                pl.vy = -JUMP_V * 1.08 * pl.grav;
                pl.onGround = false;
                playSfx(SFX_ORB);
                spawnSpark(o.x + 20, o.y, RGB(255, 225, 80));
            } else if (!over && trigUsed[i]) {
                trigUsed[i] = 0;
            }
        } else if (o.t == OBJ_SHARD) {
            if (!trigUsed[i] && rectOverlap(pl.x - P_HALF, pl.y - P_HALF, P_HALF * 2, P_HALF * 2, o.x + 4, o.y + 4, 32, 32)) {
                trigUsed[i] = 1;
                save.shards += 5;
                saveGame();
                playSfx(SFX_COIN);
                spawnSpark(o.x + 20, o.y + 20, RGB(255, 225, 80));
            }
        } else if (o.t == OBJ_PORTAL) {
            double cx = o.x + 20;
            if (fabs(pl.x - cx) < 26) changeMode(o.mode);
        }
    }
    if (pl.x >= finishX) winGame();
}

static void updatePlayer(double dt) {
    bool pressed = jumpEdgePending;
    jumpEdgePending = false;
    plSquash *= exp(-10.0 * dt);
    if (fabs(plSquash) < 0.01) plSquash = 0;
    switch (pl.mode) {
    case MODE_CUBE: {
        bool wasGround = pl.onGround;
        if (pl.onGround && jumpHeld) {
            pl.vy = -JUMP_V * pl.grav;
            pl.onGround = false;
            plSquash = 0.38;
            playSfx(SFX_JUMP);
            if (pl.grav > 0) spawnDust();
        }
        pl.vy += GRAV * pl.grav * dt;
        pl.vy = clampD(pl.vy, -TERM_V, TERM_V);
        moveX(gSpeed * dt);
        if (state != ST_PLAY) return;
        pl.onGround = moveY(pl.vy * dt);
        if (pl.onGround && !wasGround) {
            plSquash = -0.34;
            if (pl.grav > 0) spawnDust();
        }
        if (!pl.onGround) pl.rot += 640 * dt;
        else pl.rot = floor(pl.rot / 90.0 + 0.5) * 90.0;
        break;
    }
    case MODE_SHIP:
        pl.vy += (jumpHeld ? SHIP_UP : SHIP_GRAV) * dt;
        pl.vy = clampD(pl.vy, -SHIP_VMAX, SHIP_VMAX);
        moveX(gSpeed * dt);
        if (state != ST_PLAY) return;
        moveY(pl.vy * dt);
        pl.rot = pl.vy / SHIP_VMAX * 30.0;
        break;
    case MODE_BALL:
        if (pressed && pl.onGround) {
            pl.grav = -pl.grav;
            pl.vy = 60 * pl.grav;
            pl.onGround = false;
            playSfx(SFX_FLIP);
            spawnSpark(pl.x, pl.y, RGB(120, 230, 255));
        }
        pl.vy += GRAV * pl.grav * dt;
        pl.vy = clampD(pl.vy, -TERM_V, TERM_V);
        moveX(gSpeed * dt);
        if (state != ST_PLAY) return;
        pl.onGround = moveY(pl.vy * dt);
        if (pl.onGround) pl.rot += 470 * dt * pl.grav;
        else pl.rot += 760 * dt * pl.grav;
        break;
    case MODE_UFO:
        if (pressed) {
            pl.vy = UFO_BURST;
            playSfx(SFX_JUMP);
            spawnDust();
        }
        pl.vy += GRAV * dt;
        pl.vy = clampD(pl.vy, -700, UFO_FALL);
        moveX(gSpeed * dt);
        if (state != ST_PLAY) return;
        moveY(pl.vy * dt);
        pl.rot = pl.vy / UFO_FALL * 18.0;
        break;
    }
    if (state != ST_PLAY) return;
    checkHazards();
    if (state != ST_PLAY) return;
    checkInteractions();
    if (state != ST_PLAY) return;
    pct = clampD((pl.x - START_X) / (finishX - START_X) * 100.0, 0, 100);
}

static void updateParticles(double dt) {
    for (size_t i = 0; i < parts.size(); i++) {
        Particle& p = parts[i];
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        if (p.kind == 1) p.vy += 1400.0 * dt;
        if (p.kind == 2) p.vy += 300.0 * dt;
        p.life -= dt;
        if (p.life <= 0) {
            parts[i] = parts.back();
            parts.pop_back();
            i--;
        }
    }
}

static void updateBgShapes(double dt) {
    for (size_t i = 0; i < bgShapes.size(); i++) {
        BgShape& s = bgShapes[i];
        s.x -= gSpeed * s.depth * dt;
        if (s.x + s.size < -80) {
            s.x = WINDOW_W + 60 + frand() * 240;
            s.y = frand() * (FLOOR_Y - 60);
            s.size = 50 + frand() * 130;
        }
    }
}

static void initBgShapes() {
    bgShapes.clear();
    for (int i = 0; i < 14; i++) {
        BgShape s;
        s.x = frand() * (WINDOW_W + 300) - 150;
        s.y = frand() * (FLOOR_Y - 60);
        s.size = 50 + frand() * 140;
        s.depth = 0.12 + frand() * 0.30;
        s.bright = (frand() - 0.5) * 12.0;
        s.filled = (rand() % 2) == 0;
        bgShapes.push_back(s);
    }
}

static double beatPulse() {
    if (!musicReady || muted || state != ST_PLAY || paused) {
        return 0.35 + 0.25 * sin(totalTime * 2.0);
    }
    int bpm = levelBpm[curTrack % 3];
    DWORD el = GetTickCount() - musicStartMs;
    double ph = fmod(el / 1000.0 * (bpm / 60.0), 1.0);
    return pow(1.0 - ph, 3.0);
}

static void update(double dt) {
    totalTime += dt;
    if (toastT > 0) toastT -= dt;
    updateBgShapes(dt);
    pausedGlobal = paused;
    if (state == ST_PLAY && !paused) {
        attemptT += dt;
        updatePlayer(dt);
        if (state != ST_PLAY) return;
        updateParticles(dt);
        trailT += dt;
        if (trailT > 0.035) { trailT = 0; spawnTrail(); }
    } else if (state == ST_DEAD) {
        deadT += dt;
        updateParticles(dt);
        if (deadT > 1.05) {
            attempt++;
            if (practice && ckptSet) restoreCheckpoint();
            else resetAttempt();
            state = ST_PLAY;
            startMusic(curLevel % 3);
        }
    } else {
        updateParticles(dt);
    }
    shake *= exp(-6.0 * dt);
}

struct Btn { double x, y, w, h; };

static bool btnHover(const Btn& b) { return mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h; }
static bool btnClick(const Btn& b) { return clickEdge && btnHover(b); }

static bool jumpKeyNow() {
    return keys[VK_SPACE] || keys[VK_UP] || keys['W'] || keys['Z'] || keys[VK_SHIFT] || mouseDown;
}

static void startLevelFromSelect() {
    if (curLevel <= save.unlocked) startLevel(curLevel);
    else { playSfx(SFX_LOCKED); showToast("LOCKED - BEAT THE PREVIOUS LEVEL"); }
}

static void buyOrEquip(int skinIdx) {
    bool owned = (save.owned & (1u << skinIdx)) != 0;
    if (owned) {
        save.equipped = skinIdx;
        saveGame();
        playSfx(SFX_CLICK);
        showToast("EQUIPPED");
    } else if (save.shards >= skins[skinIdx].price) {
        save.shards -= skins[skinIdx].price;
        save.owned |= (1u << skinIdx);
        save.equipped = skinIdx;
        saveGame();
        playSfx(SFX_COIN);
        showToast("PURCHASED!");
    } else {
        playSfx(SFX_LOCKED);
        showToast("NOT ENOUGH SHARDS");
    }
}

static void frameInput() {
    bool jk = jumpKeyNow();
    bool jEdge = jk && !jumpHeldPrev;
    bool escEdge = keys[VK_ESCAPE] && !keysPrev[VK_ESCAPE];
    bool pEdge = keys['P'] && !keysPrev['P'];
    bool mEdge = keys['M'] && !keysPrev['M'];
    bool rEdge = keys['R'] && !keysPrev['R'];
    bool cEdge = keys['C'] && !keysPrev['C'];
    bool enterEdge = keys[VK_RETURN] && !keysPrev[VK_RETURN];
    bool leftEdge = keys[VK_LEFT] && !keysPrev[VK_LEFT];
    bool rightEdge = keys[VK_RIGHT] && !keysPrev[VK_RIGHT];
    bool upEdge = keys[VK_UP] && !keysPrev[VK_UP];
    bool downEdge = keys[VK_DOWN] && !keysPrev[VK_DOWN];
    bool spaceDown = keys[VK_SPACE];
    bool spaceEdge = spaceDown && !spacePrev;
    spacePrev = spaceDown;
    bool f11Edge = keys[VK_F11] && !keysPrev[VK_F11];

    if (f11Edge) toggleFullscreen();

    if (mEdge) {
        muted = !muted;
        saveGame();
        if (muted) pauseMusic();
        else if (state == ST_PLAY && !paused) resumeMusic();
        if (!muted) playSfx(SFX_CLICK);
    }
    if (jEdge && state == ST_PLAY && !paused) { jumpEdgePending = true; lastJumpT = totalTime; }

    if (state == ST_MENU) {
        if (escEdge && g_fullscreen) toggleFullscreen();
        if (btnClick(Btn{ 330, 250, 300, 62 })) { state = ST_SELECT; playSfx(SFX_CLICK); }
        else if (btnClick(Btn{ 330, 330, 300, 62 })) { state = ST_SHOP; shopSel = imin(15, save.equipped); playSfx(SFX_CLICK); }
        else if (spaceEdge || enterEdge) { state = ST_SELECT; playSfx(SFX_CLICK); }
    } else if (state == ST_SELECT) {
        if (leftEdge) { curLevel = imax(0, curLevel - 1); playSfx(SFX_CLICK); }
        if (rightEdge) { curLevel = imin(save.unlocked, curLevel + 1); playSfx(SFX_CLICK); }
        if (upEdge) { curLevel = imax(0, curLevel - 5); playSfx(SFX_CLICK); }
        if (downEdge) { curLevel = imin(save.unlocked, curLevel + 5); playSfx(SFX_CLICK); }
        if (enterEdge || spaceEdge) startLevelFromSelect();
        if (escEdge) { state = ST_MENU; playSfx(SFX_CLICK); }
        for (int i = 0; i < LEVELS; i++) {
            double cx = 70 + (i % 5) * 164, cy = 76 + (i / 5) * 42;
            if (clickEdge && mx >= cx && mx < cx + 150 && my >= cy && my < cy + 36) {
                curLevel = i;
                if (i <= save.unlocked) startLevel(i);
                else { playSfx(SFX_LOCKED); showToast("LOCKED"); }
                break;
            }
        }
    } else if (state == ST_SHOP) {
        if (leftEdge) { shopSel = imax(0, shopSel - 1); playSfx(SFX_CLICK); }
        if (rightEdge) { shopSel = imin(15, shopSel + 1); playSfx(SFX_CLICK); }
        if (upEdge) { shopSel = imax(0, shopSel - 4); playSfx(SFX_CLICK); }
        if (downEdge) { shopSel = imin(15, shopSel + 4); playSfx(SFX_CLICK); }
        if (enterEdge || spaceEdge) buyOrEquip(shopSel);
        if (escEdge) { state = ST_MENU; playSfx(SFX_CLICK); }
        for (int i = 0; i < 16; i++) {
            double cx = 48 + (i % 4) * 216, cy = 76 + (i / 4) * 106;
            if (clickEdge && mx >= cx && mx < cx + 200 && my >= cy && my < cy + 100) {
                shopSel = i;
                buyOrEquip(i);
                break;
            }
        }
        if (btnClick(Btn{ 230, 496, 500, 36 })) {
            if (save.labOwned) {
                state = ST_LAB;
                playSfx(SFX_CLICK);
            } else if (save.shards >= 10000) {
                save.shards -= 10000;
                save.labOwned = 1;
                saveGame();
                state = ST_LAB;
                playSfx(SFX_BUY);
                showToast("COLOR LAB UNLOCKED!");
            } else {
                playSfx(SFX_LOCKED);
                showToast("NEED 10000 SHARDS");
            }
        }
    } else if (state == ST_PLAY) {
        if (escEdge || pEdge) {
            paused = !paused;
            pausedGlobal = paused;
            playSfx(SFX_CLICK);
            if (paused) pauseMusic(); else resumeMusic();
        } else if (rEdge) {
            attempt++;
            resetAttempt();
            if (!paused) startMusic(curLevel % 3);
        }
        if (!paused && cEdge && practice) {
            ckptSet = true;
            ckX = pl.x; ckY = pl.y; ckVy = pl.vy; ckRot = pl.rot;
            ckGrav = pl.grav; ckGround = pl.onGround; ckMode = pl.mode;
            ckPct = pct; ckTrig = trigUsed;
            playSfx(SFX_COIN);
            showToast("CHECKPOINT SAVED");
        }
        if (paused) {
            if (btnClick(Btn{ 330, 250, 300, 54 })) { paused = false; pausedGlobal = false; resumeMusic(); }
            else if (btnClick(Btn{ 330, 318, 300, 54 })) {
                paused = false; pausedGlobal = false;
                attempt++; resetAttempt(); startMusic(curLevel % 3);
            } else if (btnClick(Btn{ 330, 386, 300, 54 })) {
                paused = false; pausedGlobal = false; state = ST_SELECT; stopMusic(); playSfx(SFX_CLICK);
            } else if (btnClick(Btn{ 330, 454, 300, 50 })) {
                practice = !practice;
                if (!practice) ckptSet = false;
                playSfx(SFX_CLICK);
                showToast(practice ? "PRACTICE ON - PRESS C TO SAVE" : "PRACTICE OFF");
            }
        }
    } else if (state == ST_WIN) {
        if (btnClick(Btn{ 330, 330, 300, 54 })) {
            if (curLevel + 1 <= save.unlocked && curLevel < LEVELS - 1) startLevel(curLevel + 1);
            else startLevel(curLevel);
        } else if (btnClick(Btn{ 330, 396, 300, 54 })) {
            startLevel(curLevel);
        } else if (btnClick(Btn{ 330, 462, 300, 50 })) {
            state = ST_SELECT; playSfx(SFX_CLICK);
        } else if (enterEdge || spaceEdge) {
            if (curLevel + 1 <= save.unlocked && curLevel < LEVELS - 1) startLevel(curLevel + 1);
            else startLevel(curLevel);
        } else if (escEdge) { state = ST_SELECT; playSfx(SFX_CLICK); }
    } else if (state == ST_LAB) {
        static bool labDrag = false;
        static bool drawDirty = false;
        if (mouseDown) {
            for (int sIdx = 0; sIdx < 3; sIdx++) {
                double sy = 200 + sIdx * 58;
                if (mx >= 300 && mx < 700 && my >= sy && my < sy + 30) {
                    int hue = (int)((mx - 300) / 399.0 * 359.0);
                    save.customHue[sIdx] = imin(imax(hue, 0), 359);
                    labDrag = true;
                }
            }
            if (mx >= 740 && mx < 932 && my >= 150 && my < 342) {
                int cc = (mx - 740) / 16, rr = (my - 150) / 16;
                if (cc >= 0 && cc < 12 && rr >= 0 && rr < 12) {
                    unsigned char v = (g_drawPal == 6) ? 0 : (unsigned char)g_drawPal;
                    if (save.drawCells[rr * 12 + cc] != v) {
                        save.drawCells[rr * 12 + cc] = v;
                        drawDirty = true;
                    }
                }
            }
        }
        if (!mouseDown && labDrag) {
            labDrag = false;
            saveGame();
            playSfx(SFX_CLICK);
        }
        if (!mouseDown && drawDirty) {
            drawDirty = false;
            saveGame();
            playSfx(SFX_CLICK);
        }
        if (clickEdge) {
            for (int sw = 0; sw < 6; sw++) {
                int bx = 740 + sw * 30;
                if (mx >= bx && mx < bx + 26 && my >= 118 && my < 144) {
                    g_drawPal = sw + 1;
                    playSfx(SFX_CLICK);
                }
            }
        }
        if (btnClick(Btn{ 120, 372, 220, 46 })) {
            pickPhoto();
        } else if (btnClick(Btn{ 360, 372, 220, 46 })) {
            freePhoto();
            save.photoPath[0] = 0;
            saveGame();
            playSfx(SFX_CLICK);
            showToast("PHOTO CLEARED");
        } else if (btnClick(Btn{ 600, 372, 240, 46 })) {
            save.equipped = 16;
            saveGame();
            playSfx(SFX_BUY);
            showToast("CUSTOM CUBE EQUIPPED");
        } else if (btnClick(Btn{ 660, 466, 260, 46 })) {
            memset(save.drawCells, 0, sizeof(save.drawCells));
            saveGame();
            playSfx(SFX_CLICK);
            showToast("DRAWING CLEARED");
        } else if (btnClick(Btn{ 380, 466, 200, 46 })) {
            saveGame();
            state = ST_SHOP;
            playSfx(SFX_CLICK);
        } else if (escEdge) {
            saveGame();
            state = ST_SHOP;
            playSfx(SFX_CLICK);
        }
    }

    jumpHeld = (state == ST_PLAY && !paused) ? jk : false;
    jumpHeldPrev = jk;
    clickEdge = false;
    memcpy(keysPrev, keys, sizeof(keys));
}

static int activeHue() {
    if (state == ST_PLAY || state == ST_DEAD || state == ST_WIN) return levelHue(curLevel);
    return (int)fmod(215.0 + totalTime * 3.0, 360.0);
}

static std::map<DWORD, HBRUSH> brushCache;
static std::map<unsigned long long, HPEN> penCache;
static std::map<int, HFONT> fontCache;

static COLORREF quant(COLORREF c) {
    return RGB(GetRValue(c) & 0xF8, GetGValue(c) & 0xF8, GetBValue(c) & 0xF8);
}

static void purgeGdi() {
    if (brushCache.size() > 300) {
        for (auto& kv : brushCache) DeleteObject(kv.second);
        brushCache.clear();
    }
    if (penCache.size() > 300) {
        for (auto& kv : penCache) DeleteObject(kv.second);
        penCache.clear();
    }
}

static HBRUSH getBrush(COLORREF c) {
    c = quant(c);
    auto it = brushCache.find((DWORD)c);
    if (it != brushCache.end()) return it->second;
    HBRUSH b = CreateSolidBrush(c);
    brushCache[(DWORD)c] = b;
    return b;
}

static HPEN getPen(COLORREF c, int width) {
    c = quant(c);
    unsigned long long key = ((unsigned long long)(unsigned short)width << 32) | (DWORD)c;
    auto it = penCache.find(key);
    if (it != penCache.end()) return it->second;
    HPEN p = CreatePen(PS_SOLID, width, c);
    penCache[key] = p;
    return p;
}

static HFONT getFont(int size) {
    auto it = fontCache.find(size);
    if (it != fontCache.end()) return it->second;
    HFONT font = CreateFontA(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "BM space");
    fontCache[size] = font;
    return font;
}

static void fillRect(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    RECT r = { x, y, x + w, y + h };
    FillRect(hdc, &r, getBrush(c));
}

static void strokeRect(HDC hdc, double x, double y, double w, double h, int t, COLORREF c) {
    int ix = (int)x, iy = (int)y, iw = (int)w, ih = (int)h;
    fillRect(hdc, ix, iy, iw, t, c);
    fillRect(hdc, ix, iy + ih - t, iw, t, c);
    fillRect(hdc, ix, iy, t, ih, c);
    fillRect(hdc, ix + iw - t, iy, t, ih, c);
}

static void fillCircle(HDC hdc, double cx, double cy, double r, COLORREF c) {
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(c));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(c, 1));
    Ellipse(hdc, (int)(cx - r), (int)(cy - r), (int)(cx + r), (int)(cy + r));
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

static void strokeCircle(HDC hdc, double cx, double cy, double r, int t, COLORREF c) {
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(c, t));
    Ellipse(hdc, (int)(cx - r), (int)(cy - r), (int)(cx + r), (int)(cy + r));
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

static void fillEllipse(HDC hdc, double cx, double cy, double rx, double ry, COLORREF c) {
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(c));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(c, 1));
    Ellipse(hdc, (int)(cx - rx), (int)(cy - ry), (int)(cx + rx), (int)(cy + ry));
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

static void strokeEllipse(HDC hdc, double cx, double cy, double rx, double ry, int t, COLORREF c) {
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(c, t));
    Ellipse(hdc, (int)(cx - rx), (int)(cy - ry), (int)(cx + rx), (int)(cy + ry));
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

static void fillPoly(HDC hdc, POINT* pts, int n, COLORREF fill, COLORREF stroke, int t) {
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(fill));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(stroke, t));
    Polygon(hdc, pts, n);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

static void drawTriShape(HDC hdc, double x1, double y1, double x2, double y2, double x3, double y3, COLORREF fill, COLORREF stroke) {
    POINT pts[3] = { {(int)x1, (int)y1}, {(int)x2, (int)y2}, {(int)x3, (int)y3} };
    fillPoly(hdc, pts, 3, fill, stroke, 2);
}

static void setTextColor(HDC hdc, COLORREF c) {
    SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
}

static void drawText(HDC hdc, const char* text, int x, int y, int size, COLORREF c) {
    HFONT oldFont = (HFONT)SelectObject(hdc, getFont(size));
    setTextColor(hdc, c);
    TextOutA(hdc, x, y, text, (int)strlen(text));
    SelectObject(hdc, oldFont);
}

static int textWidth(HDC hdc, const char* text, int size) {
    HFONT oldFont = (HFONT)SelectObject(hdc, getFont(size));
    SIZE sz;
    GetTextExtentPoint32A(hdc, text, (int)strlen(text), &sz);
    SelectObject(hdc, oldFont);
    return sz.cx;
}

static void drawTextC(HDC hdc, const char* text, int cx, int y, int size, COLORREF c) {
    int w = textWidth(hdc, text, size);
    drawText(hdc, text, cx - w / 2, y, size, c);
}

static void drawShard(HDC hdc, double cx, double cy, double r) {
    POINT pts[4] = { {(int)cx, (int)(cy - r)}, {(int)(cx + r * 0.7), (int)cy}, {(int)cx, (int)(cy + r)}, {(int)(cx - r * 0.7), (int)cy} };
    fillPoly(hdc, pts, 4, RGB(70, 170, 255), RGB(190, 235, 255), 2);
    POINT hi[3] = { {(int)cx, (int)(cy - r + 3)}, {(int)(cx + r * 0.45), (int)cy}, {(int)cx, (int)cy} };
    fillPoly(hdc, hi, 3, RGB(150, 215, 255), RGB(150, 215, 255), 1);
}

static void drawShardCount(HDC hdc, int x, int y) {
    drawShard(hdc, x + 12, y + 13, 13);
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", save.shards);
    drawText(hdc, buf, x + 32, y + 1, 24, RGB(190, 235, 255));
}

static void drawFace(HDC hdc, double cx, double cy, double r, int tier) {
    COLORREF cols[6] = { RGB(80, 230, 110), RGB(70, 170, 255), RGB(255, 160, 40), RGB(255, 80, 70), RGB(240, 80, 220), RGB(90, 40, 60) };
    COLORREF c = cols[imin(tier, 5)];
    fillCircle(hdc, cx, cy, r, c);
    strokeCircle(hdc, cx, cy, r, 3, lerpColor(c, RGB(0, 0, 0), 0.45));
    double er = r * 0.18;
    fillCircle(hdc, cx - r * 0.35, cy - r * 0.15, er, RGB(20, 20, 30));
    fillCircle(hdc, cx + r * 0.35, cy - r * 0.15, er, RGB(20, 20, 30));
    if (tier <= 1) {
        strokeCircle(hdc, cx, cy + r * 0.3, r * 0.4, 3, RGB(20, 20, 30));
    } else if (tier <= 3) {
        POINT pts[3] = { {(int)(cx - r * 0.4), (int)(cy + r * 0.5)}, {(int)(cx + r * 0.4), (int)(cy + r * 0.5)}, {(int)cx, (int)(cy + r * 0.15)} };
        fillPoly(hdc, pts, 3, RGB(20, 20, 30), RGB(20, 20, 30), 1);
    } else {
        fillEllipse(hdc, cx, cy + r * 0.35, r * 0.4, r * 0.25, RGB(20, 20, 30));
        if (tier == 5) {
            POINT h1[3] = { {(int)(cx - r * 0.7), (int)(cy - r * 0.5)}, {(int)(cx - r * 0.2), (int)(cy - r * 1.1)}, {(int)(cx - r * 0.05), (int)(cy - r * 0.55)} };
            fillPoly(hdc, h1, 3, RGB(40, 40, 55), RGB(40, 40, 55), 1);
            POINT h2[3] = { {(int)(cx + r * 0.7), (int)(cy - r * 0.5)}, {(int)(cx + r * 0.2), (int)(cy - r * 1.1)}, {(int)(cx + r * 0.05), (int)(cy - r * 0.55)} };
            fillPoly(hdc, h2, 3, RGB(40, 40, 55), RGB(40, 40, 55), 1);
        }
    }
}

static void drawSkinCube(HDC hdc, double cx, double cy, double rot, double h, int skinIdx,
                         double sxK = 1.0, double syK = 1.0) {
    if (skinIdx == 16) {
        double a = rot * PI / 180.0;
        double ca = cos(a), sa = sin(a);
        auto rp = [&](double px, double py) {
            double qx = px * sxK, qy = py * syK;
            return POINT{ (int)(cx + qx * ca - qy * sa), (int)(cy + qx * sa + qy * ca) };
        };
        POINT p[4] = { rp(-h, -h), rp(h, -h), rp(h, h), rp(-h, h) };
        fillPoly(hdc, p, 4, customColor(0), RGB(255, 255, 255), 3);
        if (g_photo) {
            HRGN rgn = CreatePolygonRgn(p, 4, WINDING);
            SaveDC(hdc);
            SelectClipRgn(hdc, rgn);
            double side = h * 2.0;
            double sc = side / (g_photoW < g_photoH ? g_photoW : g_photoH);
            int dw = (int)(g_photoW * sc), dh = (int)(g_photoH * sc);
            int dx = (int)(cx - dw * 0.5), dy = (int)(cy - dh * 0.5);
            HDC mem = CreateCompatibleDC(hdc);
            HBITMAP old = (HBITMAP)SelectObject(mem, g_photo);
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, NULL);
            StretchBlt(hdc, dx, dy, dw, dh, mem, 0, 0, g_photoW, g_photoH, SRCCOPY);
            SelectObject(mem, old);
            DeleteDC(mem);
            RestoreDC(hdc, -1);
            DeleteObject(rgn);
        } else {
            POINT i2[4] = { rp(-h * 0.55, -h * 0.55), rp(h * 0.55, -h * 0.55),
                            rp(h * 0.55, h * 0.55), rp(-h * 0.55, h * 0.55) };
            fillPoly(hdc, i2, 4, customColor(1), customColor(1), 1);
            POINT d[4] = { rp(-h * 0.5, -h * 0.18), rp(-h * 0.18, -h * 0.18),
                           rp(-h * 0.18, h * 0.18), rp(-h * 0.5, h * 0.18) };
            fillPoly(hdc, d, 4, customColor(2), customColor(2), 1);
        }
        POINT g[3] = { rp(-h, -h * 0.55), rp(h, -h), rp(-h, h) };
        for (int dr = 0; dr < 12; dr++) {
            for (int dc2 = 0; dc2 < 12; dc2++) {
                unsigned char v = save.drawCells[dr * 12 + dc2];
                if (!v) continue;
                COLORREF dcol = (v == 1) ? customColor(0) : (v == 2) ? customColor(1) : (v == 3) ? customColor(2) :
                                (v == 4) ? RGB(255, 255, 255) : RGB(25, 25, 35);
                double cellS = h * 2.0 / 12.0;
                double x0 = -h + dc2 * cellS, y0 = -h + dr * cellS;
                POINT q[4] = { rp(x0, y0), rp(x0 + cellS, y0), rp(x0 + cellS, y0 + cellS), rp(x0, y0 + cellS) };
                fillPoly(hdc, q, 4, dcol, dcol, 1);
            }
        }
        COLORREF gc = lerpColor(customColor(0), RGB(255, 255, 255), 0.30);
        drawTriShape(hdc, g[0].x, g[0].y, g[1].x, g[1].y, g[2].x, g[2].y, gc, gc);
        return;
    }
    Skin& sk = skins[skinIdx];
    double a = rot * PI / 180.0;
    double ca = cos(a), sa = sin(a);
    auto rp = [&](double px, double py) {
        double qx = px * sxK, qy = py * syK;
        return POINT{ (int)(cx + qx * ca - qy * sa), (int)(cy + qx * sa + qy * ca) };
    };
    auto sq = [&](double s, POINT* p) {
        p[0] = rp(-s, -s); p[1] = rp(s, -s); p[2] = rp(s, s); p[3] = rp(-s, s);
    };
    auto gloss = [&](double s, COLORREF base) {
        POINT g[3] = { rp(-s, -s * 0.55), rp(s, -s), rp(-s, s) };
        COLORREF gc = lerpColor(base, RGB(255, 255, 255), 0.30);
        drawTriShape(hdc, g[0].x, g[0].y, g[1].x, g[1].y, g[2].x, g[2].y, gc, gc);
    };
    switch (sk.style) {
    case SK_SQUARE: {
        POINT p[4]; sq(h, p);
        fillPoly(hdc, p, 4, sk.main, RGB(255, 255, 255), 3);
        gloss(h, sk.main);
        POINT i2[4]; sq(h * 0.5, i2);
        fillPoly(hdc, i2, 4, sk.dark, sk.dark, 1);
        POINT d[4] = { rp(-h * 0.55, -h * 0.2), rp(-h * 0.2, -h * 0.2), rp(-h * 0.2, h * 0.2), rp(-h * 0.55, h * 0.2) };
        fillPoly(hdc, d, 4, sk.accent, sk.accent, 1);
        fillCircle(hdc, cx, cy, h * 0.16, lerpColor(sk.accent, RGB(255, 255, 255), 0.35));
        break;
    }
    case SK_ROUNDED: {
        POINT p[8] = { rp(-h * 0.55, -h), rp(h * 0.55, -h), rp(h, -h * 0.55), rp(h, h * 0.55),
                       rp(h * 0.55, h), rp(-h * 0.55, h), rp(-h, h * 0.55), rp(-h, -h * 0.55) };
        fillPoly(hdc, p, 8, sk.main, sk.accent, 3);
        gloss(h, sk.main);
        POINT i2[8] = { rp(-h * 0.3, -h * 0.55), rp(h * 0.3, -h * 0.55), rp(h * 0.55, -h * 0.3), rp(h * 0.55, h * 0.3),
                        rp(h * 0.3, h * 0.55), rp(-h * 0.3, h * 0.55), rp(-h * 0.55, h * 0.3), rp(-h * 0.55, -h * 0.3) };
        fillPoly(hdc, i2, 8, sk.dark, sk.dark, 1);
        break;
    }
    case SK_BOLT: {
        POINT p[4]; sq(h, p);
        fillPoly(hdc, p, 4, sk.dark, RGB(255, 255, 255), 3);
        gloss(h, sk.dark);
        POINT bolt[6] = { rp(-h * 0.15, -h * 0.7), rp(h * 0.5, -h * 0.7), rp(h * 0.05, -h * 0.05),
                          rp(h * 0.45, -h * 0.05), rp(-h * 0.4, h * 0.75), rp(-h * 0.05, h * 0.05) };
        fillPoly(hdc, bolt, 6, sk.main, sk.accent, 2);
        break;
    }
    case SK_STAR: {
        POINT p[4]; sq(h, p);
        fillPoly(hdc, p, 4, sk.main, RGB(255, 255, 255), 3);
        gloss(h, sk.main);
        POINT star[10];
        for (int i = 0; i < 10; i++) {
            double ang = -PI / 2 + i * PI / 5;
            double rr = (i % 2 == 0) ? h * 0.85 : h * 0.38;
            star[i] = rp(cos(ang) * rr, sin(ang) * rr);
        }
        fillPoly(hdc, star, 10, sk.accent, sk.dark, 2);
        break;
    }
    case SK_SKULL: {
        POINT p[8] = { rp(-h * 0.55, -h), rp(h * 0.55, -h), rp(h, -h * 0.55), rp(h, h * 0.55),
                       rp(h * 0.55, h), rp(-h * 0.55, h), rp(-h, h * 0.55), rp(-h, -h * 0.55) };
        fillPoly(hdc, p, 8, sk.main, RGB(255, 255, 255), 3);
        gloss(h, sk.main);
        double ex1 = cx - h * 0.4 * ca - (-h * 0.15) * sa, ey1 = cy - h * 0.4 * sa + (-h * 0.15) * ca;
        double ex2 = cx + h * 0.4 * ca - (-h * 0.15) * sa, ey2 = cy + h * 0.4 * sa + (-h * 0.15) * ca;
        fillCircle(hdc, ex1, ey1, h * 0.22, sk.dark);
        fillCircle(hdc, ex2, ey2, h * 0.22, sk.dark);
        for (int i = -1; i <= 1; i++) {
            POINT t[4] = { rp(i * h * 0.3 - h * 0.1, h * 0.35), rp(i * h * 0.3 + h * 0.1, h * 0.35),
                           rp(i * h * 0.3 + h * 0.1, h * 0.7), rp(i * h * 0.3 - h * 0.1, h * 0.7) };
            fillPoly(hdc, t, 4, sk.dark, sk.dark, 1);
        }
        break;
    }
    case SK_SWIRL: {
        POINT p[4]; sq(h, p);
        fillPoly(hdc, p, 4, sk.dark, RGB(255, 255, 255), 3);
        gloss(h, sk.dark);
        for (int arm = 0; arm < 3; arm++) {
            POINT pts[9];
            for (int i = 0; i < 9; i++) {
                double t = (double)i / 8.0;
                double rr = t * h * 0.9;
                double ang = arm * 2.094 + t * 4.0;
                pts[i] = rp(cos(ang) * rr, sin(ang) * rr);
            }
            for (int i = 0; i < 8; i++) {
                drawTriShape(hdc, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, cx, cy,
                             (arm == 0) ? sk.main : sk.accent, (arm == 0) ? sk.main : sk.accent);
            }
        }
        fillCircle(hdc, cx, cy, h * 0.22, sk.accent);
        break;
    }
    case SK_PIXEL: {
        int pat[3][3] = { {0,1,0}, {1,2,1}, {0,1,0} };
        double cs = h * 2.0 / 3.0;
        for (int r = 0; r < 3; r++) {
            for (int c2 = 0; c2 < 3; c2++) {
                double px = (c2 - 1) * cs, py = (r - 1) * cs;
                COLORREF col = (pat[r][c2] == 0) ? sk.dark : (pat[r][c2] == 1 ? sk.main : sk.accent);
                POINT q[4] = { rp(px, py), rp(px + cs, py), rp(px + cs, py + cs), rp(px, py + cs) };
                fillPoly(hdc, q, 4, col, col, 1);
            }
        }
        strokeRect(hdc, cx - h, cy - h, h * 2, h * 2, 3, RGB(255, 255, 255));
        break;
    }
    case SK_ARROW: {
        POINT p[4]; sq(h, p);
        fillPoly(hdc, p, 4, sk.main, RGB(255, 255, 255), 3);
        gloss(h, sk.main);
        POINT ar[5] = { rp(-h * 0.6, -h * 0.55), rp(h * 0.55, 0), rp(-h * 0.6, h * 0.55),
                        rp(-h * 0.15, 0), rp(-h * 0.6, -h * 0.55) };
        fillPoly(hdc, ar, 5, sk.accent, sk.dark, 2);
        break;
    }
    case SK_EYE: {
        POINT p[8] = { rp(-h * 0.55, -h), rp(h * 0.55, -h), rp(h, -h * 0.55), rp(h, h * 0.55),
                       rp(h * 0.55, h), rp(-h * 0.55, h), rp(-h, h * 0.55), rp(-h, -h * 0.55) };
        fillPoly(hdc, p, 8, sk.dark, sk.accent, 3);
        fillCircle(hdc, cx, cy, h * 0.62, RGB(255, 255, 255));
        fillCircle(hdc, cx, cy, h * 0.34, sk.main);
        fillCircle(hdc, cx, cy, h * 0.16, RGB(20, 20, 30));
        fillCircle(hdc, cx + h * 0.12, cy - h * 0.12, h * 0.08, RGB(255, 255, 255));
        break;
    }
    case SK_RING: {
        POINT p[4]; sq(h, p);
        fillPoly(hdc, p, 4, sk.main, RGB(255, 255, 255), 3);
        gloss(h, sk.main);
        strokeCircle(hdc, cx, cy, h * 0.62, 5, sk.accent);
        fillCircle(hdc, cx, cy, h * 0.2, sk.accent);
        break;
    }
    case SK_CROWN: {
        POINT p[4]; sq(h, p);
        fillPoly(hdc, p, 4, sk.dark, RGB(255, 255, 255), 3);
        gloss(h, sk.dark);
        POINT cr[7] = { rp(-h * 0.7, h * 0.5), rp(-h * 0.7, -h * 0.35), rp(-h * 0.3, 0),
                        rp(0, -h * 0.6), rp(h * 0.3, 0), rp(h * 0.7, -h * 0.35), rp(h * 0.7, h * 0.5) };
        fillPoly(hdc, cr, 7, sk.main, sk.accent, 2);
        fillCircle(hdc, cx, cy + h * 0.1, h * 0.14, sk.accent);
        break;
    }
    case SK_DIAMOND: {
        POINT d[4] = { rp(0, -h), rp(h * 0.85, 0), rp(0, h), rp(-h * 0.85, 0) };
        fillPoly(hdc, d, 4, sk.main, RGB(255, 255, 255), 3);
        POINT f[4] = { rp(0, -h), rp(h * 0.4, -h * 0.3), rp(0, 0), rp(-h * 0.4, -h * 0.3) };
        fillPoly(hdc, f, 4, sk.accent, sk.accent, 1);
        POINT g[4] = { rp(-h * 0.85, 0), rp(0, -h * 0.02), rp(0, h), rp(-h * 0.5, 0) };
        fillPoly(hdc, g, 4, sk.dark, sk.dark, 1);
        break;
    }
    case SK_TRIANGLE: {
        POINT t[3] = { rp(0, -h), rp(h * 0.95, h * 0.8), rp(-h * 0.95, h * 0.8) };
        fillPoly(hdc, t, 3, sk.main, RGB(255, 255, 255), 3);
        gloss(h * 0.55, sk.main);
        POINT t2[3] = { rp(0, -h * 0.3), rp(h * 0.38, h * 0.42), rp(-h * 0.38, h * 0.42) };
        fillPoly(hdc, t2, 3, sk.dark, sk.dark, 1);
        fillCircle(hdc, cx, cy + h * 0.2, h * 0.13, sk.accent);
        break;
    }
    case SK_HEX: {
        POINT hx[6], hx2[6];
        for (int i = 0; i < 6; i++) {
            double ang = -PI / 2 + i * PI / 3;
            hx[i] = rp(cos(ang) * h, sin(ang) * h);
            hx2[i] = rp(cos(ang) * h * 0.52, sin(ang) * h * 0.52);
        }
        fillPoly(hdc, hx, 6, sk.main, RGB(255, 255, 255), 3);
        fillPoly(hdc, hx2, 6, sk.dark, sk.dark, 1);
        fillCircle(hdc, cx, cy, h * 0.17, sk.accent);
        break;
    }
    case SK_HEART: {
        POINT hrt[16];
        for (int i = 0; i < 16; i++) {
            double t = i * (2.0 * PI / 16.0);
            double s = sin(t);
            double x = 16.0 * s * s * s;
            double y = -(13.0 * cos(t) - 5.0 * cos(2 * t) - 2.0 * cos(3 * t) - cos(4 * t));
            hrt[i] = rp(x / 17.0 * h, y / 17.0 * h);
        }
        fillPoly(hdc, hrt, 16, sk.main, RGB(255, 255, 255), 3);
        POINT hs[3] = { rp(-h * 0.5, -h * 0.35), rp(-h * 0.12, -h * 0.62), rp(-h * 0.22, -h * 0.18) };
        fillPoly(hdc, hs, 3, sk.accent, sk.accent, 1);
        break;
    }
    case SK_CIRCLE: {
        fillCircle(hdc, cx, cy, h, sk.main);
        strokeCircle(hdc, cx, cy, h, 4, RGB(255, 255, 255));
        strokeCircle(hdc, cx, cy, h * 0.6, 6, sk.dark);
        fillCircle(hdc, cx, cy, h * 0.26, sk.accent);
        COLORREF gleam = lerpColor(sk.main, RGB(255, 255, 255), 0.5);
        POINT g2[3] = { rp(-h * 0.72, -h * 0.4), rp(-h * 0.2, -h * 0.8), rp(-h * 0.38, -h * 0.25) };
        fillPoly(hdc, g2, 3, gleam, gleam, 1);
        break;
    }
    }
}

static void drawBackground(HDC hdc) {
    int h = activeHue();
    double pulse = beatPulse();
    COLORREF top = hslToColor(h, 86, (int)(26 + pulse * 6));
    COLORREF bot = hslToColor(h + 40, 92, (int)(54 + pulse * 8));
    for (int y = -40; y <= WINDOW_H + 40; y += 3) {
        double t = (double)(y + 40) / (double)(WINDOW_H + 80);
        fillRect(hdc, -40, y, WINDOW_W + 80, 3, lerpColor(top, bot, t));
    }
    double skyOff = fmod(camX * 0.06, WINDOW_W + 400.0);
    for (int i = 0; i < 46; i++) {
        double sx = fmod(i * 97.0 - skyOff, WINDOW_W + 200.0);
        if (sx < -20) sx += WINDOW_W + 200.0;
        double sy = 20.0 + fmod(i * 61.0, FLOOR_Y * 0.62 - 40.0);
        double tw = 0.55 + 0.45 * sin(totalTime * 2.6 + i * 1.7);
        int sz = (i % 7 == 0) ? 3 : 2;
        COLORREF sc = lerpColor(top, RGB(255, 255, 255), 0.35 + 0.55 * tw);
        fillRect(hdc, (int)(sx - 20), (int)sy, sz, sz, sc);
    }
    double cometT = fmod(totalTime, 7.5);
    if (cometT < 0.85) {
        double ct = cometT / 0.85;
        double x0 = 1060 - ct * 1520, y0 = 26 + ct * 300;
        for (int k = 0; k < 9; k++) {
            double br = (1.0 - k / 9.0) * (1.0 - ct);
            if (br > 0.03)
                fillRect(hdc, x0 + k * 20.0, y0 - k * 13.0, 16, 2, lerpColor(top, RGB(255, 255, 255), br));
        }
    }
    double sunX = 762, sunY = 128;
    for (int i = 0; i < 12; i++) {
        double ang = totalTime * 0.14 + i * PI / 6.0;
        double r0 = 52, r1 = 84 + sin(totalTime * 2.0 + i * 1.3) * 7;
        POINT ray[3] = { {(int)(sunX + cos(ang - 0.07) * r0), (int)(sunY + sin(ang - 0.07) * r0)},
                         {(int)(sunX + cos(ang) * r1), (int)(sunY + sin(ang) * r1)},
                         {(int)(sunX + cos(ang + 0.07) * r0), (int)(sunY + sin(ang + 0.07) * r0)} };
        fillPoly(hdc, ray, 3, lerpColor(top, RGB(255, 214, 90), 0.18 + pulse * 0.1),
                 lerpColor(top, RGB(255, 214, 90), 0.18 + pulse * 0.1), 1);
    }
    fillCircle(hdc, sunX, sunY, 44 + pulse * 5, RGB(255, 196, 60));
    fillCircle(hdc, sunX, sunY, 33 + pulse * 4, RGB(255, 226, 110));
    fillCircle(hdc, sunX, sunY, 21 + pulse * 3, RGB(255, 248, 190));
    double mtnBase = FLOOR_Y * 0.62 + 6;
    for (int layer = 0; layer < 2; layer++) {
        double par = layer == 0 ? 0.10 : 0.18;
        double step = 96.0;
        double off = fmod(camX * par, step);
        if (off < 0) off += step;
        double hAmp = layer == 0 ? 100.0 : 66.0;
        COLORREF mc = hslToColor(h + 30, layer == 0 ? 38 : 46, layer == 0 ? 13 : 10);
        COLORREF rim = hslToColor(h + 140, 90, layer == 0 ? 42 : 36);
        for (int k = -1; k < WINDOW_W / (int)step + 2; k++) {
            int seg = (((k + layer * 5) % 4) + 4) % 4;
            double peakH = hAmp * ((seg == 0) ? 1.0 : (seg == 1 ? 0.68 : (seg == 2 ? 0.92 : 0.55)));
            POINT tri[3] = { {(int)(k * step - off), (int)mtnBase},
                             {(int)(k * step - off + step * 0.5), (int)(mtnBase - peakH)},
                             {(int)(k * step - off + step), (int)mtnBase} };
            fillPoly(hdc, tri, 3, mc, mc, 1);
            POINT cap[3] = { {(int)(k * step - off + step * 0.5), (int)(mtnBase - peakH)},
                             {(int)(k * step - off + step * 0.5 + 15), (int)(mtnBase - peakH + 24)},
                             {(int)(k * step - off + step * 0.5 - 15), (int)(mtnBase - peakH + 24)} };
            fillPoly(hdc, cap, 3, rim, rim, 1);
        }
        fillRect(hdc, -40, (int)mtnBase - 2, WINDOW_W + 80, 5, mc);
    }
    double skyY = FLOOR_Y * 0.62;
    double bOff = fmod(camX * 0.14, 560.0);
    if (bOff < 0) bOff += 560.0;
    for (int i = -1; i < 3; i++) {
        double bx = i * 300.0 - bOff;
        double bh1 = 70.0 + (i & 1) * 55.0;
        double bh2 = 110.0 + ((i + 2) & 1) * 45.0;
        fillRect(hdc, (int)bx, (int)(skyY - bh1), 140, (int)bh1 + 4, hslToColor(h + 30, 45, (int)(19 + pulse * 3)));
        fillRect(hdc, (int)(bx + 150), (int)(skyY - bh2), 120, (int)bh2 + 4, hslToColor(h + 30, 45, (int)(16 + pulse * 3)));
        fillRect(hdc, (int)bx, (int)(skyY - bh1), 140, 3, hslToColor(h + 160, 95, (int)(64 + pulse * 16)));
        fillRect(hdc, (int)(bx + 150), (int)(skyY - bh2), 120, 3, hslToColor(h + 100, 95, (int)(60 + pulse * 16)));
        for (int wY = 0; wY < (int)bh1 - 18; wY += 22) {
            for (int wX = 0; wX < 3; wX++) {
                if (((i * 7 + wY / 22 + wX) & 3) == 0)
                    fillRect(hdc, (int)(bx + 16 + wX * 40), (int)(skyY - bh1 + 12 + wY), 9, 11,
                             ((wY / 22 + wX) & 1) ? hslToColor(h + 70, 90, (int)(62 + pulse * 14))
                                                  : hslToColor(h + 200, 88, (int)(58 + pulse * 14)));
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        double beamX = fmod(i * 530.0 + totalTime * 46.0, WINDOW_W + 700.0) - 200.0;
        POINT beam[4] = { {(int)beamX, -40}, {(int)(beamX + 130), -40},
                          {(int)(beamX + 330), WINDOW_H + 40}, {(int)(beamX + 170), WINDOW_H + 40} };
        fillPoly(hdc, beam, 4, lerpColor(top, RGB(255, 255, 255), 0.055 + pulse * 0.05),
                 lerpColor(top, RGB(255, 255, 255), 0.055 + pulse * 0.05), 1);
    }
    for (size_t i = 0; i < bgShapes.size(); i++) {
        BgShape& s = bgShapes[i];
        if (s.x > WINDOW_W + 60 || s.x + s.size < -60) continue;
        int sh = (h + 180 + (int)(i * 61)) % 360;
        COLORREF line = hslToColor(sh, 92, (int)(56 + pulse * 8 + s.bright));
        COLORREF fill = hslToColor(sh, 74, (int)(24 + pulse * 5 + s.bright));
        if ((i % 3) == 1) {
            if (s.filled) fillCircle(hdc, s.x + s.size * 0.5, s.y + s.size * 0.5, s.size * 0.5 - 4, fill);
            strokeCircle(hdc, s.x + s.size * 0.5, s.y + s.size * 0.5, s.size * 0.5, 3, line);
            strokeCircle(hdc, s.x + s.size * 0.5, s.y + s.size * 0.5, s.size * 0.34, 2, line);
        } else if ((i % 3) == 2) {
            POINT dia[4] = { {(int)(s.x + s.size * 0.5), (int)s.y},
                             {(int)(s.x + s.size), (int)(s.y + s.size * 0.5)},
                             {(int)(s.x + s.size * 0.5), (int)(s.y + s.size)},
                             {(int)s.x, (int)(s.y + s.size * 0.5)} };
            fillPoly(hdc, dia, 4, fill, line, 3);
        } else {
            if (s.filled) fillRect(hdc, (int)(s.x + 8), (int)(s.y + 8), (int)(s.size - 16), (int)(s.size - 16), fill);
            strokeRect(hdc, s.x, s.y, s.size, s.size, 3, line);
        }
    }
    for (int i = 0; i < 22; i++) {
        double period = 6.5 + (i % 5);
        double t = fmod(totalTime * (0.9 + (i % 3) * 0.2) + i * 1.37, period);
        double ex = fmod(i * 137.0 - camX * (0.25 + (i % 4) * 0.07), WINDOW_W + 120.0);
        if (ex < -30) ex += WINDOW_W + 120.0;
        double ey = FLOOR_Y * 0.95 - (t / period) * FLOOR_Y * 0.78;
        double br = sin(PI * t / period);
        if (br < 0) br = 0;
        COLORREF ec = lerpColor(lerpColor(top, RGB(255, 255, 255), 0.25),
                                hslToColor(((h + (int)i * 47) % 360 + 360) % 360, 100, 62),
                                0.35 + 0.65 * br);
        fillRect(hdc, (int)ex, (int)ey, 3, 3 + (i % 2), ec);
    }
    double gy = FLOOR_Y * 0.62;
    double hzOff = fmod(camX * 0.35, 64.0);
    if (hzOff < 0) hzOff += 64.0;
    for (double x = -hzOff - 64; x < WINDOW_W + 64; x += 64) {
        fillRect(hdc, (int)x, (int)gy, 40, 4, hslToColor(h + 180, 95, (int)(58 + pulse * 18)));
    }
}

static void drawGround(HDC hdc) {
    int h = activeHue();
    double pulse = beatPulse();
    COLORREF gTop = hslToColor(h, 50, 14);
    COLORREF gBot = hslToColor(h, 55, 6);
    for (int y = FLOOR_Y; y < WINDOW_H; y += 3) {
        double t = (double)(y - FLOOR_Y) / (double)(WINDOW_H - FLOOR_Y);
        fillRect(hdc, -40, y, WINDOW_W + 80, 3, lerpColor(gTop, gBot, t));
    }
    double off = fmod(camX, 40.0);
    if (off < 0) off += 40.0;
    for (double x = -off - 40; x < WINDOW_W + 40; x += 40) {
        strokeRect(hdc, x, FLOOR_Y + 6, 40, WINDOW_H - FLOOR_Y, 2, hslToColor(h + 180, 45, 33));
    }
    double off2 = fmod(camX, 320.0);
    if (off2 < 0) off2 += 320.0;
    for (double x = -off2 - 320; x < WINDOW_W + 320; x += 320) {
        fillRect(hdc, (int)x, FLOOR_Y + 6, 120, 3, hslToColor(h + 55, 95, 64));
    }
    fillRect(hdc, -40, FLOOR_Y - 6, WINDOW_W + 80, 6, hslToColor(h + 180, 95, (int)(68 + pulse * 18)));
    fillRect(hdc, -40, FLOOR_Y, WINDOW_W + 80, 3, hslToColor(h, 80, 30));
}

static void drawObjects(HDC hdc) {
    int h = activeHue();
    double pulse = beatPulse();
    COLORREF blockMain = hslToColor(h + 180, 64, 42);
    COLORREF blockTop = hslToColor(h + 180, 70, 57);
    COLORREF blockEdge = brighten(hslToColor(h + 180, 92, 78), (int)(pulse * 34));
    COLORREF blockShade = hslToColor(h + 180, 62, 26);
    COLORREF blockAccent = hslToColor(h + 55, 95, (int)(62 + pulse * 22));
    COLORREF spikeMain = brighten(hslToColor(h, 88, 58), (int)(pulse * 26));
    COLORREF spikeDark = hslToColor(h + 180, 65, 32);
    for (size_t i = 0; i < objs.size(); i++) {
        Obj& o = objs[i];
        double sx = o.x - camX;
        if (sx > WINDOW_W + 90 || sx + o.w < -90) continue;
        switch (o.t) {
        case OBJ_BLOCK:
            fillRect(hdc, (int)sx, (int)o.y, (int)o.w, (int)o.h, blockMain);
            fillRect(hdc, (int)sx, (int)o.y, (int)o.w, 7, blockTop);
            fillRect(hdc, (int)sx, (int)o.y, (int)o.w, 3, blockAccent);
            fillRect(hdc, (int)(sx + o.w - 5), (int)o.y, 5, (int)o.h, brighten(blockMain, 16));
            fillRect(hdc, (int)(sx + o.w - 5), (int)o.y, 5, 3, blockAccent);
            fillRect(hdc, (int)sx, (int)(o.y + o.h - 8), (int)o.w, 8, blockShade);
            strokeRect(hdc, sx + 5, o.y + 5, o.w - 10, o.h - 10, 2, blockEdge);
            strokeRect(hdc, sx, o.y, o.w, o.h, 3, brighten(blockEdge, 30));
            break;
        case OBJ_SPIKE_UP:
            drawTriShape(hdc, sx + 1, o.y + 40, sx + 39, o.y + 40, sx + 20, o.y + 2, spikeMain, RGB(255, 255, 255));
            drawTriShape(hdc, sx + 12, o.y + 36, sx + 28, o.y + 36, sx + 20, o.y + 16, spikeDark, spikeDark);
            fillRect(hdc, (int)(sx + 16), (int)(o.y + 26), 8, 4, RGB(255, 255, 255));
            break;
        case OBJ_SPIKE_DOWN:
            drawTriShape(hdc, sx + 1, o.y, sx + 39, o.y, sx + 20, o.y + 38, spikeMain, RGB(255, 255, 255));
            drawTriShape(hdc, sx + 12, o.y + 4, sx + 28, o.y + 4, sx + 20, o.y + 22, spikeDark, spikeDark);
            break;
        case OBJ_ORB:
        case OBJ_ORB_BLUE: {
            double cx = sx + 20, cy = o.y + 20;
            double pw = 1.0 + 0.12 * sin(totalTime * 6.0);
            COLORREF main = (o.t == OBJ_ORB) ? hslToColor(48, 100, 58) : hslToColor(215, 90, 60);
            COLORREF lite = (o.t == OBJ_ORB) ? hslToColor(48, 100, 76) : hslToColor(215, 95, 75);
            strokeCircle(hdc, cx, cy, 17 * pw, 4, main);
            strokeCircle(hdc, cx, cy, 24 * pw, 2, lite);
            fillCircle(hdc, cx, cy, 7, RGB(255, 255, 255));
            for (int sp = 0; sp < 4; sp++) {
                double sa = totalTime * 2.4 + sp * PI / 2.0;
                fillCircle(hdc, cx + cos(sa) * 24.0 * pw, cy + sin(sa) * 24.0 * pw, 3, lite);
            }
            break;
        }
        case OBJ_PAD: {
            double cx = sx + 20, cy = o.y + o.h;
            fillEllipse(hdc, cx, cy, 20, 8, hslToColor(48, 100, 55));
            strokeEllipse(hdc, cx, cy, 20, 8, 2, RGB(255, 255, 255));
            fillEllipse(hdc, cx, cy - 4, 12, 5, hslToColor(48, 100, 72));
            break;
        }
        case OBJ_PORTAL: {
            double cx = sx + 20, cy = FLOOR_Y - 56;
            COLORREF col, col2;
            if (o.mode == MODE_SHIP) { col = RGB(0, 225, 255); col2 = RGB(170, 250, 255); }
            else if (o.mode == MODE_BALL) { col = RGB(255, 120, 40); col2 = RGB(255, 210, 160); }
            else if (o.mode == MODE_UFO) { col = RGB(200, 90, 255); col2 = RGB(240, 190, 255); }
            else { col = RGB(120, 255, 140); col2 = RGB(210, 255, 220); }
            fillEllipse(hdc, cx, cy, 17, 52, RGB(18, 18, 30));
            strokeEllipse(hdc, cx, cy, 17, 52, 4, col);
            strokeEllipse(hdc, cx, cy, 10, 40, 2, col2);
            fillEllipse(hdc, cx, cy, 4, 15, RGB(255, 255, 255));
            const char* letters[4] = { "C", "S", "B", "U" };
            drawTextC(hdc, letters[imin(o.mode, 3)], (int)cx - 4, (int)cy - 34, 16, col2);
            break;
        }
        case OBJ_SHARD: {
            if (trigUsed[i]) break;
            double by = o.y + 20 + sin(totalTime * 3.0 + (double)i) * 4.0;
            double pw = 1.0 + 0.15 * sin(totalTime * 5.0 + (double)i);
            strokeCircle(hdc, sx + 20, by, 15 * pw, 3, hslToColor(48, 100, 62));
            drawShard(hdc, sx + 20, by, 9 * pw);
            break;
        }
        case OBJ_FINISH: {
            for (int yy = 0; yy < FLOOR_Y; yy += 20) {
                for (int xx = 0; xx < 2; xx++) {
                    COLORREF c = (((yy / 20) + xx) % 2) ? RGB(250, 250, 250) : RGB(20, 20, 20);
                    fillRect(hdc, (int)sx + xx * 20, yy, 20, 20, c);
                }
            }
            strokeRect(hdc, sx - 4, -6, 48, FLOOR_Y + 6, 3, RGB(255, 255, 255));
            break;
        }
        }
    }
}

static void drawPlayer(HDC hdc) {
    if (state == ST_DEAD) return;
    double sx = pl.x - camX;
    double h = 16;
    switch (pl.mode) {
    case MODE_CUBE: {
        double glowR = 23.0 + 1.5 * sin(totalTime * 5.0);
        strokeCircle(hdc, sx, pl.y, glowR + 5, 6, lerpColor(skinMain(), RGB(255, 255, 255), 0.28));
        strokeCircle(hdc, sx, pl.y, glowR + 11, 3, lerpColor(skinMain(), RGB(255, 255, 255), 0.10));
        drawSkinCube(hdc, sx, pl.y, pl.rot, h, save.equipped, 1.0 - plSquash * 0.5, 1.0 + plSquash);
        break;
    }
    case MODE_SHIP: {
        double a = pl.rot * PI / 180.0;
        double ca = cos(a), sa = sin(a);
        auto rp = [&](double px, double py) { return POINT{ (int)(sx + px * ca - py * sa), (int)(pl.y + px * sa + py * ca) }; };
        if (jumpHeld) {
            double fl = 34 + 10 * sin(totalTime * 40.0);
            POINT flame[3] = { rp(-20, -7), rp(-20, 7), rp(-fl - 20, 0) };
            fillPoly(hdc, flame, 3, RGB(255, 170, 40), RGB(255, 220, 120), 2);
        }
        POINT body[7] = { rp(24, 0), rp(10, 11), rp(-16, 13), rp(-24, 6), rp(-24, -6), rp(-16, -13), rp(10, -11) };
        fillPoly(hdc, body, 7, skinMain(), RGB(255, 255, 255), 3);
        POINT win[4] = { rp(-6, -8), rp(6, -4), rp(6, 4), rp(-6, 8) };
        fillPoly(hdc, win, 4, skinDark(), skinAccent(), 2);
        break;
    }
    case MODE_BALL: {
        fillCircle(hdc, sx, pl.y, 17, skinMain());
        strokeCircle(hdc, sx, pl.y, 17, 3, RGB(255, 255, 255));
        double a = pl.rot * PI / 180.0;
        double ca = cos(a), sa = sin(a);
        auto rp = [&](double px, double py) { return POINT{ (int)(sx + px * ca - py * sa), (int)(pl.y + px * sa + py * ca) }; };
        POINT bar[4] = { rp(-3, -17), rp(3, -17), rp(3, 17), rp(-3, 17) };
        fillPoly(hdc, bar, 4, skinDark(), skinDark(), 1);
        fillCircle(hdc, sx, pl.y, 6, skinAccent());
        break;
    }
    case MODE_UFO: {
        double a = pl.rot * PI / 180.0;
        double ca = cos(a), sa = sin(a);
        auto rp = [&](double px, double py) { return POINT{ (int)(sx + px * ca - py * sa), (int)(pl.y + px * sa + py * ca) }; };
        if (jumpHeld) {
            POINT flame[3] = { rp(-8, 12), rp(8, 12), rp(0, 26 + 6 * sin(totalTime * 40.0)) };
            fillPoly(hdc, flame, 3, RGB(255, 170, 40), RGB(255, 220, 120), 2);
        }
        POINT dome[5] = { rp(-10, -2), rp(-6, -12), rp(0, -16), rp(6, -12), rp(10, -2) };
        fillPoly(hdc, dome, 5, RGB(220, 245, 255), RGB(255, 255, 255), 2);
        POINT saucer[7] = { rp(22, 2), rp(10, 10), rp(-10, 10), rp(-22, 2), rp(-10, -2), rp(10, -2), rp(22, 2) };
        fillPoly(hdc, saucer, 7, skinMain(), RGB(255, 255, 255), 3);
        fillCircle(hdc, sx, pl.y + 4, 5, skinAccent());
        break;
    }
    }
}

static void drawParticles(HDC hdc) {
    int h = activeHue();
    COLORREF fadeTo = hslToColor(h, 60, 34);
    for (size_t i = 0; i < parts.size(); i++) {
        Particle& p = parts[i];
        double t = clampD(p.life / p.maxLife, 0, 1);
        double sz = p.size * (0.4 + 0.6 * t);
        COLORREF c;
        if (p.kind == 3) {
            double rr = sz * 2.0;
            strokeCircle(hdc, p.x - camX, p.y, rr, 4, lerpColor(RGB(255, 120, 120), RGB(255, 255, 255), t));
            continue;
        }
        if (p.kind == 4) {
            double rr = p.size * (1.0 - t);
            strokeCircle(hdc, p.x - camX, p.y, rr, (int)(1 + 3 * t), lerpColor(p.col, RGB(255, 255, 255), 1.0 - t));
            continue;
        }
        c = (p.kind == 0) ? lerpColor(fadeTo, p.col, t) : lerpColor(RGB(50, 50, 60), p.col, t);
        fillRect(hdc, (int)(p.x - camX - sz * 0.5), (int)(p.y - sz * 0.5), (int)sz, (int)sz, c);
    }
}

static void drawSpeedLines(HDC hdc) {
    if (state != ST_PLAY || paused) return;
    static const double ys[5] = { 64, 140, 236, 330, 406 };
    for (int i = 0; i < 5; i++) {
        double lx = fmod(totalTime * gSpeed * 1.35 + i * 231.0, 1220.0) - 130.0;
        double w = 36 + (i % 3) * 20;
        fillRect(hdc, lx, ys[i], w, 2, RGB(205, 220, 255));
    }
}

static void drawHUD(HDC hdc) {
    char buf[64];
    int bw = 420, bx = WINDOW_W / 2 - bw / 2, by = 14, bh = 16;
    fillRect(hdc, bx - 3, by - 3, bw + 6, bh + 6, RGB(0, 0, 0));
    fillRect(hdc, bx, by, bw, bh, RGB(28, 28, 40));
    int fw = (int)(bw * pct / 100.0);
    if (fw > 0) {
        fillRect(hdc, bx, by, fw, bh, hslToColor(128, 85, 46));
        fillRect(hdc, bx, by, fw, bh / 2, hslToColor(128, 85, 62));
    }
    int bwBest = (int)(bw * save.best[curLevel] / 100.0);
    if (bwBest > 0) fillRect(hdc, bx + bwBest - 1, by - 3, 3, bh + 6, RGB(255, 255, 255));
    strokeRect(hdc, bx, by, bw, bh, 2, RGB(255, 255, 255));
    snprintf(buf, sizeof(buf), "%d%%", (int)(pct + 0.5));
    drawText(hdc, buf, bx + bw + 14, by - 4, 22, RGB(255, 255, 255));

    drawText(hdc, "BEST", 14, 14, 16, RGB(170, 175, 195));
    snprintf(buf, sizeof(buf), "%d%%", save.best[curLevel]);
    drawText(hdc, buf, 14, 34, 22, RGB(120, 255, 160));
    drawShardCount(hdc, WINDOW_W - 150, 12);

    if (attemptT < 2.0 && state == ST_PLAY) {
        double a = 1.0;
        if (attemptT < 0.25) a = attemptT / 0.25;
        else if (attemptT > 1.4) a = (2.0 - attemptT) / 0.6;
        a = clampD(a, 0, 1);
        char nm[48];
        levelName(curLevel, nm, sizeof(nm));
        snprintf(buf, sizeof(buf), "Attempt %d", attempt);
        COLORREF bgc = hslToColor(activeHue(), 60, 34);
        drawTextC(hdc, buf, WINDOW_W / 2, 170, 40, lerpColor(bgc, RGB(255, 255, 255), a));
        drawTextC(hdc, nm, WINDOW_W / 2, 218, 24, lerpColor(bgc, RGB(255, 240, 140), a));
    }

    if (practice) {
        drawText(hdc, "PRACTICE", 14, 60, 16, RGB(120, 255, 160));
        drawText(hdc, ckptSet ? "C - UPDATE CHECKPOINT" : "C - DROP CHECKPOINT", 14, 80, 14, RGB(170, 175, 195));
    }
    if (muted) drawText(hdc, "MUTED (M)", WINDOW_W - 130, 44, 16, RGB(180, 180, 190));
    if (toastT > 0 && toastText[0]) {
        double a = clampD(toastT / 0.4, 0, 1);
        drawTextC(hdc, toastText, WINDOW_W / 2, 480, 22, lerpColor(hslToColor(activeHue(), 60, 34), RGB(255, 230, 100), a));
    }
}

static void drawBtn(HDC hdc, const Btn& b, const char* label, int size, COLORREF col) {
    bool hov = btnHover(b);
    COLORREF base = hov ? brighten(col, 45) : col;
    fillRect(hdc, (int)b.x, (int)b.y, (int)b.w, (int)b.h, base);
    strokeRect(hdc, b.x, b.y, b.w, b.h, 3, RGB(255, 255, 255));
    fillRect(hdc, (int)b.x + 3, (int)b.y + 3, (int)b.w - 6, 4, brighten(base, 60));
    drawTextC(hdc, label, (int)(b.x + b.w / 2), (int)(b.y + (b.h - size) / 2) - 2, size, RGB(20, 20, 30));
}

static void drawMenuScene(HDC hdc) {
    int h = activeHue();
    double scroll = fmod(totalTime * 170.0, 480.0);
    for (int k = 0; k < 3; k++) {
        double x = WINDOW_W - scroll + k * 480;
        if (x < -60 || x > WINDOW_W + 60) continue;
        drawTriShape(hdc, x + 1, FLOOR_Y, x + 39, FLOOR_Y, x + 20, FLOOR_Y - 38,
                     hslToColor(h, 88, 58), RGB(255, 255, 255));
    }
    double hop = fabs(sin(totalTime * 2.2)) * 110;
    drawSkinCube(hdc, 200, FLOOR_Y - P_HALF - hop, fmod(totalTime * 300.0, 360.0), 16, save.equipped);
}

static void drawMenuUI(HDC hdc) {
    char buf[96];
    double pulse = beatPulse();
    int h = activeHue();
    snprintf(buf, sizeof(buf), "GEOMETRY DASH");
    drawTextC(hdc, buf, WINDOW_W / 2, 70, 74, hslToColor(h + 20, 95, (int)(62 + pulse * 16)));
    drawTextC(hdc, "50 LEVELS  -  SHOP  -  SHIP / BALL / UFO", WINDOW_W / 2, 158, 22, RGB(230, 235, 250));
    drawBtn(hdc, Btn{ 330, 250, 300, 62 }, "PLAY", 34, RGB(70, 220, 130));
    drawBtn(hdc, Btn{ 330, 330, 300, 62 }, "SHOP", 34, RGB(80, 170, 255));
    drawTextC(hdc, "SPACE / CLICK - JUMP    ESC - PAUSE    M - MUTE    F11 - FULLSCREEN", WINDOW_W / 2, 424, 18, RGB(215, 220, 240));
    drawTextC(hdc, "HOLD TO KEEP JUMPING - BLUE ORBS FLIP GRAVITY", WINDOW_W / 2, 452, 18, RGB(215, 220, 240));
    drawShardCount(hdc, WINDOW_W - 150, 20);
    int done = 0;
    for (int i = 0; i < LEVELS; i++) if (save.complete[i]) done++;
    snprintf(buf, sizeof(buf), "COMPLETED: %d / 50", done);
    drawTextC(hdc, buf, WINDOW_W / 2, 486, 22, RGB(255, 230, 120));
}

static void drawSelectUI(HDC hdc) {
    char buf[96];
    drawTextC(hdc, "SELECT LEVEL", WINDOW_W / 2, 16, 34, RGB(255, 255, 255));
    drawShardCount(hdc, WINDOW_W - 150, 16);
    for (int i = 0; i < LEVELS; i++) {
        double cx = 70 + (i % 5) * 164, cy = 76 + (i / 5) * 42;
        bool locked = i > save.unlocked;
        bool sel = i == curLevel;
        COLORREF col = locked ? RGB(45, 45, 60) : (save.complete[i] ? RGB(30, 90, 60) : RGB(35, 45, 85));
        if (sel && !locked) col = brighten(col, 45);
        fillRect(hdc, (int)cx, (int)cy, 150, 36, col);
        strokeRect(hdc, cx, cy, 150, 36, sel ? 3 : 2, sel ? RGB(255, 255, 160) : RGB(120, 130, 160));
        snprintf(buf, sizeof(buf), "%d", i + 1);
        drawText(hdc, buf, (int)cx + 8, (int)cy + 6, 22, locked ? RGB(110, 110, 125) : RGB(255, 255, 255));
        if (!locked) {
            drawFace(hdc, cx + 52, cy + 18, 12, levelTier(i));
            snprintf(buf, sizeof(buf), "%d%%", save.best[i]);
            drawText(hdc, buf, (int)cx + 96, (int)cy + 10, 16, save.complete[i] ? RGB(120, 255, 160) : RGB(200, 205, 225));
        } else {
            fillRect(hdc, (int)cx + 44, (int)cy + 12, 14, 12, RGB(110, 110, 125));
            strokeCircle(hdc, cx + 51, cy + 12, 6, 3, RGB(110, 110, 125));
        }
    }
    drawTextC(hdc, "CLICK A LEVEL OR PRESS ENTER  -  ESC TO GO BACK", WINDOW_W / 2, 502, 17, RGB(210, 215, 235));
    if (toastT > 0 && toastText[0]) drawTextC(hdc, toastText, WINDOW_W / 2, 470, 20, RGB(255, 230, 100));
}

static void drawShopUI(HDC hdc) {
    char buf[96];
    drawTextC(hdc, "ICON SHOP", WINDOW_W / 2, 14, 34, RGB(255, 255, 255));
    drawShardCount(hdc, WINDOW_W - 150, 16);
    for (int i = 0; i < 16; i++) {
        double cx = 48 + (i % 4) * 216, cy = 76 + (i / 4) * 106;
        bool owned = (save.owned & (1u << i)) != 0;
        bool equipped = save.equipped == i;
        bool sel = shopSel == i;
        COLORREF col = equipped ? RGB(28, 85, 55) : (owned ? RGB(35, 50, 95) : RGB(45, 45, 62));
        if (sel) col = brighten(col, 40);
        fillRect(hdc, (int)cx, (int)cy, 200, 100, col);
        strokeRect(hdc, cx, cy, 200, 100, sel ? 3 : 2, sel ? RGB(255, 255, 160) : RGB(120, 130, 160));
        drawSkinCube(hdc, cx + 100, cy + 36, totalTime * 90.0 + i * 30.0, 22, i);
        snprintf(buf, sizeof(buf), "%s", skins[i].name);
        drawTextC(hdc, buf, (int)(cx + 100), (int)(cy + 62), 18, RGB(255, 255, 255));
        if (equipped) drawTextC(hdc, "EQUIPPED", (int)(cx + 100), (int)(cy + 82), 15, RGB(120, 255, 160));
        else if (owned) drawTextC(hdc, "OWNED - CLICK TO EQUIP", (int)(cx + 100), (int)(cy + 82), 13, RGB(170, 210, 255));
        else {
            drawShard(hdc, cx + 62, cy + 89, 8);
            snprintf(buf, sizeof(buf), "%d", skins[i].price);
            drawText(hdc, buf, (int)cx + 76, (int)(cy + 80), 16,
                     save.shards >= skins[i].price ? RGB(160, 220, 255) : RGB(255, 140, 140));
        }
    }
    char labBtn[64];
    if (save.labOwned) {
        drawBtn(hdc, Btn{ 230, 496, 500, 36 }, "OPEN COLOR LAB - DESIGN YOUR CUBE", 20, RGB(170, 120, 255));
    } else {
        snprintf(labBtn, sizeof(labBtn), "COLOR LAB - %d / 10000 SHARDS", save.shards);
        drawBtn(hdc, Btn{ 230, 496, 500, 36 }, labBtn, 18,
                save.shards >= 10000 ? RGB(70, 220, 130) : RGB(95, 95, 115));
    }
    if (toastT > 0 && toastText[0]) drawTextC(hdc, toastText, WINDOW_W / 2, 466, 22, RGB(255, 230, 100));
}

static void drawLabUI(HDC hdc) {
    char buf[96];
    drawTextC(hdc, "COLOR LAB", WINDOW_W / 2, 14, 34, RGB(255, 255, 255));
    drawShardCount(hdc, WINDOW_W - 150, 16);
    drawSkinCube(hdc, 480, 118, totalTime * 90.0, 40, 16);
    const char* names[3] = { "PRIMARY", "SECONDARY", "GLOW" };
    for (int i = 0; i < 3; i++) {
        double sy = 200 + i * 58;
        int sat = (i == 0) ? 88 : ((i == 1) ? 80 : 100);
        int lig = (i == 0) ? 55 : ((i == 1) ? 33 : 70);
        for (int x = 0; x < 400; x++) {
            fillRect(hdc, 300 + x, (int)sy, 1, 30, hslToColor((int)(x * 359 / 399.0), sat, lig));
        }
        strokeRect(hdc, 300, sy, 400, 30, 2, RGB(255, 255, 255));
        drawTextC(hdc, names[i], 170, (int)sy + 5, 20, RGB(235, 238, 250));
        double kx = 300 + save.customHue[i] / 359.0 * 400.0;
        fillCircle(hdc, kx, sy + 15, 12, RGB(255, 255, 255));
        fillCircle(hdc, kx, sy + 15, 9, customColor(i));
    }
    drawTextC(hdc, "DRAW YOUR CUBE", 836, 92, 15, RGB(210, 215, 235));
    COLORREF palCols[6] = { customColor(0), customColor(1), customColor(2),
                            RGB(255, 255, 255), RGB(25, 25, 35), RGB(95, 95, 115) };
    for (int sw = 0; sw < 6; sw++) {
        int bx = 740 + sw * 30;
        fillRect(hdc, bx, 118, 26, 26, palCols[sw]);
        strokeRect(hdc, bx, 118, 26, 26, (g_drawPal == sw + 1) ? 3 : 2,
                   (g_drawPal == sw + 1) ? RGB(255, 255, 160) : RGB(120, 130, 160));
        if (sw == 5) drawTextC(hdc, "E", bx + 13, 121, 16, RGB(235, 238, 250));
    }
    fillRect(hdc, 740, 150, 192, 192, RGB(30, 32, 48));
    for (int rr = 0; rr < 12; rr++) {
        for (int cc = 0; cc < 12; cc++) {
            unsigned char v = save.drawCells[rr * 12 + cc];
            if (!v) continue;
            COLORREF dcol = (v == 1) ? customColor(0) : (v == 2) ? customColor(1) : (v == 3) ? customColor(2) :
                            (v == 4) ? RGB(255, 255, 255) : RGB(25, 25, 35);
            fillRect(hdc, 741 + cc * 16, 151 + rr * 16, 15, 15, dcol);
        }
    }
    strokeRect(hdc, 740, 150, 192, 192, 2, RGB(255, 255, 255));
    drawBtn(hdc, Btn{ 120, 372, 220, 46 }, "LOAD PHOTO", 22, RGB(80, 170, 255));
    drawBtn(hdc, Btn{ 360, 372, 220, 46 }, "CLEAR PHOTO", 22, RGB(255, 120, 120));
    drawBtn(hdc, Btn{ 600, 372, 240, 46 }, "EQUIP CUBE", 22, RGB(70, 220, 130));
    snprintf(buf, sizeof(buf), "%s   -   %s", g_photo ? "PHOTO: LOADED" : "PHOTO: NONE",
             (save.equipped == 16) ? "EQUIPPED" : "NOT EQUIPPED");
    drawTextC(hdc, buf, WINDOW_W / 2, 432, 18, g_photo ? RGB(160, 220, 255) : RGB(200, 205, 225));
    drawBtn(hdc, Btn{ 380, 466, 200, 46 }, "BACK", 22, RGB(120, 130, 160));
    drawBtn(hdc, Btn{ 660, 466, 260, 46 }, "CLEAR DRAWING", 20, RGB(255, 130, 130));
    if (toastT > 0 && toastText[0]) drawTextC(hdc, toastText, WINDOW_W / 2, 452, 20, RGB(255, 230, 100));
}

static void drawWinUI(HDC hdc) {
    char buf[96];
    fillRect(hdc, 0, 0, WINDOW_W, WINDOW_H, RGB(12, 14, 26));
    for (int i = 0; i < 46; i++) {
        double fy = fmod(totalTime * (85.0 + (i % 6) * 28.0) + i * 63.0, 580.0) - 30.0;
        double fx = ((i * 83) % 960) + sin(totalTime * 2.3 + i * 1.7) * 24.0;
        COLORREF c = hslToColor((int)(i * 34.0 + totalTime * 70.0) % 360, 88, 62);
        fillRect(hdc, fx, fy, 8 + (i % 3) * 4, 5, c);
    }
    drawTextC(hdc, "LEVEL COMPLETE!", WINDOW_W / 2, 70, 58, RGB(120, 255, 160));
    drawShard(hdc, WINDOW_W / 2 - 70, 178, 18);
    snprintf(buf, sizeof(buf), "+%d SHARDS", lastEarned);
    drawText(hdc, buf, WINDOW_W / 2 - 40, 164, 32, RGB(160, 220, 255));
    snprintf(buf, sizeof(buf), "ATTEMPTS: %d      TIME: %.1fs", attempt, attemptT);
    drawTextC(hdc, buf, WINDOW_W / 2, 230, 26, RGB(255, 255, 255));
    int r = tierReward(levelTier(curLevel));
    snprintf(buf, sizeof(buf), "DIFFICULTY REWARD: %d SHARDS", r);
    drawTextC(hdc, buf, WINDOW_W / 2, 274, 20, RGB(200, 205, 230));
    drawBtn(hdc, Btn{ 330, 330, 300, 54 }, "NEXT LEVEL", 26, RGB(70, 220, 130));
    drawBtn(hdc, Btn{ 330, 396, 300, 54 }, "RETRY", 26, RGB(255, 170, 60));
    drawBtn(hdc, Btn{ 330, 462, 300, 50 }, "LEVEL LIST", 24, RGB(80, 170, 255));
    drawShardCount(hdc, WINDOW_W - 150, 16);
}

static void drawPauseUI(HDC hdc) {
    fillRect(hdc, 0, 0, WINDOW_W, WINDOW_H, RGB(10, 10, 22));
    drawTextC(hdc, "PAUSED", WINDOW_W / 2, 160, 64, RGB(255, 255, 255));
    drawBtn(hdc, Btn{ 330, 250, 300, 54 }, "RESUME", 26, RGB(70, 220, 130));
    drawBtn(hdc, Btn{ 330, 318, 300, 54 }, "RESTART", 26, RGB(255, 170, 60));
    drawBtn(hdc, Btn{ 330, 386, 300, 54 }, "LEVEL LIST", 24, RGB(80, 170, 255));
    drawBtn(hdc, Btn{ 330, 454, 300, 50 }, practice ? "PRACTICE MODE: ON" : "PRACTICE MODE: OFF", 22,
            practice ? RGB(120, 255, 160) : RGB(150, 150, 170));
}

static void draw(HDC hdc) {
    int ox = 0, oy = 0;
    if (shake > 0.01) {
        ox = (int)(sin(totalTime * 57.0) * shake * 12.0);
        oy = (int)(cos(totalTime * 73.0) * shake * 12.0);
    }
    SetViewportOrgEx(hdc, ox, oy, NULL);
    drawBackground(hdc);
    drawGround(hdc);
    if (state == ST_MENU || state == ST_SELECT || state == ST_SHOP || state == ST_LAB) {
        drawMenuScene(hdc);
    } else if (state != ST_WIN) {
        drawObjects(hdc);
        drawParticles(hdc);
        drawSpeedLines(hdc);
        drawPlayer(hdc);
    }
    if (state == ST_SELECT || state == ST_SHOP || state == ST_LAB) {
        for (int y = 0; y < WINDOW_H; y += 2) fillRect(hdc, 0, y, WINDOW_W, 1, RGB(10, 12, 24));
    }
    SetViewportOrgEx(hdc, 0, 0, NULL);
    if (state == ST_MENU) drawMenuUI(hdc);
    else if (state == ST_SELECT) drawSelectUI(hdc);
    else if (state == ST_SHOP) drawShopUI(hdc);
    else if (state == ST_LAB) drawLabUI(hdc);
    else if (state == ST_WIN) { drawWinUI(hdc); drawParticles(hdc); }
    else drawHUD(hdc);
    if (state == ST_PLAY && paused) drawPauseUI(hdc);
    purgeGdi();
}

static void saveFrameBmp(HDC src, int w, int h, const char* path) {
    if (!src) return;
    BITMAPINFOHEADER bi;
    memset(&bi, 0, sizeof(bi));
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = h;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP dib = CreateDIBSection(src, (BITMAPINFO*)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!dib || !bits) { if (dib) DeleteObject(dib); return; }
    HDC dc = CreateCompatibleDC(src);
    HBITMAP old = (HBITMAP)SelectObject(dc, dib);
    BitBlt(dc, 0, 0, w, h, src, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteDC(dc);
    int stride = ((w * 3 + 3) / 4) * 4;
    FILE* f = fopen(path, "wb");
    if (f) {
        BITMAPFILEHEADER fh;
        memset(&fh, 0, sizeof(fh));
        fh.bfType = 0x4D42;
        fh.bfOffBits = 14 + 40;
        fh.bfSize = (DWORD)(14 + 40 + (long)stride * h);
        fwrite(&fh, 1, 14, f);
        fwrite(&bi, 1, 40, f);
        fwrite(bits, 1, (size_t)stride * h, f);
        fclose(f);
    }
    DeleteObject(dib);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        if (wParam == 0x7B) { saveFrameBmp(g_memDC, WINDOW_W, WINDOW_H, "gd_frame.bmp"); return 0; }
        if (wParam < 256) keys[wParam] = true;
        return 0;
    case WM_KEYUP:
        if (wParam < 256) keys[wParam] = false;
        return 0;
    case WM_LBUTTONDOWN:
        mouseDown = true;
        clickEdge = true;
        mx = (short)LOWORD(lParam);
        my = (short)HIWORD(lParam);
        if (g_fullscreen) {
            RECT frc;
            GetClientRect(hwnd, &frc);
            if (frc.right > 0 && frc.bottom > 0) {
                mx = mx * WINDOW_W / frc.right;
                my = my * WINDOW_H / frc.bottom;
            }
        }
        return 0;
    case WM_LBUTTONUP:
        mouseDown = false;
        return 0;
    case WM_MOUSEMOVE:
        mx = (short)LOWORD(lParam);
        my = (short)HIWORD(lParam);
        if (g_fullscreen) {
            RECT frc;
            GetClientRect(hwnd, &frc);
            if (frc.right > 0 && frc.bottom > 0) {
                mx = mx * WINDOW_W / frc.right;
                my = my * WINDOW_H / frc.bottom;
            }
        }
        return 0;
    case WM_KILLFOCUS:
        memset(keys, 0, sizeof(keys));
        mouseDown = false;
        if (state == ST_PLAY && !paused) {
            paused = true;
            pausedGlobal = true;
            pauseMusic();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    srand((unsigned)time(NULL));

    genSounds();
    loadGame();
    initMusic();
    initBgShapes();
    genLevel(0);

    char modulePath[MAX_PATH];
    GetModuleFileNameA(NULL, modulePath, MAX_PATH);
    char* slash = strrchr(modulePath, '\\');
    if (slash) {
        strcpy(slash + 1, "Fonts\\BM space.TTF");
        AddFontResourceExA(modulePath, FR_PRIVATE, 0);
        strcpy(slash + 1, "Fonts\\8-BIT WONDER.TTF");
        AddFontResourceExA(modulePath, FR_PRIVATE, 0);
    }

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "GeometryDashClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    RECT rc = { 0, 0, WINDOW_W, WINDOW_H };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);

    RECT wa = { 0, 0, 1280, 720 };
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    int winW = rc.right - rc.left, winH = rc.bottom - rc.top;
    int posX = wa.left + ((wa.right - wa.left) - winW) / 2;
    int posY = wa.top + ((wa.bottom - wa.top) - winH) / 2;
    if (posX < wa.left) posX = wa.left;
    if (posY < wa.top) posY = wa.top;

    HWND hwnd = CreateWindowA(
        "GeometryDashClass", "Geometry Dash",
        (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)),
        posX, posY, winW, winH,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    g_hwnd = hwnd;

    HDC hdc = GetDC(hwnd);
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, WINDOW_W, WINDOW_H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);
    g_memDC = memDC;

    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);

    double accumulator = 0.0;
    timeBeginPeriod(1);

    MSG msg;
    bool running = true;
    LARGE_INTEGER frameStart;
    while (running) {
        QueryPerformanceCounter(&frameStart);

        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        frameInput();
        pollMusic();

        QueryPerformanceCounter(&now);
        double elapsed = (double)(now.QuadPart - last.QuadPart) / freq.QuadPart;
        last = now;
        if (elapsed > 0.1) elapsed = 0.1;
        accumulator += elapsed;

        while (accumulator >= PHYS_DT) {
            update(PHYS_DT);
            accumulator -= PHYS_DT;
        }

        camX = (state == ST_PLAY || state == ST_DEAD || state == ST_WIN) ? (pl.x - SCREEN_PX) : fmod(totalTime * 170.0, 1000000.0);
        draw(memDC);
        if (g_fullscreen) {
            RECT frc;
            GetClientRect(hwnd, &frc);
            if (frc.right > 0 && frc.bottom > 0) {
                SetStretchBltMode(hdc, HALFTONE);
                SetBrushOrgEx(hdc, 0, 0, NULL);
                StretchBlt(hdc, 0, 0, frc.right, frc.bottom, memDC, 0, 0, WINDOW_W, WINDOW_H, SRCCOPY);
            }
        } else {
            BitBlt(hdc, 0, 0, WINDOW_W, WINDOW_H, memDC, 0, 0, SRCCOPY);
        }

        QueryPerformanceCounter(&now);
        double taken = (double)(now.QuadPart - frameStart.QuadPart) / freq.QuadPart;
        double wait = (1.0 / 60.0) - taken;
        if (wait > 0.001) Sleep((DWORD)(wait * 1000.0));
    }

    timeEndPeriod(1);
    stopMusic();
    if (musicReady) {
        mciSendStringA("close bgm0", NULL, 0, NULL);
        mciSendStringA("close bgm1", NULL, 0, NULL);
        mciSendStringA("close bgm2", NULL, 0, NULL);
    }
    for (int t = 0; t < 3; t++) if (musicPath[t][0]) DeleteFileA(musicPath[t]);
    saveGame();

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    ReleaseDC(hwnd, hdc);

    return 0;
}

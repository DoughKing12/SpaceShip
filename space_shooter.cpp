#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <algorithm>
#include <vector>
#include <map>
#include <ctime>

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

// --- Constants ---
static const int WINDOW_W = 640;
static const int WINDOW_H = 860;
static const int FPS = 60;
static const double PI = 3.14159265358979;

// --- Random ---
static int randRange(int lo, int hi) { return lo + (rand() % (hi - lo + 1)); }
static double randf() { return (double)rand() / RAND_MAX; }

// --- Sound System ---
enum SoundId { SND_LASER, SND_BOSS_WARN, SND_LEVEL_UP, SND_HISCORE, SND_EXPLOSION, SND_POWERUP, SND_COUNT };
static std::vector<BYTE> soundData[SND_COUNT];
static int prioritySoundTimer = 0;
static int laserSoundCooldown = 0;
static int highScore = 2650;

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

static void initSounds() {
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

static void playSound(SoundId id) {
    if (prioritySoundTimer > 0) return;
    if (soundData[id].empty()) return;
    PlaySoundA((LPCSTR)soundData[id].data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

static void playPrioritySound(SoundId id, int blockFrames) {
    if (soundData[id].empty()) return;
    prioritySoundTimer = blockFrames;
    PlaySoundA((LPCSTR)soundData[id].data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// --- MP3 Sound Effects (from /sounds folder) ---
static char soundDir[MAX_PATH] = "";

static void initMciSounds() {
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

static void playMci(const char* alias, int limitMs = 0) {
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
static volatile bool shootSfxPending = false;

static DWORD WINAPI soundThreadProc(LPVOID) {
    for (;;) {
        if (shootSfxPending) {
            shootSfxPending = false;
            playMci("sfxshoot");
        }
        Sleep(1);
    }
    return 0;
}

static void initSoundThread() {
    CreateThread(NULL, 0, soundThreadProc, NULL, 0, NULL);
}

// --- Vec2 ---
struct Vec2 {
    double x, y;
    Vec2(double x = 0, double y = 0) : x(x), y(y) {}
    Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(double s) const { return { x * s, y * s }; }
    double len() const { return sqrt(x * x + y * y); }
    Vec2 norm() const { double l = len(); return l > 0 ? Vec2{ x / l, y / l } : Vec2{ 0, 0 }; }
};

// --- Particle ---
struct Particle {
    Vec2 pos, vel;
    double life, decay, size;
    COLORREF color;
    bool isSpark;
};

// --- Bullet ---
struct Bullet {
    Vec2 pos, vel;
    double dmg;
    COLORREF color;
    bool isEnemy;
    bool homing;
};

// --- Enemy ---
enum EnemyType { ENEMY_BASIC, ENEMY_FAST, ENEMY_SHOOTER, ENEMY_ELITE, ENEMY_KAMIKAZE, ENEMY_SPLITTER };
struct Enemy {
    Vec2 pos;
    int hp, maxHp;
    double speed, shootTimer, shootRate;
    EnemyType type;
    COLORREF color;
    double phase;
    double w, h;
    int flash;
    double drift;      // per-enemy sideways drift so squads spread apart
    double swayAmp;    // how strongly this enemy sways
    int shieldN;       // shield hits before taking damage
    bool child;        // split off from a splitter - smaller, no re-split
};

// --- Boss ---
enum BossPattern { BOSS_SINE, BOSS_CIRCLE, BOSS_SPIRAL, BOSS_AIMED, BOSS_BURST, BOSS_ALL };
struct Boss {
    Vec2 pos;
    int hp, maxHp;
    double speed, shootTimer, shootRate, bulletSpeed;
    double size;
    BossPattern pattern;
    COLORREF color;
    double phase, spiralAngle;
    int moveDir;
    bool entered;
    double targetY;
    int flash;
    int telegraph;   // frames of warning before the next bullet wave
};

// --- Star ---
struct Star {
    double x, y, speed, size, brightness;
};

// --- Power-Up ---
enum PowerUpType { PU_TRIPLE, PU_SHIELD, PU_RAPID, PU_NUKE, PU_EXTRA_LIFE, PU_PRISM, PU_HOMING, PU_LASER };
struct PowerUp {
    Vec2 pos;
    double phase;
    PowerUpType type;
};

// --- Consumable Drop ---
enum ConsumableType { CONS_GRENADE, CONS_REPAIR, CONS_OVERCLOCK };
struct ConsumableDrop {
    Vec2 pos;
    double phase;
    ConsumableType type;
};

// --- Bombardment Rock ---
struct Bombard {
    Vec2 pos, vel;
    double size, rot, rotSpd;
    COLORREF color;
};

// --- Score Gem ---
struct Gem {
    Vec2 pos;
    double phase;
    int value;
    COLORREF color;
};

// --- Level Config ---
struct LevelConfig {
    bool isBoss;
    int enemyCount;
    int spawnRate;
    int enemyHp;
    double enemySpeed;
    int enemyShootRate;
    COLORREF enemyColor;
    EnemyType enemyType;
};

// --- Game State ---
enum GameState { STATE_MENU, STATE_PLAYING, STATE_GAMEOVER, STATE_WIN };

struct Game {
    GameState state;
    int score, level, lives;
    double screenShake;
    int levelTimer, spawnTimer, levelEnemiesLeft;
    bool bossActive;

    Vec2 ship;
    double shipW, shipH;
    int shipShootCooldown, shipShootRate, shipInvincible;
    int shipPowerLevel;
    double shipThrustPhase;

    int shieldCount;
    int rapidTimer;
    double abilityPulse;
    char msgText[64];
    int msgTimer;

    std::vector<Bullet> bullets;
    std::vector<Bullet> enemyBullets;
    std::vector<Enemy> enemies;
    std::vector<Particle> particles;
    std::vector<Star> stars;
    std::vector<PowerUp> powerups;
    std::vector<ConsumableDrop> consumables;
    std::vector<Bombard> bombards;
    std::vector<Gem> gems;
    Boss boss;
    bool bossAlive;

    int grenades;
    int repairKits;
    int freezeCharges;

    int comboCount;
    int comboTimer;
    int slowmoTimer;
    int laserTimer, laserCooldown;
    int freezeTimer;
    int homingTimer;
    int bombardPending, bombardTimer, bombardLeft;
    bool bombardActive;
    bool endless;
    bool paused;
    bool killArmed;
    int continues;
    int bannerTimer;
    char bannerText[64];

    double winScroll;

    bool keys[256];
    bool keysPrev[256];

    bool cheatGod = false;
    bool cheatInfShield = false;
    bool cheatInfLives = false;
    bool cheatInfGrenade = false;
    bool cheatInfFreeze = false;
    bool cheatRainbow = false;
    bool cheatSlow = false;
    double cheatSpeed = 1.0;
    COLORREF shipColor = RGB(0, 255, 255);
};

static Game game;

// --- Hidden Console ---
enum ConsoleState { CONS_CLOSED, CONS_PASSWORD, CONS_OPEN };
struct Console {
    ConsoleState state = CONS_CLOSED;
    char input[256];
    int inputLen = 0;
    char log[8][128];
    int logCount = 0;
};
Console console;

static void consoleLog(const char* text) {
    for (int i = 0; i < 7; i++) memcpy(console.log[i], console.log[i + 1], sizeof(console.log[i]));
    strncpy(console.log[7], text, sizeof(console.log[7]) - 1);
    console.log[7][sizeof(console.log[7]) - 1] = 0;
    if (console.logCount < 8) console.logCount++;
}

static void consoleWelcome() {
    consoleLog("ADMIN CONSOLE - PRESS F1 TO CLOSE");
    consoleLog("GOD           INVINCIBLE");
    consoleLog("INF SHIELD    INFINITE SHIELD");
    consoleLog("INF LIVES / BOMBS / FREEZE");
    consoleLog("MAX/NUKE/SLOW/BOSS/LEVEL N");
    consoleLog("INF FREEZE    PRESS J = INF FREEZE");
    consoleLog("FREEZE        TIME FREEZE 6S");
    consoleLog("MINI/BIG/RAINBOW/SPEED N");
    consoleLog("KILLALL       KILL ALL + ARM P KEY");
    consoleLog("P KEY KILLS AFTER KILLALL INPUT");
}

static const char* CONSOLE_PASSWORD = "3412";

static void execConsoleCommand(const char* cmd);

static void consoleSubmit() {
    if (console.state == CONS_PASSWORD) {
        if (strcmp(console.input, CONSOLE_PASSWORD) == 0) {
            console.state = CONS_OPEN;
            consoleWelcome();
        } else {
            consoleLog("WRONG PASSWORD");
        }
        console.inputLen = 0;
        console.input[0] = 0;
        return;
    }
    execConsoleCommand(console.input);
    console.inputLen = 0;
    console.input[0] = 0;
}

// --- Helpers ---
static double dist(Vec2 a, Vec2 b) { return (a - b).len(); }

static void addParticle(Vec2 pos, Vec2 vel, double life, double decay, double size, COLORREF color, bool spark = false) {
    if (vel.y < 0) vel.y = -vel.y;
    game.particles.push_back({ pos, vel, life, decay, size, color, spark });
}

static void screenShakeAmt(double amt) {
    if (amt > game.screenShake) game.screenShake = amt;
}

static COLORREF hslToColor(int h, int s, int l) {
    double ss = s / 100.0, ll = l / 100.0;
    double c = (1.0 - fabs(2.0 * ll - 1.0)) * ss;
    double x = c * (1.0 - fabs(fmod(h / 60.0, 2.0) - 1.0));
    double m = ll - c / 2.0;
    double r = 0, g = 0, b = 0;
    if (h < 60)      { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return RGB((int)((r + m) * 255), (int)((g + m) * 255), (int)((b + m) * 255));
}

// --- Level Config ---
static LevelConfig getLevelConfig(int level) {
    LevelConfig cfg{};
    bool isBossLevel = (level == 5 || level == 10 || level == 20 || level == 30 || level == 40 || level == 50);
    if (game.endless && level > 50 && level % 10 == 0) isBossLevel = true;
    cfg.isBoss = isBossLevel;

    if (isBossLevel) {
        cfg.enemyCount = 0;
        return cfg;
    }

    double diff = 1.0 + (level - 1) * 0.09;
    cfg.enemyCount = (level < 6) ? 8 : (level >= 10 ? 11 : 10);
    cfg.spawnRate = max(24, 50 - (int)(level * 0.7));
    cfg.enemyHp = (int)ceil(diff);
    cfg.enemySpeed = 1.0 + level * 0.04;
    cfg.enemyShootRate = max(60, 90 - (int)(level * 0.5));
    cfg.enemyColor = hslToColor((level * 37) % 360, 70, 60);

    if (level <= 5) cfg.enemyType = ENEMY_BASIC;
    else if (level <= 12) cfg.enemyType = ENEMY_FAST;
    else if (level <= 19) cfg.enemyType = ENEMY_SHOOTER;
    else if (level <= 29) cfg.enemyType = ENEMY_ELITE;
    else cfg.enemyType = (EnemyType)(level % 6); // variety forever in endless

    return cfg;
}

// --- Explosion ---
static void createExplosion(Vec2 pos, COLORREF color, int count, double size) {
    for (int i = 0; i < count; i++) {
        double angle = (2.0 * PI / count) * i + randf() * 0.3;
        double spd = randf() * size + 1.0;
        addParticle(pos, { cos(angle) * spd, sin(angle) * spd }, 1.0, 0.01 + randf() * 0.02, randf() * 4.0 + 2.0, color);
    }
    for (int i = 0; i < count / 2; i++) {
        double angle = randf() * 2.0 * PI;
        double spd = randf() * size * 0.5 + 0.5;
        addParticle(pos, { cos(angle) * spd, sin(angle) * spd }, 1.0, 0.005 + randf() * 0.01, randf() * 2.0 + 1.0, RGB(255, 255, 255), true);
    }
    screenShakeAmt(min(count * 0.5, 15.0));
}

static void createDebris(Vec2 pos, COLORREF color, int count) {
    for (int i = 0; i < count; i++) {
        double angle = randf() * 2.0 * PI;
        double spd = randf() * 3.0 + 1.0;
        addParticle(pos, { cos(angle) * spd, sin(angle) * spd - 1.0 }, 1.0, 0.005 + randf() * 0.008, randf() * 3.0 + 1.0, color);
    }
}

static void createShieldHit(Vec2 pos) {
    for (int i = 0; i < 12; i++) {
        double angle = (2.0 * PI / 12) * i;
        addParticle(pos, { cos(angle) * 3.0, sin(angle) * 3.0 }, 1.0, 0.04, 3.0, RGB(0, 255, 255));
    }
}

// --- Power-Up Helpers ---
static COLORREF powerUpColor(PowerUpType t) {
    switch (t) {
    case PU_TRIPLE:     return RGB(0, 170, 255);
    case PU_SHIELD:     return RGB(0, 255, 180);
    case PU_RAPID:      return RGB(255, 220, 0);
    case PU_NUKE:       return RGB(255, 50, 255);
    case PU_EXTRA_LIFE: return RGB(50, 255, 80);
    case PU_PRISM:      return RGB(255, 100, 255);
    case PU_HOMING:     return RGB(120, 255, 120);
    case PU_LASER:      return RGB(255, 140, 40);
    }
    return RGB(255, 255, 255);
}

static const char* powerUpName(PowerUpType t) {
    switch (t) {
    case PU_TRIPLE:     return "POWER UP";
    case PU_SHIELD:     return "SHIELD";
    case PU_RAPID:      return "RAPID FIRE";
    case PU_NUKE:       return "SCREEN NUKE";
    case PU_EXTRA_LIFE: return "+1 LIFE";
    case PU_PRISM:      return "PRISM SHOT";
    case PU_HOMING:     return "HOMING MISSILES";
    case PU_LASER:      return "LASER BEAM";
    }
    return "???";
}

// --- Consumable Helpers ---
static void addScore(int pts);
static COLORREF consumableColor(ConsumableType t) {
    switch (t) {
    case CONS_GRENADE:   return RGB(255, 120, 0);
    case CONS_REPAIR:    return RGB(0, 255, 80);
    case CONS_OVERCLOCK: return RGB(120, 220, 255);
    }
    return RGB(255, 255, 255);
}

static const char* consumableName(ConsumableType t) {
    switch (t) {
    case CONS_GRENADE:   return "GRENADE";
    case CONS_REPAIR:    return "REPAIR KIT";
    case CONS_OVERCLOCK: return "TIME FREEZE";
    }
    return "???";
}

static char consumableKeyChar(ConsumableType t) {
    switch (t) {
    case CONS_GRENADE:   return 'G';
    case CONS_REPAIR:    return 'H';
    case CONS_OVERCLOCK: return 'J';
    }
    return '?';
}

static void dropConsumableChance(Vec2 pos) {
    int roll = rand() % 100;
    ConsumableType t = CONS_GRENADE;
    if (roll < 3) t = CONS_GRENADE;
    else if (roll < 6) t = CONS_REPAIR;
    else if (roll < 9) t = CONS_OVERCLOCK;
    else return;
    game.consumables.push_back({ pos, randf() * 2.0 * PI, t });
}

static void collectConsumable(ConsumableType t) {
    switch (t) {
    case CONS_GRENADE:
        game.grenades = min(game.grenades + 1, 3);
        break;
    case CONS_REPAIR:
        game.repairKits = min(game.repairKits + 1, 2);
        break;
    case CONS_OVERCLOCK:
        game.freezeCharges = min(game.freezeCharges + 1, 3);
        break;
    }
    playSound(SND_POWERUP);
    snprintf(game.msgText, sizeof(game.msgText), "GOT %s!", consumableName(t));
    game.msgTimer = FPS * 2;
    for (int i = 0; i < 12; i++) {
        double angle = randf() * 2.0 * PI;
        double spd = randf() * 2.5 + 0.5;
        addParticle(game.ship, { cos(angle) * spd, sin(angle) * spd }, 1.0, 0.012, 3.0, consumableColor(t));
    }
}

static void useGrenade() {
    if (game.grenades <= 0) return;
    if (!game.cheatInfGrenade) game.grenades--;
    for (auto& e : game.enemies) {
        createExplosion(e.pos, e.color, 15, 3.0);
        createDebris(e.pos, e.color, 6);
        addScore(30);
    }
    game.enemies.clear();
    game.enemyBullets.clear();
    game.levelEnemiesLeft = 0;
    screenShakeAmt(20.0);
    playSound(SND_EXPLOSION);
    snprintf(game.msgText, sizeof(game.msgText), "GRENADE!!");
    game.msgTimer = FPS * 2;
}

static void useRepairKit() {
    if (game.repairKits <= 0 || game.lives >= 5) return;
    game.repairKits--;
    game.lives++;
    snprintf(game.msgText, sizeof(game.msgText), "+1 LIFE");
    game.msgTimer = FPS * 2;
}

static void useTimeFreeze() {
    if (game.cheatInfFreeze) {
        game.freezeTimer = max(game.freezeTimer, FPS * 8);
        playSound(SND_POWERUP);
        snprintf(game.msgText, sizeof(game.msgText), "TIME FREEZE!");
        game.msgTimer = FPS * 2;
        return;
    }
    if (game.freezeCharges <= 0) return;
    game.freezeCharges--;
    game.freezeTimer = max(game.freezeTimer, FPS * 6);
    playSound(SND_POWERUP);
    snprintf(game.msgText, sizeof(game.msgText), "TIME FREEZE!");
    game.msgTimer = FPS * 2;
}

static void dropBossRewards() {
    int count = game.level >= 40 ? 3 : (game.level >= 20 ? 2 : 1);
    for (int i = 0; i < count; i++) {
        int roll = rand() % 100;
        PowerUpType t;
        if (roll < 16)       t = PU_TRIPLE;
        else if (roll < 32)  t = PU_SHIELD;
        else if (roll < 48)  t = PU_RAPID;
        else if (roll < 62)  t = PU_NUKE;
        else if (roll < 78)  t = PU_EXTRA_LIFE;
        else if (roll < 90)  t = PU_PRISM;
        else if (roll < 95)  t = PU_HOMING;
        else                 t = PU_LASER;
        double px = game.boss.pos.x + (randf() - 0.5) * 120.0;
        px = max(30.0, min((double)(WINDOW_W - 30), px));
        game.powerups.push_back({ { px, game.boss.pos.y + (randf() - 0.5) * 30.0 }, randf() * 2.0 * PI, t });
    }
}

static void applyPowerUp(PowerUpType t) {
    switch (t) {
    case PU_TRIPLE:
        game.shipPowerLevel = min(game.shipPowerLevel + 1, 3);
        break;
    case PU_SHIELD:
        game.shieldCount = min(game.shieldCount + 3, 5);
        break;
    case PU_RAPID:
        game.rapidTimer = FPS * 10;
        break;
    case PU_NUKE:
        for (auto& e : game.enemies) {
            createExplosion(e.pos, e.color, 15, 3.0);
            game.score += 50;
        }
        game.enemies.clear();
        game.enemyBullets.clear();
        screenShakeAmt(20.0);
        break;
    case PU_EXTRA_LIFE:
        game.lives = min(game.lives + 1, 5);
        break;
    case PU_PRISM:
        game.shipPowerLevel = min(game.shipPowerLevel + 1, 3);
        game.rapidTimer = max(game.rapidTimer, FPS * 8);
        break;
    case PU_HOMING:
        game.homingTimer = FPS * 12;
        break;
    case PU_LASER:
        game.laserTimer = FPS * 12;
        break;
    }
    for (int i = 0; i < 15; i++) {
        double angle = randf() * 2.0 * PI;
        double spd = randf() * 3.0 + 1.0;
        addParticle(game.ship, { cos(angle) * spd, sin(angle) * spd }, 1.0, 0.01, 4.0, powerUpColor(t));
    }
    if (t == PU_NUKE) playSound(SND_EXPLOSION);
    snprintf(game.msgText, sizeof(game.msgText), "%s", powerUpName(t));
    game.msgTimer = FPS * 2;
}

// --- Splitter children (spawned after the enemy loop, to avoid iterator invalidation) ---
struct SplitSpec { double x, y; int hp; double spd; COLORREF col; };
static std::vector<SplitSpec> pendingSplits;

// --- Combo / scoring ---
static int comboMult() { return 1 + min(game.comboCount / 5, 4); }

static void award(int pts) {
    game.comboCount++;
    game.comboTimer = FPS * 3;
    addScore(pts * comboMult());
}

static void bannerSet(const char* text) {
    snprintf(game.bannerText, sizeof(game.bannerText), "%s", text);
    game.bannerTimer = FPS * 2;
}

static bool currentIsBossLevel() {
    int lv = game.level;
    if (lv == 5 || lv == 10 || lv == 20 || lv == 30 || lv == 40 || lv == 50) return true;
    if (game.endless && lv > 50 && lv % 10 == 0) return true;
    return false;
}

static bool isBombardLevel() {
    int lv = game.level;
    if (lv % 7 != 0) return false;
    if (currentIsBossLevel()) return false;
    return true;
}

// --- Score gems ---
static void dropGemChance(Vec2 pos, int extraRoll = 30) {
    if ((rand() % 100) >= extraRoll) return;
    game.gems.push_back({ { pos.x + (randf() - 0.5) * 10, pos.y + (randf() - 0.5) * 8 },
        randf() * 2.0 * PI, 5 + rand() % 25, RGB(255, 220, 40) });
}

static void dropGemShower(Vec2 pos, int count) {
    for (int i = 0; i < count; i++) {
        game.gems.push_back({ { pos.x + (randf() - 0.5) * 140, pos.y + (randf() - 0.5) * 60 },
            randf() * 2.0 * PI, 10 + rand() % 40, RGB(255, 220, 40) });
    }
}

// --- Kill an enemy: score, boom, loot, split children ---
static void killEnemy(Enemy& e) {
    int pts = e.child ? 50
        : e.type == ENEMY_ELITE ? 300 : e.type == ENEMY_KAMIKAZE ? 180
        : e.type == ENEMY_SPLITTER ? 250 : e.type == ENEMY_SHOOTER ? 200
        : e.type == ENEMY_FAST ? 120 : 100;
    award(pts);
    createExplosion(e.pos, e.color, 20, 4.0);
    createDebris(e.pos, e.color, 8);
    screenShakeAmt(6.0);
    playSound(SND_EXPLOSION);
    dropConsumableChance(e.pos);
    dropGemChance(e.pos);
    if (!e.child && e.type == ENEMY_SPLITTER) {
        for (int i = 0; i < 2; i++) {
            pendingSplits.push_back({
                e.pos.x + (i ? 14 : -14), e.pos.y + 6,
                max(1, e.maxHp / 2), e.speed * 0.8,
                hslToColor((game.level * 37) % 360, 70, 66)
            });
        }
    }
}

static void killBoss() {
    Boss& b2 = game.boss;
    award(2000 + game.level * 500);
    for (int i = 0; i < 8; i++) {
        Vec2 ep = { b2.pos.x + (randf() - 0.5) * b2.size * 1.5, b2.pos.y + (randf() - 0.5) * b2.size };
        createExplosion(ep, b2.color, 30, 5.0);
        createDebris(b2.pos, b2.color, 15);
    }
    dropBossRewards();
    dropGemShower(b2.pos, 10);
    screenShakeAmt(15.0);
    playSound(SND_EXPLOSION);
    b2.hp = 0;
    game.bossAlive = false;
    game.bossActive = false;
    game.levelTimer = 0;
    game.slowmoTimer = FPS * 2;
    if (game.level < 50) bannerSet("BOSS DOWN!");
    else if (!game.endless) bannerSet("FINAL BOSS DOWN!");
    else bannerSet("BOSS DOWN!");
}

// --- Console Commands ---
static void resetGame();
static void nextLevel();
static void spawnBoss(int level);

static void killAllEnemies() {
    for (auto& e : game.enemies) {
        createExplosion(e.pos, e.color, 20, 4.0);
        createDebris(e.pos, e.color, 8);
        award(100);
        dropConsumableChance(e.pos);
        dropGemChance(e.pos);
    }
    game.enemies.clear();
    game.enemyBullets.clear();

    if (game.bossAlive) killBoss();

    screenShakeAmt(20.0);
    playSound(SND_EXPLOSION);
}

static void execConsoleCommand(const char* cmd) {
    char buf[256];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    while (*buf == ' ' || *buf == '/') memmove(buf, buf + 1, strlen(buf));

    // lowercase for matching
    char lower[256];
    strncpy(lower, buf, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = 0;
    for (char* p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);

    char a[32] = {}, b[64] = {}, c[32] = {};
    int n1 = 0;
    sscanf(lower, "%31s %63s %31s", a, b, c);

    if (strcmp(a, "help") == 0 || strcmp(a, "?") == 0) {
        consoleLog("COMMANDS (HIT / THEN TYPE)");
        consoleLog("GOD         - INVINCIBLE");
        consoleLog("INF SHIELD  - INFINITE SHIELD");
        consoleLog("INF LIVES   - INFINITE LIVES");
        consoleLog("INF BOMBS   - INFINITE GRENADES");
        consoleLog("MAX         - FULL POWER + RAPID");
        consoleLog("NUKE        - DESTROY ALL ENEMIES");
        consoleLog("KILL ALL    - KILL EVERYTHING (EVEN BOSS)");
        consoleLog("LEVEL N     - JUMP TO LEVEL");
        consoleLog("BOSS        - SPAWN BOSS");
        consoleLog("LIVES N     - SET LIVES");
        consoleLog("SCORE N     - SET SCORE");
        consoleLog("SPEED N     - SHIP SPEED (1-10)");
        consoleLog("SLOW        - TOGGLE SLOW ENEMIES");
        consoleLog("MINI        - TINY SHIP");
        consoleLog("BIG         - GIANT SHIP");
        consoleLog("RAINBOW     - RAINBOW SHIP");
        consoleLog("ENDLESS     - ENDLESS MODE PAST LV 50");
        consoleLog("OFF ALL     - DISABLE ALL CHEATS");
        return;
    }

    if (strcmp(a, "god") == 0) {
        game.cheatGod = !game.cheatGod;
        consoleLog(game.cheatGod ? "GOD MODE ON" : "GOD MODE OFF");
        return;
    }
    if (strcmp(a, "inf") == 0) {
        if (strcmp(b, "shield") == 0) {
            game.cheatInfShield = true;
            game.shieldCount = max(game.shieldCount, 5);
            consoleLog("INFINITE SHIELD ON");
        } else if (strcmp(b, "lives") == 0 || strcmp(b, "health") == 0) {
            game.cheatInfLives = true;
            game.lives = 99;
            consoleLog("INFINITE LIVES ON");
        } else if (strcmp(b, "bombs") == 0 || strcmp(b, "grenade") == 0) {
            game.cheatInfGrenade = true;
            game.grenades = 99;
            consoleLog("INFINITE BOMBS ON");
        } else if (strcmp(b, "freeze") == 0) {
            game.cheatInfFreeze = !game.cheatInfFreeze;
            consoleLog(game.cheatInfFreeze ? "INFINITE FREEZE ON - PRESS J (8S)" : "INFINITE FREEZE OFF");
        } else {
            consoleLog("USE: INF SHIELD / LIVES / BOMBS / FREEZE");
        }
        return;
    }
    if (strcmp(a, "max") == 0) {
        game.shipPowerLevel = 3;
        game.rapidTimer = max(game.rapidTimer, FPS * 15);
        consoleLog("FULL POWER READY");
        return;
    }
    if (strcmp(a, "freeze") == 0) {
        game.freezeTimer = max(game.freezeTimer, FPS * 6);
        playSound(SND_POWERUP);
        snprintf(game.msgText, sizeof(game.msgText), "TIME FREEZE!");
        game.msgTimer = FPS * 2;
        consoleLog("TIME FREEZE!");
        return;
    }
    if (strcmp(a, "nuke") == 0 || strcmp(a, "killall") == 0 || strcmp(a, "kill") == 0) {
        if (game.state != STATE_PLAYING) resetGame();
        bool wasBoss = game.bossAlive;
        killAllEnemies();
        game.killArmed = true;
        consoleLog(wasBoss ? "ALL CLEARED (BOSS DOWN)" : "ALL CLEARED");
        consoleLog("P KEY ARMED - KILL ANYTIME");
        return;
    }
    if ((strcmp(a, "go") == 0 && (strcmp(b, "lv") == 0 || strcmp(b, "level") == 0)) ||
        strcmp(a, "lv") == 0) {
        int lv = atoi((strcmp(a, "go") == 0) ? c : b);
        if (lv < 1) lv = 1;
        if (lv > 50) lv = 50;
        if (game.state != STATE_PLAYING) resetGame();
        game.level = max(1, lv - 1);
        nextLevel();
        game.bossActive = false; // nextLevel already handles boss
        game.shipInvincible = 120;
        snprintf(game.msgText, sizeof(game.msgText), "LEVEL %d", game.level);
        game.msgTimer = FPS * 2;
        consoleLog("JUMPED TO LEVEL");
        return;
    }
    if (strcmp(a, "level") == 0) {
        int lv = atoi(b);
        if (lv < 1) lv = 1;
        if (lv > 50) lv = 50;
        if (game.state != STATE_PLAYING) resetGame();
        game.level = max(1, lv - 1);
        nextLevel();
        game.bossActive = false; // nextLevel already handles boss
        game.shipInvincible = 120;
        snprintf(game.msgText, sizeof(game.msgText), "LEVEL %d", game.level);
        game.msgTimer = FPS * 2;
        consoleLog("JUMPED TO LEVEL");
        return;
    }
    if (strcmp(a, "boss") == 0) {
        if (game.state != STATE_PLAYING) resetGame();
        spawnBoss(game.level);
        game.levelTimer = 0;
        consoleLog("BOSS SUMMONED");
        return;
    }
    if (strcmp(a, "endless") == 0) {
        game.endless = !game.endless;
        consoleLog(game.endless ? "ENDLESS MODE ON (CONTINUES PAST 50)" : "ENDLESS MODE OFF");
        return;
    }
    if (strcmp(a, "lives") == 0) {
        int lv = atoi(b);
        if (lv >= 1 && lv <= 99) {
            game.lives = lv;
            snprintf(console.log[7], sizeof(console.log[7]), "LIVES SET TO %d", lv);
        } else {
            consoleLog("USE: LIVES 1-99");
        }
        return;
    }
    if (strcmp(a, "score") == 0) {
        int sc = atoi(b);
        game.score = sc;
        consoleLog("SCORE SET");
        return;
    }
    if (strcmp(a, "speed") == 0) {
        double sp = atof(b);
        if (sp >= 1 && sp <= 10) {
            game.cheatSpeed = sp;
            consoleLog("SHIP BOOSTED");
        } else {
            consoleLog("USE: SPEED 1-10");
        }
        return;
    }
    if (strcmp(a, "slow") == 0) {
        game.cheatSlow = !game.cheatSlow;
        consoleLog(game.cheatSlow ? "SLOW MOTION ON" : "SLOW MOTION OFF");
        return;
    }
    if (strcmp(a, "mini") == 0) {
        game.shipW = 22; game.shipH = 22;
        consoleLog("TINY SHIP");
        return;
    }
    if (strcmp(a, "big") == 0) {
        game.shipW = 70; game.shipH = 70;
        consoleLog("GIANT SHIP");
        return;
    }
    if (strcmp(a, "rainbow") == 0) {
        game.cheatRainbow = !game.cheatRainbow;
        consoleLog(game.cheatRainbow ? "RAINBOW ON" : "RAINBOW OFF");
        return;
    }
    if (strcmp(a, "off") == 0) {
        if (strcmp(b, "all") == 0 || strcmp(b, "") == 0) {
            game.cheatGod = false;
            game.cheatInfShield = false;
            game.cheatInfLives = false;
            game.cheatInfGrenade = false;
            game.cheatRainbow = false;
            game.cheatSlow = false;
            game.cheatSpeed = 1.0;
            game.shipW = 40; game.shipH = 40;
            consoleLog("ALL CHEATS OFF");
        } else if (strcmp(b, "shield") == 0) {
            game.cheatInfShield = false;
            consoleLog("SHIELD CHEAT OFF");
        } else if (strcmp(b, "lives") == 0) {
            game.cheatInfLives = false;
            consoleLog("LIVES CHEAT OFF");
        } else if (strcmp(b, "bombs") == 0) {
            game.cheatInfGrenade = false;
            consoleLog("BOMBS CHEAT OFF");
        } else if (strcmp(b, "slow") == 0) {
            game.cheatSlow = false;
            consoleLog("SLOW MOTION OFF");
        } else if (strcmp(b, "god") == 0) {
            game.cheatGod = false;
            consoleLog("GOD MODE OFF");
        } else if (strcmp(b, "rainbow") == 0) {
            game.cheatRainbow = false;
            consoleLog("RAINBOW OFF");
        } else if (strcmp(b, "speed") == 0) {
            game.cheatSpeed = 1.0;
            consoleLog("SPEED RESET");
        } else {
            consoleLog("USE: OFF GOD/SHIELD/LIVES/BOMBS/RAINBOW");
        }
        return;
    }

    consoleLog("UNKNOWN COMMAND - TYPE HELP");
}

// --- Scoring ---
struct ScoreEntry { int score; int level; };
static ScoreEntry topScores[5] = { {2650,1},{1800,1},{1200,1},{800,1},{500,1} };

static void saveHighScore() {
    FILE* f = fopen("space_shooter_save.dat", "wb");
    if (!f) return;
    fwrite("SS5", 1, 3, f);
    fwrite(topScores, sizeof(ScoreEntry), 5, f);
    fclose(f);
}

static void loadHighScore() {
    FILE* f = fopen("space_shooter_save.dat", "rb");
    if (!f) { highScore = topScores[0].score; return; }
    char magic[3] = { 0, 0, 0 };
    if (fread(magic, 1, 3, f) == 3 && memcmp(magic, "SS5", 3) == 0) {
        ScoreEntry tmp[5];
        if (fread(tmp, sizeof(ScoreEntry), 5, f) == 5) memcpy(topScores, tmp, sizeof(topScores));
    } else {
        // legacy single-int save
        fseek(f, 0, SEEK_SET);
        int v = 0;
        if (fread(&v, sizeof(int), 1, f) == 1 && v > 2650) topScores[0].score = v;
    }
    fclose(f);
    highScore = topScores[0].score;
}

static void updateTopScores() {
    for (int i = 0; i < 5; i++) {
        if (game.score > topScores[i].score) {
            for (int j = 4; j > i; j--) topScores[j] = topScores[j - 1];
            topScores[i].score = game.score;
            topScores[i].level = game.level;
            saveHighScore();
            break;
        }
    }
}

static void addScore(int pts) {
    game.score += pts;
    if (game.score > highScore) {
        if (highScore > 0) {
            snprintf(game.msgText, sizeof(game.msgText), "NEW HIGH SCORE!");
            game.msgTimer = FPS * 2;
        }
        highScore = game.score;
        updateTopScores();
    }
}

static const char* playerTitle(int score) {
    if (score >= 200000) return "SPACE LEGEND";
    if (score >= 100000) return "GRAND ADMIRAL";
    if (score >= 50000)  return "ADMIRAL";
    if (score >= 20000)  return "ACE";
    if (score >= 5000)   return "PILOT";
    if (score >= 1000)   return "CADET";
    return "ROOKIE";
}

// --- Fire Player Bullet ---
static void firePlayerBullet() {
    double x = game.ship.x;
    double y = game.ship.y - game.shipH / 2.0;
    game.bullets.push_back({ {x, y}, {0, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });

    if (game.shipPowerLevel >= 2) {
        game.bullets.push_back({ {x - 12, y}, {-0.5, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });
        game.bullets.push_back({ {x + 12, y}, {0.5, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });
    }
    if (game.shipPowerLevel >= 3) {
        game.bullets.push_back({ {x - 6, y}, {-0.3, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });
        game.bullets.push_back({ {x + 6, y}, {0.3, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });
    }

    if (laserSoundCooldown <= 0) {
        shootSfxPending = true;
        laserSoundCooldown = 3;
    }
}

// --- Fire Laser Beam (instant-hit column) ---
static void fireLaser() {
    double bx = game.ship.x;
    int bw = 10;
    for (auto& e : game.enemies) {
        if (e.hp <= 0 || e.pos.y > game.ship.y || fabs(e.pos.x - bx) > bw) continue;
        if (e.shieldN > 0) { e.shieldN--; e.flash = 4; createShieldHit(e.pos); continue; }
        e.hp -= 2;
        e.flash = 4;
        if (e.hp <= 0) killEnemy(e);
    }
    if (game.bossAlive && game.boss.pos.y < game.ship.y &&
        fabs(game.boss.pos.x - bx) < bw + game.boss.size * 0.3) {
        game.boss.hp -= 2;
        game.boss.flash = 3;
        if (game.boss.hp <= 0) killBoss();
    }
    for (auto& r : game.bombards) {
        if (r.pos.y > game.ship.y || fabs(r.pos.x - bx) > bw + r.size) continue;
        r.pos.y = WINDOW_H + 100;
        award(2);
        createExplosion(r.pos, RGB(255, 170, 60), 6, 1.5);
    }
    for (int i = 0; i < 10; i++) {
        addParticle({ bx, game.ship.y - randf() * game.ship.y },
            { (randf() - 0.5) * 0.4, 0.6 }, 0.3, 0.03, 2.0, RGB(255, 140, 40), true);
    }
}

// --- Fire Enemy Bullet ---
static void fireEnemyBullet(Vec2 epos, double targetX, double targetY, double speed) {
    double angle = (PI / 2) + (randf() - 0.5) * PI * 0.26;
    game.enemyBullets.push_back({ {epos.x, epos.y + 15}, {cos(angle) * speed, sin(angle) * speed}, 1.0, RGB(255, 60, 60), true });
}

// --- Fire Boss Bullets ---
static void fireBossBullets() {
    if (!game.bossAlive) return;
    Boss& b = game.boss;
    b.shootTimer++;
    if (b.shootTimer < b.shootRate) return;
    // Telegraph the attack with a warning ring before firing
    if (b.telegraph <= 0) { b.telegraph = 28; return; }
    b.telegraph--;
    if (b.telegraph > 0) return;
    b.shootTimer = 0;

    auto fireSpread = [&](int count, double spreadAngle, double spdMult, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double angle = PI / 2 + (spreadAngle * (i - (count - 1) / 2.0) / (count > 1 ? (count - 1) / 2.0 : 1.0));
            double vx = cos(angle) * b.bulletSpeed * spdMult;
            double vy = fabs(sin(angle) * b.bulletSpeed * spdMult);
            game.enemyBullets.push_back({ b.pos, {vx, vy}, 1.0, col, true });
        }
    };

    auto fireCircle = [&](int count, double spdMult, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double a = (2.0 * PI / count) * i + b.phase * 0.5;
            double vy = fabs(sin(a) * b.bulletSpeed * spdMult);
            game.enemyBullets.push_back({ b.pos, {cos(a) * b.bulletSpeed * spdMult, vy}, 1.0, col, true });
        }
    };

    auto fireSpiral = [&](int count, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double a = b.spiralAngle + (2.0 * PI / count) * i;
            double vy = fabs(sin(a) * b.bulletSpeed);
            game.enemyBullets.push_back({ b.pos, {cos(a) * b.bulletSpeed, vy}, 1.0, col, true });
        }
        b.spiralAngle += 0.3;
    };

    auto fireAimed = [&](int count, double spdMult, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double dx = game.ship.x - b.pos.x + (i - (count - 1) / 2.0) * 40;
            double dy = fabs(game.ship.y - b.pos.y);
            double d = sqrt(dx * dx + dy * dy) + 0.001;
            game.enemyBullets.push_back({ b.pos, {(dx / d) * b.bulletSpeed * spdMult, (dy / d) * b.bulletSpeed * spdMult}, 1.0, col, true });
        }
    };

    auto fireBurst = [&](int count, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double a = (2.0 * PI / count) * i;
            double vy = fabs(sin(a) * b.bulletSpeed * 0.6) + 1.0;
            game.enemyBullets.push_back({ b.pos, {cos(a) * b.bulletSpeed * 0.6, vy}, 1.0, col, true });
        }
    };

    switch (b.pattern) {
    case BOSS_SINE:   fireSpread(7, 2.2, 1.0, b.color); break;
    case BOSS_CIRCLE: fireCircle(16, 0.7, b.color); break;
    case BOSS_SPIRAL: fireSpiral(8, b.color); break;
    case BOSS_AIMED:  fireAimed(3, 1.2, RGB(255, 255, 0)); break;
    case BOSS_BURST:  fireBurst(24, b.color); break;
    case BOSS_ALL:
        fireSpread(7, 0.8 * 6, 1.0, b.color);
        fireCircle(16, 0.7, b.color);
        fireSpiral(8, b.color);
        fireAimed(3, 1.2, RGB(255, 255, 0));
        fireBurst(24, b.color);
        break;
    }
}

// --- Spawn Boss ---
static void spawnBoss(int level) {
    int bossIndex = 0;
    int bossLevels[] = { 5, 10, 20, 30, 40, 50 };
    for (int i = 0; i < 6; i++) { if (bossLevels[i] == level) { bossIndex = i; break; } }
    if (game.endless && level > 50) bossIndex = 5; // endless bosses = final overlord

    struct BossDef { int hp; double speed; int shootRate; double bulletSz; double size; COLORREF color; BossPattern pattern; };
    BossDef defs[6] = {
        {80, 1.5, 40, 4.0, 50, RGB(255,60,60), BOSS_SINE},
        {150, 1.8, 35, 5.0, 55, RGB(255,128,0), BOSS_CIRCLE},
        {250, 2.0, 28, 5.5, 60, RGB(255,0,255), BOSS_SPIRAL},
        {380, 2.2, 22, 6.0, 65, RGB(255,255,0), BOSS_AIMED},
        {500, 2.5, 18, 6.5, 70, RGB(255,0,0), BOSS_BURST},
        {700, 2.8, 14, 7.0, 80, RGB(255,255,255), BOSS_ALL}
    };

    BossDef& d = defs[bossIndex];
    game.boss = {};
    game.boss.pos = { WINDOW_W / 2.0, -80.0 };
    game.boss.hp = d.hp;
    game.boss.maxHp = d.hp;
    game.boss.speed = d.speed;
    game.boss.shootRate = d.shootRate;
    game.boss.bulletSpeed = 5.0;
    game.boss.size = d.size;
    game.boss.color = d.color;
    game.boss.pattern = d.pattern;
    game.boss.targetY = 110.0;
    game.boss.moveDir = 1;
    game.boss.phase = 0;
    game.boss.spiralAngle = 0;
    game.boss.entered = false;
    game.boss.flash = 0;
    game.boss.shootTimer = 0;
    game.bossAlive = true;
    playMci("sfxwarn", 2400);
}

// --- Spawn Enemy ---
static void spawnEnemyAt(LevelConfig& cfg, double x, double y, double phase) {
    Enemy e{};
    e.pos = { x, y };
    e.hp = cfg.enemyHp;
    e.maxHp = cfg.enemyHp;
    e.speed = cfg.enemySpeed;
    e.shootTimer = randf() * cfg.enemyShootRate;
    e.shootRate = cfg.enemyShootRate;
    e.color = cfg.enemyColor;
    e.type = cfg.enemyType;
    e.phase = phase + randf() * 1.5;
    e.flash = 0;
    e.drift = (randf() - 0.5) * 1.6;
    e.swayAmp = 1.2 + randf() * 2.6;

    switch (e.type) {
    case ENEMY_FAST:      e.w = 24; e.h = 24; e.speed *= 1.6; e.hp = (int)ceil(e.hp * 0.7); e.maxHp = e.hp; e.shootRate = (int)(e.shootRate * 1.6); break;
    case ENEMY_SHOOTER:   e.w = 34; e.h = 34; e.speed *= 0.7; e.hp = (int)ceil(e.hp * 1.5); e.maxHp = e.hp; e.shootRate = (int)(e.shootRate * 0.6); break;
    case ENEMY_ELITE:     e.w = 36; e.h = 36; e.speed *= 0.9; e.hp = (int)ceil(e.hp * 2.0); e.maxHp = e.hp; e.shootRate = (int)(e.shootRate * 0.5); break;
    case ENEMY_KAMIKAZE:  e.w = 26; e.h = 26; e.speed *= 2.1; e.hp = (int)ceil(e.hp * 0.8); e.maxHp = e.hp; e.shootRate = 100000; e.drift = 0; break;
    case ENEMY_SPLITTER:  e.w = 34; e.h = 34; e.speed *= 0.8; e.hp = (int)ceil(e.hp * 1.8); e.maxHp = e.hp; e.shootRate = (int)(e.shootRate * 1.4); break;
    default:              e.w = 30; e.h = 30; break;
    }

    // Shield-bearers on higher levels
    if (game.level >= 20 && (e.type == ENEMY_SHOOTER || e.type == ENEMY_ELITE || e.type == ENEMY_SPLITTER)) {
        e.shieldN = game.level >= 35 ? 2 : 1;
    }

    game.enemies.push_back(e);
}

static void spawnEnemy(LevelConfig& cfg) {
    spawnEnemyAt(cfg, (double)randRange(30, WINDOW_W - 30), -30.0, randf() * 2.0 * PI);
}

// --- Spawn Formation (a squad of enemies moving down together, soldiers style) ---
static void spawnFormation(LevelConfig& cfg) {
    int remaining = game.levelEnemiesLeft;
    if (remaining <= 0) return;

    int squad;
    if (game.level < 6) squad = 3 + game.level / 2;
    else if (game.level >= 10) squad = 11;
    else squad = 10;
    if (squad > remaining) squad = remaining;

    int pattern = rand() % 3;
    double centerX = WINDOW_W / 2.0;
    double topY = -55.0;
    double phase = randf() * 2.0 * PI;

    for (int i = 0; i < squad; i++) {
        double dx = 0, dy = 0;
        switch (pattern) {
        case 0: { // V formation
            int k = i / 2;
            int side = (i % 2 == 0) ? -1 : 1;
            dx = side * (22.0 + k * 44.0);
            dy = k * 32.0;
            break;
        }
        case 1: { // staggered line
            dx = (i - (squad - 1) / 2.0) * 50.0;
            dy = (i % 2) * 30.0;
            break;
        }
        default: { // twin columns
            dx = (i % 2 == 0) ? -40.0 : 40.0;
            dy = (i / 2) * 34.0;
            break;
        }
        }
        spawnEnemyAt(cfg, centerX + dx, topY + dy, phase);
    }
    game.levelEnemiesLeft -= squad;
}

// --- Hit Player ---
static void hitPlayer() {
    if (game.shipInvincible > 0) return;
    if (game.cheatGod) return;
    game.comboCount = 0;
    game.comboTimer = 0;
    if (game.shieldCount > 0) {
        if (!game.cheatInfShield) game.shieldCount--;
        game.shipInvincible = 40;
        createShieldHit(game.ship);
        createExplosion(game.ship, RGB(0, 255, 180), 8, 2.0);
        screenShakeAmt(6.0);
        return;
    }
    if (game.cheatInfLives) { game.shipInvincible = 40; return; }
    game.lives--;
    game.shipInvincible = 120;
    createExplosion(game.ship, RGB(0, 255, 255), 20, 3.0);
    createShieldHit(game.ship);
    screenShakeAmt(12.0);
    if (game.lives <= 0) { updateTopScores(); game.state = STATE_GAMEOVER; }
}

// --- Next Level ---
static void nextLevel() {
    game.level++;
    if (game.level > 50 && !game.endless) { game.state = STATE_WIN; game.winScroll = 0; return; }

    game.enemies.clear();
    game.enemyBullets.clear();
    game.bullets.clear();
    game.particles.clear();
    game.powerups.clear();
    game.consumables.clear();
    game.bombards.clear();
    game.gems.clear();
    game.bossAlive = false;
    game.bossActive = false;
    game.levelTimer = 0;
    game.spawnTimer = 0;
    game.bombardActive = false;
    game.bombardPending = 0;
    game.bombardLeft = 0;

    LevelConfig cfg = getLevelConfig(game.level);
    game.levelEnemiesLeft = cfg.enemyCount;

    game.shipPowerLevel = min(game.shipPowerLevel + 1, 3);

    if (cfg.isBoss) {
        game.bossActive = true;
        spawnBoss(game.level);
        char tmp[64]; snprintf(tmp, sizeof(tmp), "BOSS LEVEL %d", game.level);
        bannerSet(tmp);
    } else {
        char tmp[64]; snprintf(tmp, sizeof(tmp), "LEVEL %d", game.level);
        bannerSet(tmp);
        if (isBombardLevel()) game.bombardPending = 90;
    }
}

// --- Continue (one checkpoint per run) ---
static void continueGame() {
    if (game.continues <= 0) return;
    game.continues--;
    game.state = STATE_PLAYING;
    game.lives = 3;
    game.enemies.clear();
    game.enemyBullets.clear();
    game.powerups.clear();
    game.consumables.clear();
    game.bombards.clear();
    game.gems.clear();
    game.bombardActive = false;
    game.bombardPending = 0;
    game.shipInvincible = 120;
    game.levelTimer = 0;
    game.spawnTimer = 0;
    game.bossAlive = false;
    game.bossActive = false;
    game.ship = { WINDOW_W / 2.0, WINDOW_H - 80.0 };
    game.shipPowerLevel = max(game.shipPowerLevel, 1);
    LevelConfig cfg = getLevelConfig(game.level);
    game.levelEnemiesLeft = cfg.enemyCount;
    if (cfg.isBoss) { game.bossActive = true; spawnBoss(game.level); }
    bannerSet("CONTINUE!");
}

// --- Reset Game ---
static void resetGame() {
    game.state = STATE_PLAYING;
    game.score = 0;
    game.level = 1;
    game.lives = 3;
    game.screenShake = 0;
    game.bullets.clear();
    game.enemyBullets.clear();
    game.enemies.clear();
    game.particles.clear();
    game.powerups.clear();
    game.consumables.clear();
    game.bombards.clear();
    game.gems.clear();
    game.bullets.reserve(256);
    game.enemyBullets.reserve(256);
    game.enemies.reserve(40);
    game.particles.reserve(1024);
    game.powerups.reserve(32);
    game.consumables.reserve(64);
    game.bossAlive = false;
    game.bossActive = false;
    game.levelTimer = 0;
    game.spawnTimer = 0;
    game.shieldCount = 0;
    game.rapidTimer = 0;
    game.abilityPulse = 0;
    game.msgTimer = 0;
    game.msgText[0] = '\0';
    game.grenades = 0;
    game.repairKits = 0;
    game.freezeCharges = 0;
    game.comboCount = 0;
    game.comboTimer = 0;
    game.slowmoTimer = 0;
    game.laserTimer = 0; game.laserCooldown = 0;
    game.freezeTimer = 0;
    game.homingTimer = 0;
    game.bombardActive = false;
    game.bombardPending = 0;
    game.bombardTimer = 0;
    game.bombardLeft = 0;
    game.paused = false;
    game.killArmed = false;
    game.continues = 1;
    game.bannerTimer = 0;
    game.bannerText[0] = 0;
    memset(game.keysPrev, 0, sizeof(game.keysPrev));

    game.ship = { WINDOW_W / 2.0, WINDOW_H - 80.0 };
    game.shipW = 40; game.shipH = 40;
    game.shipShootCooldown = 0; game.shipShootRate = 22;
    game.shipInvincible = 0; game.shipPowerLevel = 1;
    game.shipThrustPhase = 0;

    LevelConfig cfg = getLevelConfig(1);
    game.levelEnemiesLeft = cfg.enemyCount;
}

// --- Init Stars ---
static void initStars() {
    game.stars.clear();
    for (int i = 0; i < 100; i++) {
        game.stars.push_back({
            randf() * WINDOW_W,
            randf() * WINDOW_H,
            randf() * 3.0 + 0.5,
            randf() * 2.0 + 0.5,
            randf()
        });
    }
}

// --- Update ---
static void update() {
    // Hidden console toggle with F1
    bool fanNow = game.keys[VK_F1];
    if (fanNow && !game.keysPrev[VK_F1]) {
        if (console.state == CONS_CLOSED) {
            console.state = CONS_PASSWORD;
            console.inputLen = 0;
            console.input[0] = 0;
        } else {
            console.state = CONS_CLOSED;
        }
    }
    game.keysPrev[VK_F1] = fanNow;

    // P-kill: only works after typing KILLALL in console (arms the key)
    if (game.keys['P'] && !game.keysPrev['P'] && game.killArmed) {
        killAllEnemies();
        memcpy(game.keysPrev, game.keys, sizeof(game.keys));
    }
    if (console.state != CONS_CLOSED) return;

    if (game.state == STATE_WIN) {
        game.winScroll += 0.9;
    }
    if (game.state != STATE_PLAYING) return;
    if (game.paused) return;

    game.shipThrustPhase += 0.15;

    // Movement
    double spd = 5.0 * game.cheatSpeed;
    if (game.keys['A'] || game.keys[VK_LEFT])  game.ship.x -= spd;
    if (game.keys['D'] || game.keys[VK_RIGHT]) game.ship.x += spd;
    if (game.keys['W'] || game.keys[VK_UP])    game.ship.y -= spd;
    if (game.keys['S'] || game.keys[VK_DOWN])  game.ship.y += spd;
    game.ship.x = max(game.shipW / 2.0, min((double)(WINDOW_W - game.shipW / 2.0), game.ship.x));
    game.ship.y = max(game.shipH / 2.0, min((double)(WINDOW_H - game.shipH / 2.0), game.ship.y));

    // Shooting
    int shootRate = game.shipShootRate - (game.rapidTimer > 0 ? 6 : 0);
    shootRate = max(3, shootRate);
    if (game.keys[VK_SPACE] || game.keys['Z']) {
        if (game.shipShootCooldown <= 0) {
            if (game.laserTimer > 0) {
                if (game.laserCooldown <= 0) {
                    fireLaser();
                    game.laserCooldown = 9;
                    game.shipShootCooldown = 9;
                }
            } else {
                firePlayerBullet();
                game.shipShootCooldown = shootRate;
            }
        }
    }
    if (game.shipShootCooldown > 0) game.shipShootCooldown--;
    if (game.shipInvincible > 0) game.shipInvincible--;
    if (game.rapidTimer > 0) game.rapidTimer--;
    if (game.laserTimer > 0) game.laserTimer--;
    if (game.laserCooldown > 0) game.laserCooldown--;
    if (game.freezeTimer > 0) game.freezeTimer--;
    if (game.homingTimer > 0) game.homingTimer--;
    if (game.slowmoTimer > 0) game.slowmoTimer--;
    if (game.comboTimer > 0) { game.comboTimer--; if (game.comboTimer == 0) game.comboCount = 0; }
    if (game.abilityPulse < 1.0) game.abilityPulse += 0.02;

    // Consumable hotkeys (edge-triggered)
    if (game.keys['G'] && !game.keysPrev['G']) useGrenade();
    if (game.keys['H'] && !game.keysPrev['H']) useRepairKit();
    if (game.keys['J'] && !game.keysPrev['J']) useTimeFreeze();
    memcpy(game.keysPrev, game.keys, sizeof(game.keys));

    // Update bullets
    for (auto& b : game.bullets) {
        if (b.homing) {
            double best = 1e9;
            Vec2 tgt;
            bool found = false;
            if (game.bossAlive && game.boss.entered) {
                double d = dist(b.pos, game.boss.pos);
                if (d < best) { best = d; tgt = game.boss.pos; found = true; }
            }
            for (auto& e : game.enemies) {
                if (e.hp <= 0) continue;
                double d = dist(b.pos, e.pos);
                if (d < best) { best = d; tgt = e.pos; found = true; }
            }
            if (found) {
                Vec2 dir = (tgt - b.pos).norm();
                b.vel = dir * 10.0;
            }
        }
        b.pos = b.pos + b.vel;
    }
    game.bullets.erase(
        std::remove_if(game.bullets.begin(), game.bullets.end(),
            [](const Bullet& b) { return b.pos.y < -20 || b.pos.y > WINDOW_H + 20 || b.pos.x < -20 || b.pos.x > WINDOW_W + 20; }),
        game.bullets.end());

    double spdMul = (game.cheatSlow ? 0.4 : 1.0) * (game.slowmoTimer > 0 ? 0.3 : 1.0) * (game.freezeTimer > 0 ? 0.0 : 1.0);

    for (auto& b : game.enemyBullets) {
        if (b.vel.y < 0) b.vel.y = -b.vel.y;
        b.pos.x += b.vel.x * spdMul;
        b.pos.y += b.vel.y * spdMul;
    }
    game.enemyBullets.erase(
        std::remove_if(game.enemyBullets.begin(), game.enemyBullets.end(),
            [](const Bullet& b) { return b.pos.y > WINDOW_H + 20 || b.pos.y < -20 || b.pos.x < -20 || b.pos.x > WINDOW_W + 20; }),
        game.enemyBullets.end());

    // Spawn enemies
    LevelConfig cfg = getLevelConfig(game.level);
    if (!cfg.isBoss && game.levelEnemiesLeft > 0) {
        game.spawnTimer++;
        if (game.spawnTimer >= cfg.spawnRate) {
            game.spawnTimer = 0;
            spawnFormation(cfg);
        }
    }

    // Update enemies
    for (auto& e : game.enemies) {
        e.phase += 0.03;
        if (game.freezeTimer > 0) {
            if (e.flash > 0) e.flash--;
            continue;
        }
        switch (e.type) {
        case ENEMY_FAST:
            e.pos.y += e.speed * spdMul;
            e.pos.x += sin(e.phase) * e.swayAmp + e.drift * 2.0;
            break;
        case ENEMY_SHOOTER:
            e.pos.y += e.speed * 0.5 * spdMul;
            e.pos.x += sin(e.phase * 0.8) * e.swayAmp + e.drift;
            break;
        case ENEMY_ELITE: {
            double targetY = 100.0 + sin(e.phase * 0.5) * 50.0;
            if (e.pos.y < targetY) e.pos.y += e.speed * spdMul;
            e.pos.x += sin(e.phase) * e.swayAmp + e.drift;
            break;
        }
        case ENEMY_KAMIKAZE: {
            // dives straight at the player
            e.pos.y += e.speed * 1.6 * spdMul;
            double dx = game.ship.x - e.pos.x;
            e.pos.x += (dx > 0 ? 1.0 : -1.0) * min(fabs(dx) * 0.12, 2.6) * spdMul;
            e.pos.x += sin(e.phase * 2.0) * 1.5;
            break;
        }
        case ENEMY_SPLITTER:
            e.pos.y += e.speed * 0.8 * spdMul;
            e.pos.x += sin(e.phase * 0.7) * (e.swayAmp + 1.5) + e.drift;
            break;
        default:
            e.pos.y += e.speed * spdMul;
            e.pos.x += sin(e.phase) * e.swayAmp + e.drift;
            break;
        }

        e.shootTimer++;
        if (e.shootTimer >= e.shootRate && e.pos.y > 0 && e.pos.y < WINDOW_H - 100) {
            e.shootTimer = 0;
            double aimSpd = min(1.4 + game.level * 0.02, 2.0);
            fireEnemyBullet(e.pos, game.ship.x, game.ship.y, aimSpd);
        }

        if (e.flash > 0) e.flash--;

        // Missed the ship - bring it back from the top
        if (e.pos.y > WINDOW_H + 50) {
            e.pos.y = -30;
            e.pos.x = (double)randRange(30, WINDOW_W - 30);
            e.phase = randf() * 2.0 * PI;
            e.shootTimer = randf() * e.shootRate * 0.3;
            e.flash = 0;
        }
    }

    // Update boss
    if (game.bossAlive) {
        Boss& b = game.boss;
        if (game.freezeTimer > 0) {
            if (b.flash > 0) b.flash--;
        } else {
            if (!b.entered) {
                b.pos.y += 1.5 * spdMul;
                if (b.pos.y >= b.targetY) { b.pos.y = b.targetY; b.entered = true; }
            } else {
                b.pos.x += b.moveDir * b.speed * spdMul;
                if (b.pos.x > WINDOW_W - b.size || b.pos.x < b.size) b.moveDir *= -1;
                b.phase += 0.02;
            }
            fireBossBullets();
            if (b.flash > 0) b.flash--;
        }
    }

    // Bullet-Enemy collision
    for (auto& b : game.bullets) {
        for (auto& e : game.enemies) {
            if (e.hp > 0 && fabs(b.pos.x - e.pos.x) < (e.w / 2 + 4) &&
                fabs(b.pos.y - e.pos.y) < (e.h / 2 + 8)) {
                if (e.shieldN > 0) {
                    e.shieldN--;
                    e.flash = 4;
                    b.pos.y = -100;
                    createShieldHit(e.pos);
                    continue;
                }
                e.hp -= (int)b.dmg;
                e.flash = 4;
                b.pos.y = -100;
                createExplosion({ b.pos.x, b.pos.y + 10 }, RGB(0, 255, 255), 3, 1.0);
                if (e.hp <= 0) killEnemy(e);
            }
        }

        // Bullet-Boss collision
        if (game.bossAlive) {
            Boss& b2 = game.boss;
            if (fabs(b.pos.x - b2.pos.x) < (b2.size + 4) && fabs(b.pos.y - b2.pos.y) < (b2.size * 0.75 + 8)) {
                b2.hp -= (int)b.dmg;
                b2.flash = 3;
                b.pos.y = -100;
                createExplosion({ b.pos.x, b.pos.y + 10 }, RGB(0, 255, 255), 3, 1.0);
                if (b2.hp <= 0) killBoss();
            }
        }
    }

    // Enemy bullet-ship collision
    for (auto& b : game.enemyBullets) {
        if (fabs(b.pos.x - game.ship.x) < (game.shipW / 2 + 3) && fabs(b.pos.y - game.ship.y) < (game.shipH / 2 + 3)) {
            hitPlayer();
            b.pos.y = WINDOW_H + 100;
        }
    }

    // Enemy-ship collision
    for (auto& e : game.enemies) {
        if (e.hp > 0 && fabs(e.pos.x - game.ship.x) < (e.w / 2 + game.shipW / 2) &&
            fabs(e.pos.y - game.ship.y) < (e.h / 2 + game.shipH / 2)) {
            hitPlayer();
            e.hp -= 2;
            if (e.hp <= 0) killEnemy(e);
        }
    }

    // Remove dead enemies
    game.enemies.erase(
        std::remove_if(game.enemies.begin(), game.enemies.end(), [](const Enemy& e) { return e.hp <= 0; }),
        game.enemies.end());

    // Flush splitter children
    for (auto& s : pendingSplits) {
        Enemy c{};
        c.pos = { s.x, s.y };
        c.type = ENEMY_FAST;
        c.child = true;
        c.hp = s.hp; c.maxHp = s.hp;
        c.speed = s.spd;
        c.w = 14; c.h = 14;
        c.color = s.col;
        c.shootRate = 99999;
        c.phase = randf() * 2.0 * PI;
        c.drift = (randf() - 0.5) * 1.2;
        c.swayAmp = 1.0;
        game.enemies.push_back(c);
    }
    pendingSplits.clear();

    // Update power-ups
    for (auto& pu : game.powerups) {
        pu.pos.y += 2.2;
        pu.phase += 0.06;
    }
    game.powerups.erase(
        std::remove_if(game.powerups.begin(), game.powerups.end(),
            [](const PowerUp& pu) { return pu.pos.y > WINDOW_H + 40; }),
        game.powerups.end());

    for (auto& pu : game.powerups) {
        if (fabs(pu.pos.x - game.ship.x) < 32 && fabs(pu.pos.y - game.ship.y) < 32) {
            applyPowerUp(pu.type);
            pu.pos.y = WINDOW_H + 100;
        }
    }
    game.powerups.erase(
        std::remove_if(game.powerups.begin(), game.powerups.end(),
            [](const PowerUp& pu) { return pu.pos.y > WINDOW_H + 50; }),
        game.powerups.end());

    // Update consumable drops
    for (auto& c : game.consumables) {
        c.pos.y += 2.2;
        c.phase += 0.06;
    }
    game.consumables.erase(
        std::remove_if(game.consumables.begin(), game.consumables.end(),
            [](const ConsumableDrop& c) { return c.pos.y > WINDOW_H + 40; }),
        game.consumables.end());

    for (auto& c : game.consumables) {
        if (fabs(c.pos.x - game.ship.x) < 30 && fabs(c.pos.y - game.ship.y) < 30) {
            collectConsumable(c.type);
            c.pos.y = WINDOW_H + 100;
        }
    }
    game.consumables.erase(
        std::remove_if(game.consumables.begin(), game.consumables.end(),
            [](const ConsumableDrop& c) { return c.pos.y > WINDOW_H + 50; }),
        game.consumables.end());

    // Bombardment events
    if (game.bombardPending > 0) {
        game.bombardPending--;
        if (game.bombardPending == 0) {
            game.bombardActive = true;
            game.bombardTimer = 0;
            game.bombardLeft = 26;
            playMci("sfxwarn", 2400);
            bannerSet("ASTEROID STORM!");
        }
    }
    if (game.bombardActive && game.freezeTimer <= 0) {
        if (game.bombardLeft > 0) {
            game.bombardTimer++;
            if (game.bombardTimer >= 5) {
                game.bombardTimer = 0;
                game.bombardLeft--;
                double sz = 10 + randf() * 16;
                game.bombards.push_back({
                    { (double)randRange(24, WINDOW_W - 24), -20.0 },
                    { (randf() - 0.5) * 1.4, 2.6 + randf() * 3.4 },
                    sz, randf() * 6.28, (randf() - 0.5) * 0.3,
                    randf() < 0.3 ? RGB(150, 120, 100) : RGB(95, 88, 84)
                });
            }
        }
    }
    for (auto& r : game.bombards) {
        if (game.freezeTimer <= 0) {
            r.pos = r.pos + r.vel;
            r.rot += r.rotSpd;
        }
    }
    game.bombards.erase(
        std::remove_if(game.bombards.begin(), game.bombards.end(),
            [](const Bombard& r) { return r.pos.y > WINDOW_H + 60; }),
        game.bombards.end());

    // Bullets destroy rocks
    for (auto& b : game.bullets) {
        for (auto& r : game.bombards) {
            if (dist(b.pos, r.pos) < r.size + 4) {
                b.pos.y = -100;
                r.pos.y = WINDOW_H + 100;
                award(5);
                createExplosion(r.pos, RGB(255, 170, 60), 10, 2.0);
                screenShakeAmt(3.0);
            }
        }
    }
    game.bombards.erase(
        std::remove_if(game.bombards.begin(), game.bombards.end(),
            [](const Bombard& r) { return r.pos.y > WINDOW_H + 60; }),
        game.bombards.end());

    if (game.bombardActive && game.bombardLeft <= 0 && game.bombards.empty()) game.bombardActive = false;

    // Rocks hit the ship
    for (auto& r : game.bombards) {
        if (dist(r.pos, game.ship) < r.size * 1.2 + game.shipW / 2) {
            hitPlayer();
            createExplosion(r.pos, RGB(200, 120, 60), 8, 2.0);
            r.pos.y = WINDOW_H + 100;
        }
    }
    game.bombards.erase(
        std::remove_if(game.bombards.begin(), game.bombards.end(),
            [](const Bombard& r) { return r.pos.y > WINDOW_H + 60; }),
        game.bombards.end());

    // Score gems
    for (auto& g : game.gems) { g.pos.y += 1.6; g.phase += 0.05; }
    game.gems.erase(
        std::remove_if(game.gems.begin(), game.gems.end(),
            [](const Gem& g) { return g.pos.y > WINDOW_H + 30; }),
        game.gems.end());
    for (auto& g : game.gems) {
        if (fabs(g.pos.x - game.ship.x) < 26 && fabs(g.pos.y - game.ship.y) < 28) {
            award(g.value);
            playSound(SND_POWERUP);
            for (int i = 0; i < 8; i++) {
                double angle = randf() * 2.0 * PI;
                addParticle(g.pos, { cos(angle) * 2.0, sin(angle) * 2.0 }, 0.6, 0.04, 2.5, RGB(255, 220, 40), true);
            }
            g.pos.y = WINDOW_H + 100;
        }
    }
    game.gems.erase(
        std::remove_if(game.gems.begin(), game.gems.end(),
            [](const Gem& g) { return g.pos.y > WINDOW_H + 30; }),
        game.gems.end());

    // Update particles
    for (auto& p : game.particles) {
        p.pos = p.pos + p.vel;
        p.life -= p.decay;
        if (p.isSpark) { /* no gravity */ }
        else { p.vel.x *= 0.98; p.vel.y *= 0.98; }
    }
    game.particles.erase(
        std::remove_if(game.particles.begin(), game.particles.end(), [](const Particle& p) { return p.life <= 0; }),
        game.particles.end());

    // Update stars
    for (auto& s : game.stars) {
        s.y += s.speed;
        if (s.y > WINDOW_H) { s.y = 0; s.x = randf() * WINDOW_W; }
    }

    // Screen shake decay
    if (game.screenShake > 0) game.screenShake *= 0.85;

    // Level completion check
    if (!cfg.isBoss && !game.bossActive && game.levelEnemiesLeft <= 0 && game.enemies.empty()) {
        game.levelTimer++;
        if (game.levelTimer > 60) nextLevel();
    }
    // After boss killed - wait until reward drops are collected or gone
    if (!game.bossActive && !game.bossAlive && cfg.isBoss && game.powerups.empty()) {
        game.levelTimer++;
        if (game.levelTimer > 60) nextLevel();
    }

    // Ability message timer
    if (game.msgTimer > 0) game.msgTimer--;
    if (game.bannerTimer > 0) game.bannerTimer--;

    // Sound cooldowns
    if (laserSoundCooldown > 0) laserSoundCooldown--;
    if (prioritySoundTimer > 0) prioritySoundTimer--;
}

// --- Draw Helpers ---
static std::map<DWORD, HBRUSH> brushCache;
static std::map<unsigned long long, HPEN> penCache;
static HBRUSH getBrush(COLORREF c) {
    auto it = brushCache.find((DWORD)c);
    if (it != brushCache.end()) return it->second;
    HBRUSH b = CreateSolidBrush(c);
    brushCache[(DWORD)c] = b;
    return b;
}
static HPEN getPen(COLORREF c, int width) {
    unsigned long long key = ((unsigned long long)(unsigned short)width << 32) | (DWORD)c;
    auto it = penCache.find(key);
    if (it != penCache.end()) return it->second;
    HPEN p = CreatePen(PS_SOLID, width, c);
    penCache[key] = p;
    return p;
}

static void drawPixel(HDC hdc, int x, int y, COLORREF c) {
    SetPixel(hdc, x, y, c);
}

static void fillRect(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    RECT r = { x, y, x + w, y + h };
    FillRect(hdc, &r, getBrush(c));
}

static void drawCircle(HDC hdc, double cx, double cy, double r, COLORREF c) {
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(c));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(c, 1));
    Ellipse(hdc, (int)(cx - r), (int)(cy - r), (int)(cx + r), (int)(cy + r));
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

static void drawTriangle(HDC hdc, Vec2 p1, Vec2 p2, Vec2 p3, COLORREF fill, COLORREF stroke) {
    POINT pts[3] = { {(int)p1.x, (int)p1.y}, {(int)p2.x, (int)p2.y}, {(int)p3.x, (int)p3.y} };
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(fill));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(stroke, 1));
    Polygon(hdc, pts, 3);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

static void drawEllipse(HDC hdc, double cx, double cy, double rx, double ry, COLORREF fill) {
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(fill));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(fill, 1));
    Ellipse(hdc, (int)(cx - rx), (int)(cy - ry), (int)(cx + rx), (int)(cy + ry));
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

static void drawLine(HDC hdc, double x1, double y1, double x2, double y2, COLORREF c, int width = 1) {
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(c, width));
    MoveToEx(hdc, (int)x1, (int)y1, NULL);
    LineTo(hdc, (int)x2, (int)y2);
    SelectObject(hdc, oldPen);
}

static void setTextColor(HDC hdc, COLORREF c) {
    SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
}

static std::map<int, HFONT> fontCache;
static HFONT getFont(int size) {
    auto it = fontCache.find(size);
    if (it != fontCache.end()) return it->second;
    HFONT font = CreateFontA(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "BM space");
    fontCache[size] = font;
    return font;
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

// Unicoded text (emoji-friendly), centered on cx
static void drawCenteredW(HDC hdc, const wchar_t* text, double cx, double y, int size, COLORREF c, const wchar_t* face) {
    HFONT font = CreateFontW(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    setTextColor(hdc, c);
    SIZE sz;
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, (int)(cx - sz.cx / 2.0), (int)y, text, (int)wcslen(text));
    SelectObject(hdc, oldFont);
    DeleteObject(font);
}

// --- Draw Ship ---
static void drawShip(HDC hdc) {
    if (game.shipInvincible > 0 && (game.shipInvincible / 4) % 2 == 0) return;

    double x = game.ship.x;
    double y = game.ship.y;
    double hw = game.shipW / 2;
    double hh = game.shipH / 2;

    // Ship evolves color as you level up
    int lv = game.level;
    double hue = 185 - (lv - 1) * 6.0;   // cyan -> blue -> violet
    if (hue < 0) hue += 360;
    COLORREF mainCol = hslToColor((int)hue, 82, 62);
    COLORREF brightCol = hslToColor(((int)hue + 40) % 360, 95, 72);
    COLORREF darkCol = hslToColor((int)hue, 55, 32);
    COLORREF accent = RGB(255, 220, 60);
    if (game.cheatRainbow) {
        int hueNow = ((int)(GetTickCount() / 50)) % 360;
        mainCol = hslToColor(hueNow, 85, 60);
        brightCol = hslToColor((hueNow + 60) % 360, 95, 72);
        darkCol = hslToColor((hueNow + 120) % 360, 55, 30);
    }
    double lvDrift = lv * 0.02;   // tiny pulsing for higher levels
    double pulse = sin(game.abilityPulse * PI * 2) * (lv >= 20 ? 3.0 : 1.5);

    // Shield bubble (behind ship)
    if (game.shieldCount > 0) {
        double r = 32.0 + sin(game.abilityPulse * PI * 2) * 2.0;
        drawCircle(hdc, x, y, r, RGB(0, 255, 180));
        drawCircle(hdc, x, y, r - 4, RGB(0, 160, 255));
        drawCircle(hdc, x, y, r - 8, RGB(0, 80, 160));
    }

    // Layered engine flames (flickering)
    double flick = sin(game.shipThrustPhase) * 4.0 + 2.0;
    drawEllipse(hdc, x, y + hh + 4, 7, 14 + flick, RGB(0, 110, 255));
    drawEllipse(hdc, x, y + hh + 4, 4.5, 10 + flick, RGB(0, 200, 255));
    drawEllipse(hdc, x, y + hh + 4, 2.2, 6 + flick, RGB(240, 255, 255));
    if (lv >= 15) drawEllipse(hdc, x, y + hh + 4, 5.5, 12 + flick, RGB(255, 255, 255));

    auto wingLight = [&](double wx, double wy) {
        drawEllipse(hdc, wx, wy, 3, 3, accent);
        drawEllipse(hdc, wx, wy, 1.5, 1.5, RGB(255, 255, 255));
    };
    auto cockpit = [&](double cy, double rx, double ry) {
        drawEllipse(hdc, x, cy, rx + 2, ry + 2, mainCol);
        drawEllipse(hdc, x, cy, rx, ry, brightCol);
        drawEllipse(hdc, x, cy, rx * 0.45, ry * 0.45, RGB(255, 255, 255));
    };

    // Ship hull changes shape as you level up
    int tier = lv <= 5 ? 5 : lv <= 10 ? 10 : lv <= 20 ? 20 : lv <= 30 ? 30 : lv <= 40 ? 40 : 50;

    switch (tier) {
    case 5: {
        // DART STARFIGHTER - swept wings, metal nose
        drawTriangle(hdc, { x, y - hh * 0.25 }, { x - hw, y + hh * 0.8 }, { x - hw * 0.5, y + hh * 0.95 }, darkCol, mainCol);
        drawTriangle(hdc, { x, y - hh * 0.25 }, { x + hw, y + hh * 0.8 }, { x + hw * 0.5, y + hh * 0.95 }, darkCol, mainCol);
        drawTriangle(hdc, { x, y - hh }, { x - hw * 0.55, y + hh * 0.7 }, { x, y + hh * 0.15 }, darkCol, mainCol);
        drawTriangle(hdc, { x, y - hh }, { x + hw * 0.55, y + hh * 0.7 }, { x, y + hh * 0.15 }, darkCol, mainCol);
        drawTriangle(hdc, { x, y - hh }, { x - hw * 0.28, y + hh * 0.1 }, { x + hw * 0.28, y + hh * 0.1 }, mainCol, brightCol);
        drawLine(hdc, x, y - hh + 2, x, y + hh * 0.7, brightCol, 1);
        wingLight(x - hw * 0.88, y + hh * 0.86);
        wingLight(x + hw * 0.88, y + hh * 0.86);
        cockpit(y - 2, 3.5, 5.5);
        break;
    }
    case 10: {
        // TWIN-BOOM INTERCEPTOR - center pod, twin side engines, needle nose
        drawTriangle(hdc, { x, y - hh * 1.15 }, { x - hw * 0.14, y - hh * 0.45 }, { x + hw * 0.14, y - hh * 0.45 }, brightCol, brightCol);
        drawEllipse(hdc, x, y, hw * 0.3, hh * 0.68, darkCol);
        drawEllipse(hdc, x - hw * 0.7, y + hh * 0.15, hw * 0.22, hh * 0.52, darkCol);
        drawEllipse(hdc, x + hw * 0.7, y + hh * 0.15, hw * 0.22, hh * 0.52, darkCol);
        drawTriangle(hdc, { x - hw * 0.45, y - hh * 0.35 }, { x - hw * 0.95, y + hh * 0.35 }, { x - hw * 0.4, y + hh * 0.15 }, mainCol, mainCol);
        drawTriangle(hdc, { x + hw * 0.45, y - hh * 0.35 }, { x + hw * 0.95, y + hh * 0.35 }, { x + hw * 0.4, y + hh * 0.15 }, mainCol, mainCol);
        drawEllipse(hdc, x - hw * 0.7, y + hh * 0.55, hw * 0.14, hh * 0.2, brightCol);
        drawEllipse(hdc, x + hw * 0.7, y + hh * 0.55, hw * 0.14, hh * 0.2, brightCol);
        wingLight(x - hw * 0.9, y + hh * 0.38);
        wingLight(x + hw * 0.9, y + hh * 0.38);
        cockpit(y - hh * 0.15, 3, 4);
        break;
    }
    case 20: {
        // HEAVY GUNSHIP - broad armored hull, side gun pods
        drawTriangle(hdc, { x, y - hh }, { x - hw, y - hh * 0.15 }, { x - hw * 0.65, y + hh * 0.7 }, darkCol, mainCol);
        drawTriangle(hdc, { x, y - hh }, { x + hw, y - hh * 0.15 }, { x + hw * 0.65, y + hh * 0.7 }, darkCol, mainCol);
        drawTriangle(hdc, { x - hw * 0.65, y + hh * 0.7 }, { x - hw, y - hh * 0.15 }, { x - hw, y + hh * 0.75 }, darkCol, brightCol);
        drawTriangle(hdc, { x + hw * 0.65, y + hh * 0.7 }, { x + hw, y - hh * 0.15 }, { x + hw, y + hh * 0.75 }, darkCol, brightCol);
        drawEllipse(hdc, x - hw * 0.8, y + hh * 0.1, hw * 0.16, hh * 0.45, RGB(35, 40, 48));
        drawEllipse(hdc, x + hw * 0.8, y + hh * 0.1, hw * 0.16, hh * 0.45, RGB(35, 40, 48));
        drawEllipse(hdc, x - hw * 0.8, y + hh * 0.45, hw * 0.09, hh * 0.22, accent);
        drawEllipse(hdc, x + hw * 0.8, y + hh * 0.45, hw * 0.09, hh * 0.22, accent);
        wingLight(x - hw * 0.85, y + hh * 0.3);
        wingLight(x + hw * 0.85, y + hh * 0.3);
        cockpit(y - hh * 0.15, 4, 5);
        break;
    }
    case 30: {
        // NEEDLE SPEAR - razor-thin hull, wide delta wings, big engine
        drawTriangle(hdc, { x, y - hh * 1.3 }, { x - hw * 0.3, y + hh * 0.45 }, { x, y + hh * 0.5 }, mainCol, brightCol);
        drawTriangle(hdc, { x, y - hh * 1.3 }, { x + hw * 0.3, y + hh * 0.45 }, { x, y + hh * 0.5 }, mainCol, brightCol);
        drawTriangle(hdc, { x, y - hh * 0.1 }, { x - hw * 1.05, y + hh * 0.55 }, { x - hw * 0.6, y + hh * 0.85 }, darkCol, mainCol);
        drawTriangle(hdc, { x, y - hh * 0.1 }, { x + hw * 1.05, y + hh * 0.55 }, { x + hw * 0.6, y + hh * 0.85 }, darkCol, mainCol);
        drawTriangle(hdc, { x - hw * 0.3, y + hh * 0.45 }, { x - hw * 0.75, y + hh * 1.05 }, { x - hw * 0.9, y + hh * 1.0 }, darkCol, brightCol);
        drawTriangle(hdc, { x + hw * 0.3, y + hh * 0.45 }, { x + hw * 0.75, y + hh * 1.05 }, { x + hw * 0.9, y + hh * 1.0 }, darkCol, brightCol);
        wingLight(x - hw * 0.95, y + hh * 0.65);
        wingLight(x + hw * 0.95, y + hh * 0.65);
        drawEllipse(hdc, x, y - hh * 0.25, 2.5, 6, RGB(255, 255, 255));
        break;
    }
    case 40: {
        // PHANTOM V-WING - stealth dart, glowing sweep edges
        drawTriangle(hdc, { x, y - hh * 0.5 }, { x - hw, y + hh * 0.65 }, { x, y + hh * 0.95 }, darkCol, mainCol);
        drawTriangle(hdc, { x, y - hh * 0.5 }, { x + hw, y + hh * 0.65 }, { x, y + hh * 0.95 }, darkCol, mainCol);
        drawTriangle(hdc, { x, y - hh }, { x - hw * 0.24, y + hh * 0.6 }, { x + hw * 0.24, y + hh * 0.6 }, mainCol, brightCol);
        drawLine(hdc, x - hw * 0.55, y - hh * 0.2, x - hw * 0.95, y + hh * 0.62, brightCol, 2);
        drawLine(hdc, x + hw * 0.55, y - hh * 0.2, x + hw * 0.95, y + hh * 0.62, brightCol, 2);
        wingLight(x - hw * 0.9, y + hh * 0.7);
        wingLight(x + hw * 0.9, y + hh * 0.7);
        cockpit(y + hh * 0.05, 3, 7);
        break;
    }
    default: {
        // APEX VALKYRIE - crowned delta, energy ring, twin split engines, aura
        drawCircle(hdc, x, y, hh * 0.95 + pulse * 0.4, accent);
        drawTriangle(hdc, { x - hw * 0.55, y - hh * 0.6 }, { x - hw * 0.7, y - hh * 1.0 }, { x - hw * 0.15, y - hh * 0.6 }, darkCol, brightCol);
        drawTriangle(hdc, { x + hw * 0.55, y - hh * 0.6 }, { x + hw * 0.7, y - hh * 1.0 }, { x + hw * 0.15, y - hh * 0.6 }, darkCol, brightCol);
        drawTriangle(hdc, { x - hw * 0.55, y - hh * 0.6 }, { x - hw, y + hh * 0.4 }, { x, y + hh * 0.55 }, darkCol, mainCol);
        drawTriangle(hdc, { x + hw * 0.55, y - hh * 0.6 }, { x + hw, y + hh * 0.4 }, { x, y + hh * 0.55 }, darkCol, mainCol);
        drawTriangle(hdc, { x, y - hh * 0.65 }, { x - hw * 0.4, y + hh * 0.3 }, { x + hw * 0.4, y + hh * 0.3 }, mainCol, brightCol);
        drawEllipse(hdc, x - hw * 0.4, y + hh * 0.55, hw * 0.16, hh * 0.18, brightCol);
        drawEllipse(hdc, x + hw * 0.4, y + hh * 0.55, hw * 0.16, hh * 0.18, brightCol);
        wingLight(x - hw * 0.9, y + hh * 0.42);
        wingLight(x + hw * 0.9, y + hh * 0.42);
        cockpit(y - hh * 0.05, 3.5, 5);
        drawCircle(hdc, x, y, hh * 1.35 + pulse, mainCol);
        drawCircle(hdc, x, y, hh * 1.15 + pulse, brightCol);
        break;
    }
    }
}

// --- Draw Enemy ---
static void drawEnemy(HDC hdc, Enemy& e) {
    double x = e.pos.x, y = e.pos.y;
    double hw = e.w / 2, hh = e.h / 2;
    COLORREF col = e.flash > 0 ? RGB(255, 255, 255) : e.color;
    COLORREF dark = RGB(22, 30, 44);

    switch (e.type) {
    case ENEMY_FAST: {
        // Sleek dart
        drawTriangle(hdc, { x - hw, y - hh * 0.25 }, { x + hw, y - hh * 0.25 }, { x, y - hh }, dark, col);
        drawTriangle(hdc, { x - hw, y - hh * 0.25 }, { x + hw, y - hh * 0.25 }, { x, y + hh }, dark, col);
        drawLine(hdc, x, y - hh, x, y + hh, col, 2);
        drawEllipse(hdc, x, y, 2.5, 2.5, RGB(255, 255, 255));
        break;
    }
    case ENEMY_SHOOTER: {
        // Heavy gunship
        drawTriangle(hdc, { x, y + hh }, { x - hw, y - hh }, { x, y - hh * 0.3 }, dark, col);
        drawTriangle(hdc, { x, y + hh }, { x + hw, y - hh }, { x, y - hh * 0.3 }, dark, col);
        drawLine(hdc, x - hw * 0.5, y - hh * 0.4, x - hw * 0.5, y + hh * 0.4, col, 1);
        drawLine(hdc, x + hw * 0.5, y - hh * 0.4, x + hw * 0.5, y + hh * 0.4, col, 1);
        drawEllipse(hdc, x - hw * 0.5, y, 3.5, 5, RGB(70, 70, 80));
        drawEllipse(hdc, x + hw * 0.5, y, 3.5, 5, RGB(70, 70, 80));
        drawEllipse(hdc, x - hw * 0.5, y + hh * 0.25, 2, 3, col);
        drawEllipse(hdc, x + hw * 0.5, y + hh * 0.25, 2, 3, col);
        drawEllipse(hdc, x, y, 3.5, 3.5, RGB(255, 255, 255));
        break;
    }
    case ENEMY_ELITE: {
        // Armored fortress
        drawTriangle(hdc, { x - hw, y - hh }, { x - hw * 0.45, y - hh * 0.2 }, { x, y - hh * 0.3 }, dark, col);
        drawTriangle(hdc, { x + hw, y - hh }, { x + hw * 0.45, y - hh * 0.2 }, { x, y - hh * 0.3 }, dark, col);
        drawTriangle(hdc, { x - hw, y - hh }, { x - hw * 0.45, y - hh * 0.2 }, { x - hw, y + hh }, dark, col);
        drawTriangle(hdc, { x + hw, y - hh }, { x + hw * 0.45, y - hh * 0.2 }, { x + hw, y + hh }, dark, col);
        drawTriangle(hdc, { x, y - hh }, { x - hw * 0.45, y - hh * 0.2 }, { x, y + hh * 0.5 }, dark, col);
        drawTriangle(hdc, { x, y - hh }, { x + hw * 0.45, y - hh * 0.2 }, { x, y + hh * 0.5 }, dark, col);
        drawEllipse(hdc, x, y - hh * 0.3, 3.5, 3.5, col);
        drawEllipse(hdc, x - hw * 0.38, y + hh * 0.4, 3, 3, col);
        drawEllipse(hdc, x + hw * 0.38, y + hh * 0.4, 3, 3, col);
        drawEllipse(hdc, x, y - hh * 0.3, 1.5, 1.5, RGB(255, 255, 255));
        break;
    }
    case ENEMY_KAMIKAZE: {
        // Diving arrow with flame trail
        drawTriangle(hdc, { x, y + hh }, { x - hw, y - hh }, { x, y - hh * 0.4 }, dark, col);
        drawTriangle(hdc, { x, y + hh }, { x + hw, y - hh }, { x, y - hh * 0.4 }, dark, col);
        drawEllipse(hdc, x, y + hh + 3, 3, 6, RGB(255, 120, 30));
        drawEllipse(hdc, x, y - hh * 0.1, 2.5, 2.5, RGB(255, 255, 255));
        break;
    }
    case ENEMY_SPLITTER: {
        // Split core with two node pods
        drawTriangle(hdc, { x, y - hh }, { x - hw, y }, { x, y + hh }, dark, col);
        drawTriangle(hdc, { x, y - hh }, { x + hw, y }, { x, y + hh }, dark, col);
        drawEllipse(hdc, x - hw * 0.55, y, 3, 4, RGB(255, 255, 255));
        drawEllipse(hdc, x + hw * 0.55, y, 3, 4, RGB(255, 255, 255));
        drawEllipse(hdc, x, y, 3, 3, col);
        break;
    }
    default: {
        // Diamond fighter
        drawTriangle(hdc, { x - hw, y - hh }, { x + hw, y - hh }, { x, y + hh }, dark, col);
        drawTriangle(hdc, { x - hw, y + hh }, { x + hw, y + hh }, { x, y - hh }, dark, col);
        drawEllipse(hdc, x, y, hw * 0.32, hh * 0.35, col);
        drawEllipse(hdc, x, y, hw * 0.16, hh * 0.18, RGB(255, 255, 255));
        break;
    }
    }

    // Shield ring for shield-bearers
    if (e.shieldN > 0) {
        drawCircle(hdc, x, y, hw + 7, RGB(0, 255, 200));
        drawCircle(hdc, x, y, hw + 3, RGB(130, 255, 220));
    }

    // HP bar for multi-hp enemies
    if (e.maxHp > 1) {
        int barW = (int)(e.w + 10);
        int barH = 3;
        int bx = (int)(x - barW / 2);
        int by = (int)(y - hh - 8);
        fillRect(hdc, bx, by, barW, barH, RGB(60, 60, 60));
        fillRect(hdc, bx, by, (int)(barW * e.hp / e.maxHp), barH, RGB(255, 255, 255));
    }
}

// --- Draw Boss ---
static void drawBoss(HDC hdc) {
    if (!game.bossAlive) return;
    Boss& b = game.boss;
    double x = b.pos.x, y = b.pos.y;
    double s = b.size;
    int lv = game.level;
    COLORREF col = b.flash > 0 ? RGB(255, 255, 255) : b.color;
    COLORREF body = b.flash > 0 ? RGB(255, 255, 255) : RGB(50, 50, 50);
    COLORREF dark = b.flash > 0 ? RGB(255, 255, 255) : RGB(26, 26, 34);

    // Paint job evolves each boss level
    int hue = (10 + lv * 8) % 360;
    COLORREF accent = hslToColor(hue, 80, 60);
    COLORREF glow = hslToColor((hue + 40) % 360, 90, 72);
    double pulse = 4.0 + sin(b.phase * 0.6 + lv) * 2.5;

    auto eye = [&](double ex, double ey, double er) {
        drawEllipse(hdc, ex, ey, er, er, RGB(0, 0, 0));
        drawEllipse(hdc, ex, ey, er * 0.45, er * 0.45, glow);
    };

    int tier = lv <= 5 ? 5 : lv <= 10 ? 10 : lv <= 20 ? 20 : lv <= 30 ? 30 : lv <= 40 ? 40 : 50;

    switch (tier) {
    case 5: {
        // WIDEBODY CRUISER - broad hull, flat wide wings, beak nose
        drawTriangle(hdc, { x, y - s }, { x - s * 0.5, y - s * 0.7 }, { x - s * 0.42, y + s * 0.8 }, body, col);
        drawTriangle(hdc, { x, y - s }, { x + s * 0.5, y - s * 0.7 }, { x + s * 0.42, y + s * 0.8 }, body, col);
        drawTriangle(hdc, { x - s * 0.42, y + s * 0.3 }, { x - s * 1.35, y }, { x - s * 1.05, y - s * 0.5 }, dark, col);
        drawTriangle(hdc, { x + s * 0.42, y + s * 0.3 }, { x + s * 1.35, y }, { x + s * 1.05, y - s * 0.5 }, dark, col);
        drawTriangle(hdc, { x, y - s * 1.15 }, { x - s * 0.22, y - s * 0.45 }, { x + s * 0.22, y - s * 0.45 }, accent, glow);
        drawEllipse(hdc, x - s * 0.9, y + s * 0.1, s * 0.1, s * 0.22, accent);
        drawEllipse(hdc, x + s * 0.9, y + s * 0.1, s * 0.1, s * 0.22, accent);
        drawEllipse(hdc, x, y - s * 0.32, s * 0.24, s * 0.1, RGB(0, 0, 0));
        drawEllipse(hdc, x, y - s * 0.32, s * 0.1, s * 0.05, glow);
        drawLine(hdc, x - s * 1.35, y, x - s * 0.7, y - s * 0.35, accent, 2);
        drawLine(hdc, x + s * 1.35, y, x + s * 0.7, y - s * 0.35, accent, 2);
        break;
    }
    case 10: {
        // STRIKE FRIGATE - sleek arrow, twin tail fins, giant cannon barrel
        drawTriangle(hdc, { x, y - s * 1.25 }, { x - s * 0.5, y + s * 0.1 }, { x - s * 0.2, y + s * 0.15 }, body, col);
        drawTriangle(hdc, { x, y - s * 1.25 }, { x + s * 0.5, y + s * 0.1 }, { x + s * 0.2, y + s * 0.15 }, body, col);
        drawTriangle(hdc, { x - s * 0.5, y + s * 0.1 }, { x - s * 0.95, y + s * 0.8 }, { x - s * 0.45, y + s * 0.55 }, dark, col);
        drawTriangle(hdc, { x + s * 0.5, y + s * 0.1 }, { x + s * 0.95, y + s * 0.8 }, { x + s * 0.45, y + s * 0.55 }, dark, col);
        drawEllipse(hdc, x, y + s * 0.1, s * 0.24, s * 0.5, RGB(40, 44, 52));
        drawEllipse(hdc, x, y + s * 0.5, s * 0.16, s * 0.32, accent);
        drawEllipse(hdc, x, y + s * 0.78, s * 0.12, s * 0.18, glow);
        eye(x - s * 0.16, y - s * 0.4, s * 0.1);
        eye(x + s * 0.16, y - s * 0.4, s * 0.1);
        drawEllipse(hdc, x, y - s * 1.2, s * 0.08, s * 0.14, glow);
        break;
    }
    case 20: {
        // SPLIT-WING SAUCER - crescent wings, dome core, energy ring
        drawTriangle(hdc, { x - s * 0.5, y - s * 0.1 }, { x - s * 1.35, y - s * 0.55 }, { x - s * 1.0, y + s * 0.7 }, dark, col);
        drawTriangle(hdc, { x + s * 0.5, y - s * 0.1 }, { x + s * 1.35, y - s * 0.55 }, { x + s * 1.0, y + s * 0.7 }, dark, col);
        drawTriangle(hdc, { x - s * 0.5, y - s * 0.1 }, { x - s * 1.1, y + s * 0.1 }, { x - s * 0.6, y + s * 0.5 }, accent, accent);
        drawTriangle(hdc, { x + s * 0.5, y - s * 0.1 }, { x + s * 1.1, y + s * 0.1 }, { x + s * 0.6, y + s * 0.5 }, accent, accent);
        drawEllipse(hdc, x, y, s * 0.52, s * 0.4, body);
        drawEllipse(hdc, x, y, s * 0.4, s * 0.3, col);
        drawEllipse(hdc, x, y, s * 0.26, s * 0.2, glow);
        drawCircle(hdc, x, y, s * 0.62, accent);
        eye(x, y - s * 0.08, s * 0.1);
        drawLine(hdc, x, y - s * 0.4, x, y - s * 0.7, accent, 2);
        drawEllipse(hdc, x, y - s * 0.75, s * 0.07, s * 0.07, glow);
        break;
    }
    case 30: {
        // HEAVY DREADNOUGHT - stacked armor slabs, side turrets, glowing reactor
        drawTriangle(hdc, { x, y - s * 1.1 }, { x - s * 0.35, y - s * 0.6 }, { x + s * 0.35, y - s * 0.6 }, body, col);
        drawTriangle(hdc, { x - s * 0.35, y - s * 0.6 }, { x - s * 0.8, y + s * 0.25 }, { x, y + s * 0.1 }, dark, col);
        drawTriangle(hdc, { x + s * 0.35, y - s * 0.6 }, { x + s * 0.8, y + s * 0.25 }, { x, y + s * 0.1 }, dark, col);
        drawTriangle(hdc, { x - s * 0.8, y + s * 0.25 }, { x, y + s * 0.55 }, { x, y + s * 0.1 }, RGB(40, 44, 52), col);
        drawTriangle(hdc, { x + s * 0.8, y + s * 0.25 }, { x, y + s * 0.55 }, { x, y + s * 0.1 }, RGB(40, 44, 52), col);
        drawEllipse(hdc, x - s * 0.9, y - s * 0.05, s * 0.16, s * 0.3, RGB(34, 38, 46));
        drawEllipse(hdc, x + s * 0.9, y - s * 0.05, s * 0.16, s * 0.3, RGB(34, 38, 46));
        drawEllipse(hdc, x - s * 0.9, y + s * 0.12, s * 0.09, s * 0.2, accent);
        drawEllipse(hdc, x + s * 0.9, y + s * 0.12, s * 0.09, s * 0.2, accent);
        drawCircle(hdc, x, y + s * 0.28, s * 0.26 + pulse, glow);
        drawCircle(hdc, x, y + s * 0.28, s * 0.14, RGB(255, 255, 255));
        eye(x, y - s * 0.4, s * 0.11);
        drawLine(hdc, x - s * 0.8, y + s * 0.25, x + s * 0.8, y + s * 0.25, glow, 1);
        break;
    }
    case 40: {
        // ENERGY WRAITH - claw wings, spirit mandibles, arcing aura
        drawTriangle(hdc, { x - s * 0.35, y - s * 0.15 }, { x - s * 1.4, y - s * 0.3 }, { x - s * 1.05, y + s * 0.75 }, dark, col);
        drawTriangle(hdc, { x + s * 0.35, y - s * 0.15 }, { x + s * 1.4, y - s * 0.3 }, { x + s * 1.05, y + s * 0.75 }, dark, col);
        drawEllipse(hdc, x - s * 1.28, y - s * 0.25, s * 0.09, s * 0.09, glow);
        drawEllipse(hdc, x + s * 1.28, y - s * 0.25, s * 0.09, s * 0.09, glow);
        drawEllipse(hdc, x, y, s * 0.45, s * 0.55, body);
        drawEllipse(hdc, x, y, s * 0.3, s * 0.42, col);
        drawLine(hdc, x - s * 0.3, y - s * 0.3, x - s * 0.6, y - s * 0.1, glow, 3);
        drawLine(hdc, x + s * 0.3, y - s * 0.3, x + s * 0.6, y - s * 0.1, glow, 3);
        drawEllipse(hdc, x, y - s * 0.05, s * 0.16, s * 0.11, RGB(0, 0, 0));
        drawEllipse(hdc, x, y - s * 0.05, s * 0.08, s * 0.06, glow);
        drawCircle(hdc, x, y, s * 1.3 + pulse, glow);
        drawCircle(hdc, x, y, s * 1.45 + pulse, accent);
        break;
    }
    default: {
        // FINAL OVERLORD - crowned fortress, radiating spikes, triple core
        drawTriangle(hdc, { x - s * 0.3, y - s * 0.75 }, { x - s * 0.12, y - s * 1.35 }, { x + s * 0.02, y - s * 0.75 }, dark, glow);
        drawTriangle(hdc, { x + s * 0.3, y - s * 0.75 }, { x + s * 0.12, y - s * 1.35 }, { x - s * 0.02, y - s * 0.75 }, dark, glow);
        drawTriangle(hdc, { x, y - s * 0.75 }, { x - s * 0.75, y - s * 0.3 }, { x - s * 0.55, y + s * 0.8 }, body, col);
        drawTriangle(hdc, { x, y - s * 0.75 }, { x + s * 0.75, y - s * 0.3 }, { x + s * 0.55, y + s * 0.8 }, body, col);
        drawTriangle(hdc, { x - s * 0.55, y + s * 0.1 }, { x - s * 1.4, y - s * 0.1 }, { x - s * 1.15, y + s * 0.8 }, dark, col);
        drawTriangle(hdc, { x + s * 0.55, y + s * 0.1 }, { x + s * 1.4, y - s * 0.1 }, { x + s * 1.15, y + s * 0.8 }, dark, col);
        drawTriangle(hdc, { x - s * 1.4, y - s * 0.1 }, { x - s * 1.15, y - s * 0.1 }, { x - s * 1.28, y + s * 0.45 }, dark, accent);
        drawTriangle(hdc, { x + s * 1.4, y - s * 0.1 }, { x + s * 1.15, y - s * 0.1 }, { x + s * 1.28, y + s * 0.45 }, dark, accent);
        eye(x - s * 0.28, y + s * 0.15, s * 0.14);
        eye(x + s * 0.28, y + s * 0.15, s * 0.14);
        drawEllipse(hdc, x, y - s * 0.05, s * 0.2, s * 0.22, RGB(0, 0, 0));
        drawEllipse(hdc, x, y - s * 0.05, s * 0.1, s * 0.12, glow);
        drawLine(hdc, x, y - s * 0.05, x, y + s * 0.8, accent, 4);
        drawCircle(hdc, x, y, s * 1.5 + pulse, glow);
        break;
    }
    }

    // Telegraph the next attack wave
    if (b.telegraph > 0) {
        double tr = s * (0.7 + (28 - b.telegraph) * 0.14);
        drawCircle(hdc, x, y, tr, RGB(255, 255, 0));
        drawCircle(hdc, x, y, tr - 7, RGB(255, 60, 60));
    }

    // HP bar
    int barW = 200;
    int barH = 10;
    int bx = WINDOW_W / 2 - barW / 2;
    int by = 15;
    fillRect(hdc, bx, by, barW, barH, RGB(50, 50, 50));
    fillRect(hdc, bx, by, (int)(barW * b.hp / b.maxHp), barH, b.color);
    drawLine(hdc, bx, by, bx + barW, by, RGB(255, 255, 255), 1);
    drawLine(hdc, bx, by + barH, bx + barW, by + barH, RGB(255, 255, 255), 1);
    drawLine(hdc, bx, by, bx, by + barH, RGB(255, 255, 255), 1);
    drawLine(hdc, bx + barW, by, bx + barW, by + barH, RGB(255, 255, 255), 1);
}

// --- Main Draw ---
static void draw(HDC hdc) {
    // Background
    fillRect(hdc, 0, 0, WINDOW_W, WINDOW_H, RGB(5, 5, 16));

    // Stars
    for (auto& s : game.stars) {
        int b = (int)(s.brightness * 128 + 128);
        COLORREF c = RGB(min(255, 180 + b / 3), min(255, 180 + b / 3), min(255, 200 + b / 4));
        fillRect(hdc, (int)s.x, (int)s.y, (int)s.size + 1, (int)s.size + 1, c);
    }

    // Player bullets
    for (auto& b : game.bullets) {
        fillRect(hdc, (int)b.pos.x - 2, (int)b.pos.y - 6, 4, 12, RGB(0, 255, 255));
        fillRect(hdc, (int)b.pos.x - 1, (int)b.pos.y - 8, 2, 16, RGB(200, 255, 255));
    }

    // Laser beam
    if (game.laserCooldown > 0 && game.laserTimer > 0) {
        double bx = game.ship.x;
        drawLine(hdc, bx, 0, bx, game.ship.y, RGB(255, 80, 30), 6);
        drawLine(hdc, bx, 0, bx, game.ship.y, RGB(255, 220, 120), 2);
        drawCircle(hdc, bx, 6, 8, RGB(255, 140, 40));
        drawCircle(hdc, bx, 6, 4, RGB(255, 255, 220));
    }

    // Enemy bullets
    for (auto& b : game.enemyBullets) {
        drawCircle(hdc, b.pos.x, b.pos.y, 5.0, b.color);
        drawCircle(hdc, b.pos.x, b.pos.y, 2.5, RGB(255, 255, 255));
    }

    // Enemies
    for (auto& e : game.enemies) drawEnemy(hdc, e);

    // Boss
    drawBoss(hdc);

    // Bombardment rocks
    for (auto& r : game.bombards) {
        double cx = r.pos.x, cy = r.pos.y, sz = r.size;
        POINT pts[6];
        for (int i = 0; i < 6; i++) {
            double a = r.rot + (i * 60) * PI / 180.0;
            double rr = (i % 2 == 0) ? sz : sz * 0.7;
            pts[i].x = (int)(cx + cos(a) * rr);
            pts[i].y = (int)(cy + sin(a) * rr);
        }
        HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(r.color));
        HPEN oldPen = (HPEN)SelectObject(hdc, getPen(RGB(60, 55, 52), 1));
        Polygon(hdc, pts, 6);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);
        drawEllipse(hdc, cx, cy, sz * 0.35, sz * 0.35, RGB(210, 170, 130));
    }

    // Power-ups
    for (auto& pu : game.powerups) {
        COLORREF col = powerUpColor(pu.type);
        double bob = sin(pu.phase * 2.0) * 3.0;
        double cx = pu.pos.x, cy = pu.pos.y + bob;
        double pulse = 1.0 + sin(pu.phase * 3.0) * 0.15;
        drawCircle(hdc, cx, cy, 14 * pulse, col);
        drawCircle(hdc, cx, cy, 9 * pulse, RGB(255, 255, 255));
        char letter[2] = { ' ', 0 };
        switch (pu.type) {
        case PU_TRIPLE:     letter[0] = 'T'; break;
        case PU_SHIELD:     letter[0] = 'S'; break;
        case PU_RAPID:      letter[0] = 'R'; break;
        case PU_NUKE:       letter[0] = 'N'; break;
        case PU_EXTRA_LIFE: letter[0] = 'L'; break;
        case PU_PRISM:      letter[0] = 'P'; break;
        case PU_HOMING:     letter[0] = 'H'; break;
        case PU_LASER:      letter[0] = 'B'; break;
        }
        drawText(hdc, letter, (int)cx - 5, (int)cy - 10, 16, RGB(10, 10, 20));
    }

    // Consumable drops (squares)
    for (auto& c : game.consumables) {
        COLORREF col = consumableColor(c.type);
        double bob = sin(c.phase * 2.0) * 3.0;
        double cx = c.pos.x, cy = c.pos.y + bob;
        fillRect(hdc, (int)cx - 10, (int)cy - 10, 20, 20, col);
        fillRect(hdc, (int)cx - 5, (int)cy - 5, 10, 10, RGB(255, 255, 255));
        char letter[2] = { consumableKeyChar(c.type), 0 };
        drawText(hdc, letter, (int)cx - 4, (int)cy - 9, 14, RGB(10, 10, 20));
    }

    // Score gems (little diamonds)
    for (auto& g : game.gems) {
        double bob = sin(g.phase * 2.0) * 3.0;
        double cx = g.pos.x, cy = g.pos.y + bob;
        drawEllipse(hdc, cx, cy, 4, 6, g.color);
        drawEllipse(hdc, cx, cy - 1, 1.6, 2.2, RGB(255, 255, 240));
    }

    // Ship
    drawShip(hdc);

    // Particles
    for (auto& p : game.particles) {
        if (p.life <= 0) continue;
        int alpha = (int)(p.life * 255);
        if (p.isSpark) {
            fillRect(hdc, (int)p.pos.x - 1, (int)p.pos.y - 1, 2, 2, RGB(255, 255, 255));
        } else {
            double sz = p.size * p.life;
            drawCircle(hdc, p.pos.x, p.pos.y, sz, p.color);
        }
    }

    // HUD
    char buf[128];

    // Lives
    for (int i = 0; i < game.lives; i++) {
        double lx = 15 + i * 25;
        drawTriangle(hdc, { lx, 12 }, { lx - 8, 26 }, { lx + 8, 26 },
            i < game.lives ? RGB(0, 255, 0) : RGB(50, 50, 50),
            RGB(0, 200, 0));
    }

    // Score
    sprintf(buf, "SCORE: %d", game.score);
    drawText(hdc, buf, WINDOW_W - textWidth(hdc, buf, 18) - 15, 8, 18, RGB(0, 255, 255));

    // High score
    sprintf(buf, "BEST: %d", highScore);
    drawText(hdc, buf, WINDOW_W - textWidth(hdc, buf, 14) - 15, 30, 14, RGB(255, 220, 40));

    // Level
    sprintf(buf, "LEVEL: %d", game.level);
    drawText(hdc, buf, WINDOW_W / 2 - textWidth(hdc, buf, 18) / 2, 8, 18, RGB(100, 200, 255));

    // Active abilities
    if (game.shieldCount > 0) {
        sprintf(buf, "SHIELD x%d", game.shieldCount);
        drawText(hdc, buf, 15, 38, 14, RGB(0, 255, 180));
    }
    if (game.rapidTimer > 0) {
        sprintf(buf, "RAPID %ds", (int)ceil(game.rapidTimer / (double)FPS));
        drawText(hdc, buf, 15, 56, 14, RGB(255, 220, 0));
    }
    if (game.shipPowerLevel >= 2) {
        sprintf(buf, "POWER LV%d", game.shipPowerLevel);
        drawText(hdc, buf, 15, 74, 14, RGB(0, 170, 255));
    }
    if (game.laserTimer > 0) {
        sprintf(buf, "LASER %ds", (int)ceil(game.laserTimer / (double)FPS));
        drawText(hdc, buf, WINDOW_W - textWidth(hdc, buf, 14) - 15, 74, 14, RGB(255, 140, 40));
    }
    if (game.homingTimer > 0) {
        sprintf(buf, "HOMING %ds", (int)ceil(game.homingTimer / (double)FPS));
        drawText(hdc, buf, WINDOW_W - textWidth(hdc, buf, 14) - 15, 92, 14, RGB(120, 255, 120));
    }
    if (game.freezeTimer > 0) {
        sprintf(buf, "TIME FREEZE %ds", (int)ceil(game.freezeTimer / (double)FPS));
        drawText(hdc, buf, WINDOW_W - textWidth(hdc, buf, 14) - 15, 110, 14, RGB(120, 220, 255));
    }
    if (game.comboCount >= 2) {
        sprintf(buf, "COMBO x%d", comboMult());
        drawText(hdc, buf, WINDOW_W - textWidth(hdc, buf, 18) - 15, 52, 18, RGB(255, 180, 40));
    }

    // Consumable inventory
    int invY = 100;
    if (game.grenades > 0) {
        sprintf(buf, "G GRENADE x%d", game.grenades);
        drawText(hdc, buf, 15, invY, 14, RGB(255, 120, 0));
        invY += 18;
    }
    if (game.repairKits > 0) {
        sprintf(buf, "H REPAIR x%d", game.repairKits);
        drawText(hdc, buf, 15, invY, 14, RGB(0, 255, 80));
        invY += 18;
    }
    if (game.freezeCharges > 0) {
        sprintf(buf, "J FREEZE x%d", game.freezeCharges);
        drawText(hdc, buf, 15, invY, 14, RGB(120, 220, 255));
    }

    // Boss warning
    if (game.bossAlive && !game.boss.entered) {
        drawText(hdc, "WARNING: BOSS APPROACHING",
            WINDOW_W / 2 - textWidth(hdc, "WARNING: BOSS APPROACHING", 32) / 2,
            WINDOW_H / 2 - 20, 32, RGB(255, 50, 50));
    }

    // Ability acquired message
    if (game.msgTimer > 0 && game.msgText[0] != '\0') {
        char banner[64];
        snprintf(banner, sizeof(banner), "ABILITY! %s", game.msgText);
        int bw = textWidth(hdc, banner, 26);
        drawText(hdc, banner, WINDOW_W / 2 - bw / 2, WINDOW_H / 2 - 60, 26,
            RGB(0, 255, 180));
    }

    // Level / event banner
    if (game.bannerTimer > 0 && game.bannerText[0] != '\0') {
        int bw = textWidth(hdc, game.bannerText, 34);
        drawText(hdc, game.bannerText, WINDOW_W / 2 - bw / 2, WINDOW_H / 2 - 130, 34,
            game.bannerTimer > 30 ? RGB(255, 255, 0) : RGB(0, 255, 255));
    }
}

// --- Draw Menu ---
static void drawMenu(HDC hdc) {
    fillRect(hdc, 0, 0, WINDOW_W, WINDOW_H, RGB(5, 5, 16));

    for (auto& s : game.stars) {
        int b = (int)(s.brightness * 128 + 128);
        fillRect(hdc, (int)s.x, (int)s.y, (int)s.size + 1, (int)s.size + 1,
            RGB(min(255, 180 + b / 3), min(255, 180 + b / 3), min(255, 200 + b / 4)));
    }

    drawText(hdc, "SPACE SHOOTER", WINDOW_W / 2 - textWidth(hdc, "SPACE SHOOTER", 48) / 2, 180, 48, RGB(0, 255, 255));
    drawText(hdc, "WASD / ARROWS TO MOVE", WINDOW_W / 2 - textWidth(hdc, "WASD / ARROWS TO MOVE", 16) / 2, 260, 16, RGB(0, 170, 255));
    drawText(hdc, "SPACE TO SHOOT", WINDOW_W / 2 - textWidth(hdc, "SPACE TO SHOOT", 16) / 2, 285, 16, RGB(0, 170, 255));
    drawText(hdc, "50 LEVELS OF ACTION!", WINDOW_W / 2 - textWidth(hdc, "50 LEVELS OF ACTION!", 16) / 2, 320, 16, RGB(150, 150, 150));
    drawText(hdc, "BOSSES DROP SPECIAL ABILITIES!", WINDOW_W / 2 - textWidth(hdc, "BOSSES DROP SPECIAL ABILITIES!", 16) / 2, 345, 16, RGB(0, 255, 180));
    drawText(hdc, "SHIPS DROP GRENADES - PRESS G TO BLAST!", WINDOW_W / 2 - textWidth(hdc, "SHIPS DROP GRENADES - PRESS G TO BLAST!", 15) / 2, 370, 15, RGB(255, 120, 0));
    drawText(hdc, "CHAIN KILLS FOR COMBO BONUSES!", WINDOW_W / 2 - textWidth(hdc, "CHAIN KILLS FOR COMBO BONUSES!", 15) / 2, 390, 15, RGB(0, 255, 200));
    drawText(hdc, "BOMBARDMENTS RAIN ROCKS - DODGE!", WINDOW_W / 2 - textWidth(hdc, "BOMBARDMENTS RAIN ROCKS - DODGE!", 15) / 2, 408, 15, RGB(200, 140, 80));
    drawText(hdc, "PRESS ENTER TO START", WINDOW_W / 2 - textWidth(hdc, "PRESS ENTER TO START", 22) / 2, 432, 22, RGB(255, 255, 0));
    drawText(hdc, "PRESS E FOR INFINITE WAVES", WINDOW_W / 2 - textWidth(hdc, "PRESS E FOR INFINITE WAVES", 18) / 2, 462, 18, RGB(120, 255, 120));
    char hbuf[64];
    sprintf(hbuf, "BEST SCORE: %d", highScore);
    drawText(hdc, hbuf, WINDOW_W / 2 - textWidth(hdc, hbuf, 18) / 2, 495, 18, RGB(255, 220, 40));

    drawText(hdc, "TOP 5 PILOTS", WINDOW_W / 2 - textWidth(hdc, "TOP 5 PILOTS", 20) / 2, 530, 20, RGB(255, 220, 40));
    for (int i = 0; i < 5; i++) {
        char sb[64];
        sprintf(sb, "%d. %d  (LEVEL %d)", i + 1, topScores[i].score, topScores[i].level);
        drawText(hdc, sb, WINDOW_W / 2 - textWidth(hdc, sb, 16) / 2, 560 + i * 20, 16,
            i == 0 ? RGB(255, 220, 40) : RGB(180, 180, 190));
    }
    if (game.endless) {
        drawText(hdc, "ENDLESS MODE: ON", WINDOW_W / 2 - textWidth(hdc, "ENDLESS MODE: ON", 16) / 2, 670, 16, RGB(120, 255, 120));
    }
}

// --- Draw GameOver ---
static void drawGameOver(HDC hdc) {
    fillRect(hdc, 0, 0, WINDOW_W, WINDOW_H, RGB(5, 5, 16));
    drawText(hdc, "GAME OVER", WINDOW_W / 2 - textWidth(hdc, "GAME OVER", 48) / 2, 220, 48, RGB(255, 60, 60));
    char buf[64];
    sprintf(buf, "LEVEL: %d  SCORE: %d", game.level, game.score);
    drawText(hdc, buf, WINDOW_W / 2 - textWidth(hdc, buf, 20) / 2, 300, 20, RGB(180, 180, 180));
    sprintf(buf, "TITLE: %s", playerTitle(game.score));
    drawText(hdc, buf, WINDOW_W / 2 - textWidth(hdc, buf, 18) / 2, 330, 18, RGB(255, 200, 60));
    sprintf(buf, "BEST: %d", highScore);
    drawText(hdc, buf, WINDOW_W / 2 - textWidth(hdc, buf, 18) / 2, 360, 18, RGB(255, 220, 40));

    drawText(hdc, "TOP 5 PILOTS", WINDOW_W / 2 - textWidth(hdc, "TOP 5 PILOTS", 18) / 2, 405, 18, RGB(255, 220, 40));
    for (int i = 0; i < 5; i++) {
        char sb[64];
        sprintf(sb, "%d. %d  (LEVEL %d)", i + 1, topScores[i].score, topScores[i].level);
        drawText(hdc, sb, WINDOW_W / 2 - textWidth(hdc, sb, 15) / 2, 430 + i * 18, 15,
            i == 0 ? RGB(255, 220, 40) : RGB(160, 160, 170));
    }

    drawText(hdc, "PRESS ENTER TO RETRY", WINDOW_W / 2 - textWidth(hdc, "PRESS ENTER TO RETRY", 20) / 2, 545, 20, RGB(255, 255, 0));
    if (game.continues > 0) {
        drawText(hdc, "C - CONTINUE AT CURRENT LEVEL", WINDOW_W / 2 - textWidth(hdc, "C - CONTINUE AT CURRENT LEVEL", 16) / 2, 575, 16, RGB(0, 255, 180));
    }
}

// --- Draw Win ---
static void drawWin(HDC hdc) {
    fillRect(hdc, 0, 0, WINDOW_W, WINDOW_H, RGB(5, 5, 16));

    struct WinLine { const wchar_t* text; int size; COLORREF color; const wchar_t* face; };
    static const WinLine lines[] = {
        { L"\U0001F389 YOU WON THE GAME! \U0001F389", 30, RGB(255, 220, 60), L"Segoe UI Emoji" },
        { L"\U0001F3C6 CONGRATULATIONS! \U0001F3C6", 26, RGB(0, 255, 255), L"Segoe UI Emoji" },
        { L" ", 18, RGB(255, 255, 255), L"Segoe UI" },
        { L"You completed the challenge,", 22, RGB(200, 210, 230), L"Segoe UI" },
        { L"defeated every obstacle,", 22, RGB(200, 210, 230), L"Segoe UI" },
        { L"and made it to the end!", 22, RGB(200, 210, 230), L"Segoe UI" },
        { L" ", 18, RGB(255, 255, 255), L"Segoe UI" },
        { L"\u2B50 FINAL RESULT: VICTORY \u2B50", 28, RGB(255, 255, 0), L"Segoe UI Emoji" },
        { L" ", 18, RGB(255, 255, 255), L"Segoe UI" },
        { L"Thank you for playing!", 24, RGB(255, 255, 255), L"Segoe UI" },
        { L" ", 18, RGB(255, 255, 255), L"Segoe UI" },
        { L"\U0001F3AE GAME COMPLETED \U0001F3AE", 28, RGB(0, 255, 255), L"Segoe UI Emoji" },
        { L" ", 18, RGB(255, 255, 255), L"Segoe UI" },
        { L"Created by Armen Melkonyan", 24, RGB(255, 220, 60), L"Segoe UI" },
        { L" ", 18, RGB(255, 255, 255), L"Segoe UI" },
    };
    const int lineCount = (int)(sizeof(lines) / sizeof(lines[0]));

    const double lineH = 54.0;
    const double blockH = lineCount * lineH;
    double topY = -blockH + game.winScroll;

    for (int i = 0; i < lineCount; i++) {
        double y = topY + i * lineH;
        if (y > -30 && y < WINDOW_H + 30) {
            drawCenteredW(hdc, lines[i].text, WINDOW_W / 2.0, y, lines[i].size, lines[i].color, lines[i].face);
        }
    }

    if (game.winScroll > blockH + WINDOW_H) game.winScroll = 0;

    char wbuf[80];
    sprintf(wbuf, "SCORE: %d   TITLE: %s", game.score, playerTitle(game.score));
    drawText(hdc, wbuf, WINDOW_W / 2 - textWidth(hdc, wbuf, 18) / 2, WINDOW_H - 120, 18, RGB(0, 255, 255));
    drawText(hdc, "PRESS ENTER TO PLAY AGAIN", WINDOW_W / 2 - textWidth(hdc, "PRESS ENTER TO PLAY AGAIN", 20) / 2, WINDOW_H - 80, 20, RGB(255, 255, 0));
}

// --- Draw Pause ---
static void drawPause(HDC hdc) {
    fillRect(hdc, 0, 0, WINDOW_W, WINDOW_H, RGB(6, 6, 18));
    drawText(hdc, "PAUSED", WINDOW_W / 2 - textWidth(hdc, "PAUSED", 48) / 2, 280, 48, RGB(0, 255, 255));
    drawText(hdc, "ESC TO RESUME", WINDOW_W / 2 - textWidth(hdc, "ESC TO RESUME", 18) / 2, 360, 18, RGB(150, 200, 255));
    drawText(hdc, "Q TO QUIT TO MENU", WINDOW_W / 2 - textWidth(hdc, "Q TO QUIT TO MENU", 16) / 2, 390, 16, RGB(150, 150, 170));
}

// --- Draw Console ---
static void drawConsole(HDC hdc) {
    if (console.state == CONS_CLOSED) return;
    int conH = 200;
    int y0 = WINDOW_H - conH;

    RECT rc = { 0, y0, WINDOW_W, WINDOW_H };
    HBRUSH br = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    HPEN pen = CreatePen(PS_SOLID, 2, RGB(0, 255, 0));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, 0, y0, NULL);
    LineTo(hdc, WINDOW_W, y0);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    if (console.state == CONS_PASSWORD) {
        drawText(hdc, "ACCESS RESTRICTED", 12, y0 + 30, 18, RGB(255, 60, 60));
        drawText(hdc, "ENTER ADMIN PASSWORD:", 12, y0 + 70, 18, RGB(255, 255, 0));
        char stars[256];
        int i = 0;
        for (; i < console.inputLen && i < 30; i++) stars[i] = '*';
        stars[i] = 0;
        char prompt[300];
        snprintf(prompt, sizeof(prompt), "> %s_", stars);
        drawText(hdc, prompt, 12, y0 + 110, 18, RGB(140, 255, 140));
        drawText(hdc, "ENTER TO CONFIRM - F1 OR ESC TO EXIT", 12, y0 + 160, 14, RGB(120, 120, 120));
        return;
    }

    drawText(hdc, "ADMIN CONSOLE - PRESS F1 TO CLOSE", 10, y0 + 6, 16, RGB(0, 255, 0));
    for (int i = 0; i < console.logCount; i++) {
        drawText(hdc, console.log[i], 12, y0 + 30 + i * 18, 14, RGB(140, 255, 140));
    }
    char prompt[300];
    snprintf(prompt, sizeof(prompt), "> %s_", console.input);
    drawText(hdc, prompt, 12, WINDOW_H - 28, 18, RGB(255, 255, 0));
}

// --- Window Procedure ---
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_KEYDOWN:
        if (console.state != CONS_CLOSED) {
            if (wParam == VK_F1) { game.keys[wParam] = true; return 0; }
            if (wParam == VK_BACK) {
                if (console.inputLen > 0) console.input[--console.inputLen] = 0;
                return 0;
            }
            if (wParam == VK_RETURN) {
                consoleSubmit();
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                console.state = CONS_CLOSED;
                return 0;
            }
            return 0;
        }
        if (wParam < 256) game.keys[wParam] = true;
        if (wParam == VK_RETURN) {
            if (game.state == STATE_MENU || game.state == STATE_GAMEOVER || game.state == STATE_WIN) {
                resetGame();
            }
        }
        if (wParam == 'E' && game.state == STATE_MENU) {
            game.endless = true;
            resetGame();
        }
        if (wParam == 'C' && game.state == STATE_GAMEOVER) continueGame();
        if (wParam == VK_ESCAPE && game.state == STATE_PLAYING) {
            game.paused = !game.paused;
        }
        if (wParam == 'Q' && game.paused) {
            game.paused = false;
            game.state = STATE_MENU;
        }
        return 0;
    case WM_CHAR:
        if (console.state != CONS_CLOSED) {
            char c = (char)wParam;
            if (c == '/') return 0;
            if (c >= 32 && c <= 126 && console.inputLen < sizeof(console.input) - 1) {
                console.input[console.inputLen++] = c;
                console.input[console.inputLen] = 0;
            }
        }
        return 0;
    case WM_KEYUP:
        if (console.state == CONS_CLOSED && wParam < 256) game.keys[wParam] = false;
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// --- WinMain ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    srand((unsigned)time(NULL));

    initSounds();
    loadHighScore();

    // Load the retro font from Fonts folder
    char modulePath[MAX_PATH];
    GetModuleFileNameA(NULL, modulePath, MAX_PATH);
    char* slash2 = strrchr(modulePath, '\\');
    if (slash2) {
        strcpy(slash2 + 1, "sounds\\");
        strcpy(soundDir, modulePath);
        strcpy(slash2 + 1, "Fonts\\BM space.TTF");
        AddFontResourceExA(modulePath, FR_PRIVATE, 0);
        strcpy(slash2 + 1, "Fonts\\8-BIT WONDER.TTF");
        AddFontResourceExA(modulePath, FR_PRIVATE, 0);
    }

    initMciSounds();
    initSoundThread();

    WNDCLASSA wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "SpaceShooterClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassA(&wc);

    RECT rc = { 0, 0, WINDOW_W, WINDOW_H };
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX), FALSE);

    HWND hwnd = CreateWindowA(
        "SpaceShooterClass", "Space Shooter",
        (WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX)),
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, hInstance, NULL
    );

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    HDC hdc = GetDC(hwnd);

    // Double buffer
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBmp = CreateCompatibleBitmap(hdc, WINDOW_W, WINDOW_H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    initStars();
    game.state = STATE_MENU;

    LARGE_INTEGER freq, last, now;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&last);

    double accumulator = 0.0;
    double fixedDt = 1.0 / FPS;

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

        QueryPerformanceCounter(&now);
        double elapsed = (double)(now.QuadPart - last.QuadPart) / freq.QuadPart;
        last = now;
        if (elapsed > 0.1) elapsed = 0.1;
        accumulator += elapsed;

        while (accumulator >= fixedDt) {
            update();
            accumulator -= fixedDt;
        }

        // Draw to back buffer
        switch (game.state) {
        case STATE_MENU:     drawMenu(memDC); break;
        case STATE_PLAYING:  draw(memDC); if (game.paused) drawPause(memDC); break;
        case STATE_GAMEOVER: drawGameOver(memDC); break;
        case STATE_WIN:      drawWin(memDC); break;
        }
        drawConsole(memDC);

        // Blit to screen
        BitBlt(hdc, 0, 0, WINDOW_W, WINDOW_H, memDC, 0, 0, SRCCOPY);

        // Pace rendering to 60 FPS to avoid burning CPU (reduces lag)
        QueryPerformanceCounter(&now);
        double taken = (double)(now.QuadPart - frameStart.QuadPart) / freq.QuadPart;
        double wait = fixedDt - taken;
        if (wait > 0.001) Sleep((DWORD)(wait * 1000.0));
    }

    timeEndPeriod(1);

    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
    ReleaseDC(hwnd, hdc);

    return 0;
}

#pragma once

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
#include <atomic>

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif

static const int WINDOW_W = 640;
static const int WINDOW_H = 860;
static const int FPS = 60;
static const double PI = 3.14159265358979;

inline int randRange(int lo, int hi) { return lo + (rand() % (hi - lo + 1)); }
inline double randf() { return (double)rand() / RAND_MAX; }

enum SoundId { SND_LASER, SND_BOSS_WARN, SND_LEVEL_UP, SND_HISCORE, SND_EXPLOSION, SND_POWERUP, SND_COUNT };

struct Vec2 {
    double x, y;
    Vec2(double x = 0, double y = 0) : x(x), y(y) {}
    Vec2 operator+(const Vec2& o) const { return { x + o.x, y + o.y }; }
    Vec2 operator-(const Vec2& o) const { return { x - o.x, y - o.y }; }
    Vec2 operator*(double s) const { return { x * s, y * s }; }
    double len() const { return sqrt(x * x + y * y); }
    Vec2 norm() const { double l = len(); return l > 0 ? Vec2{ x / l, y / l } : Vec2{ 0, 0 }; }
};

struct Particle {
    Vec2 pos, vel;
    double life, decay, size;
    COLORREF color;
    bool isSpark;
};

struct Bullet {
    Vec2 pos, vel;
    double dmg;
    COLORREF color;
    bool isEnemy;
    bool homing;
};

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
    double drift;
    double swayAmp;
    int shieldN;
    bool child;
};

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
    int telegraph;
};

struct Star {
    double x, y, speed, size, brightness;
};

enum PowerUpType { PU_TRIPLE, PU_SHIELD, PU_RAPID, PU_NUKE, PU_EXTRA_LIFE, PU_PRISM, PU_HOMING, PU_LASER };
struct PowerUp {
    Vec2 pos;
    double phase;
    PowerUpType type;
};

enum ConsumableType { CONS_GRENADE, CONS_REPAIR, CONS_OVERCLOCK };
struct ConsumableDrop {
    Vec2 pos;
    double phase;
    ConsumableType type;
};

struct Bombard {
    Vec2 pos, vel;
    double size, rot, rotSpd;
    COLORREF color;
};

struct Gem {
    Vec2 pos;
    double phase;
    int value;
    COLORREF color;
};

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

enum ConsoleState { CONS_CLOSED, CONS_PASSWORD, CONS_OPEN };
struct Console {
    ConsoleState state = CONS_CLOSED;
    char input[256];
    int inputLen = 0;
    char log[8][128];
    int logCount = 0;
};

struct SplitSpec { double x, y; int hp; double spd; COLORREF col; };

struct ScoreEntry { int score; int level; };

extern Game game;
extern Console console;
extern int highScore;
extern ScoreEntry topScores[5];
extern char soundDir[MAX_PATH];
extern std::vector<SplitSpec> pendingSplits;
extern std::atomic<bool> shootSfxPending;
extern int prioritySoundTimer;
extern int laserSoundCooldown;

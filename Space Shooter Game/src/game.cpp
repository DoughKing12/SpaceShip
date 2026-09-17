#include "game.h"
#include "sound.h"

#include <cstdio>
#include <cstring>

// --- Global definitions ---
Game game;
int highScore = 2650;
ScoreEntry topScores[5] = { {2650,1},{1800,1},{1200,1},{800,1},{500,1} };

Console console;

// --- Splitter children (spawned after the enemy loop, to avoid iterator invalidation) ---
std::vector<SplitSpec> pendingSplits;

// --- Helpers ---

double dist(Vec2 a, Vec2 b) { return (a - b).len(); }

void addParticle(Vec2 pos, Vec2 vel, double life, double decay, double size, COLORREF color, bool spark) {
    game.particles.push_back(Particle{ pos, vel, life, decay, size, color, spark });
}

void screenShakeAmt(double amt) {
    if (amt > game.screenShake) game.screenShake = amt;
}

COLORREF hslToColor(int h, int s, int l) {
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

LevelConfig getLevelConfig(int level) {
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
    else cfg.enemyType = (EnemyType)(level % 6);

    return cfg;
}

// --- Explosion ---

void createExplosion(Vec2 pos, COLORREF color, int count, double size) {
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

void createDebris(Vec2 pos, COLORREF color, int count) {
    for (int i = 0; i < count; i++) {
        double angle = randf() * 2.0 * PI;
        double spd = randf() * 3.0 + 1.0;
        addParticle(pos, { cos(angle) * spd, sin(angle) * spd - 1.0 }, 1.0, 0.005 + randf() * 0.008, randf() * 3.0 + 1.0, color);
    }
}

void createShieldHit(Vec2 pos) {
    for (int i = 0; i < 12; i++) {
        double angle = (2.0 * PI / 12) * i;
        addParticle(pos, { cos(angle) * 3.0, sin(angle) * 3.0 }, 1.0, 0.04, 3.0, RGB(0, 255, 255));
    }
}

// --- Power-Up Helpers ---

COLORREF powerUpColor(PowerUpType t) {
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

const char* powerUpName(PowerUpType t) {
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

COLORREF consumableColor(ConsumableType t) {
    switch (t) {
    case CONS_GRENADE:   return RGB(255, 120, 0);
    case CONS_REPAIR:    return RGB(0, 255, 80);
    case CONS_OVERCLOCK: return RGB(120, 220, 255);
    }
    return RGB(255, 255, 255);
}

const char* consumableName(ConsumableType t) {
    switch (t) {
    case CONS_GRENADE:   return "GRENADE";
    case CONS_REPAIR:    return "REPAIR KIT";
    case CONS_OVERCLOCK: return "TIME FREEZE";
    }
    return "???";
}

char consumableKeyChar(ConsumableType t) {
    switch (t) {
    case CONS_GRENADE:   return 'G';
    case CONS_REPAIR:    return 'H';
    case CONS_OVERCLOCK: return 'J';
    }
    return '?';
}

void dropConsumableChance(Vec2 pos) {
    int roll = rand() % 100;
    ConsumableType t = CONS_GRENADE;
    if (roll < 6) t = CONS_REPAIR;
    else if (roll < 9) t = CONS_OVERCLOCK;
    else return;
    game.consumables.push_back(ConsumableDrop{ pos, randf() * 2.0 * PI, t });
}

void collectConsumable(ConsumableType t) {
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

void useGrenade() {
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

void useRepairKit() {
    if (game.repairKits <= 0 || game.lives >= 5) return;
    game.repairKits--;
    game.lives++;
    snprintf(game.msgText, sizeof(game.msgText), "+1 LIFE");
    game.msgTimer = FPS * 2;
}

void useTimeFreeze() {
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

void dropBossRewards() {
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
        game.powerups.push_back(PowerUp{ { px, game.boss.pos.y + (randf() - 0.5) * 30.0 }, randf() * 2.0 * PI, t });
    }
}

void applyPowerUp(PowerUpType t) {
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

// --- Combo / scoring ---

int comboMult() { return 1 + min(game.comboCount / 5, 4); }

void award(int pts) {
    game.comboCount++;
    game.comboTimer = FPS * 3;
    addScore(pts * comboMult());
}

void bannerSet(const char* text) {
    snprintf(game.bannerText, sizeof(game.bannerText), "%s", text);
    game.bannerTimer = FPS * 2;
}

bool currentIsBossLevel() {
    int lv = game.level;
    if (lv == 5 || lv == 10 || lv == 20 || lv == 30 || lv == 40 || lv == 50) return true;
    if (game.endless && lv > 50 && lv % 10 == 0) return true;
    return false;
}

bool isBombardLevel() {
    int lv = game.level;
    if (lv % 7 != 0) return false;
    if (currentIsBossLevel()) return false;
    return true;
}

// --- Score gems ---

void dropGemChance(Vec2 pos, int extraRoll) {
    if ((rand() % 100) >= extraRoll) return;
    game.gems.push_back(Gem{ { pos.x + (randf() - 0.5) * 10, pos.y + (randf() - 0.5) * 8 },
        randf() * 2.0 * PI, 5 + rand() % 25, RGB(255, 220, 40) });
}

void dropGemShower(Vec2 pos, int count) {
    for (int i = 0; i < count; i++) {
        game.gems.push_back(Gem{ { pos.x + (randf() - 0.5) * 140, pos.y + (randf() - 0.5) * 60 },
            randf() * 2.0 * PI, 10 + rand() % 40, RGB(255, 220, 40) });
    }
}

// --- Kill an enemy: score, boom, loot, split children ---

void killEnemy(Enemy& e) {
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
            pendingSplits.push_back(SplitSpec{
                e.pos.x + (i ? 14 : -14), e.pos.y + 6,
                max(1, e.maxHp / 2), e.speed * 0.8,
                hslToColor((game.level * 37) % 360, 70, 66)
            });
        }
    }
}

void killBoss() {
    Boss& b2 = game.boss;
    award(2000 + game.level * 500);
    for (int i = 0; i < 8; i++) {
        Vec2 ep = Vec2{ b2.pos.x + (randf() - 0.5) * b2.size * 1.5, b2.pos.y + (randf() - 0.5) * b2.size };
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

void killAllEnemies() {
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

// --- High Scores ---

void saveHighScore() {
    FILE* f = fopen("space_shooter_save.dat", "wb");
    if (!f) return;
    fwrite("SS5", 1, 3, f);
    fwrite(topScores, sizeof(ScoreEntry), 5, f);
    fclose(f);
}

void loadHighScore() {
    FILE* f = fopen("space_shooter_save.dat", "rb");
    if (!f) { highScore = topScores[0].score; return; }
    char magic[3] = { 0, 0, 0 };
    if (fread(magic, 1, 3, f) == 3 && memcmp(magic, "SS5", 3) == 0) {
        ScoreEntry tmp[5];
        if (fread(tmp, sizeof(ScoreEntry), 5, f) == 5) memcpy(topScores, tmp, sizeof(topScores));
    } else {
        fseek(f, 0, SEEK_SET);
        int v = 0;
        if (fread(&v, sizeof(int), 1, f) == 1 && v > 2650) topScores[0].score = v;
    }
    fclose(f);
    highScore = topScores[0].score;
}

void updateTopScores() {
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

void addScore(int pts) {
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

const char* playerTitle(int score) {
    if (score >= 200000) return "SPACE LEGEND";
    if (score >= 100000) return "GRAND ADMIRAL";
    if (score >= 50000)  return "ADMIRAL";
    if (score >= 20000)  return "ACE";
    if (score >= 5000)   return "PILOT";
    if (score >= 1000)   return "CADET";
    return "ROOKIE";
}

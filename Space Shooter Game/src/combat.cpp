#include "combat.h"

// --- Fire Player Bullet ---
void firePlayerBullet() {
    double x = game.ship.x;
    double y = game.ship.y - game.shipH / 2.0;
    game.bullets.push_back(Bullet{ {x, y}, {0, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });

    if (game.shipPowerLevel >= 2) {
        game.bullets.push_back(Bullet{ {x - 12, y}, {-0.5, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });
        game.bullets.push_back(Bullet{ {x + 12, y}, {0.5, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });
    }
    if (game.shipPowerLevel >= 3) {
        game.bullets.push_back(Bullet{ {x - 6, y}, {-0.3, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });
        game.bullets.push_back(Bullet{ {x + 6, y}, {0.3, -10}, 1.0, RGB(0, 255, 255), false, game.homingTimer > 0 });
    }

    if (laserSoundCooldown <= 0) {
        shootSfxPending = true;
        laserSoundCooldown = 3;
    }
}

// --- Fire Laser Beam (instant-hit column) ---
void fireLaser() {
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
        Vec2 rockPos = r.pos;
        r.pos.y = WINDOW_H + 100;
        award(2);
        createExplosion(rockPos, RGB(255, 170, 60), 6, 1.5);
    }
    for (int i = 0; i < 10; i++) {
        addParticle({ bx, game.ship.y - randf() * game.ship.y },
            { (randf() - 0.5) * 0.4, 0.6 }, 0.3, 0.03, 2.0, RGB(255, 140, 40), true);
    }
}

// --- Fire Enemy Bullet ---
// BUG 4: aim at the player using targetX/targetY with spread
void fireEnemyBullet(Vec2 epos, double targetX, double targetY, double speed) {
    double angle = atan2(targetY - epos.y, targetX - epos.x) + (randf() - 0.5) * 0.3;
    game.enemyBullets.push_back(Bullet{ {epos.x, epos.y + 15}, {cos(angle) * speed, sin(angle) * speed}, 1.0, RGB(255, 60, 60), true });
}

// --- Fire Boss Bullets ---
void fireBossBullets() {
    if (!game.bossAlive) return;
    Boss& b = game.boss;
    b.shootTimer++;
    if (b.shootTimer < b.shootRate) return;
    if (b.telegraph <= 0) { b.telegraph = 28; return; }
    b.telegraph--;
    if (b.telegraph > 0) return;
    b.shootTimer = 0;

    auto fireSpread = [&](int count, double spreadAngle, double spdMult, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double angle = PI / 2 + (spreadAngle * (i - (count - 1) / 2.0) / (count > 1 ? (count - 1) / 2.0 : 1.0));
            double vx = cos(angle) * b.bulletSpeed * spdMult;
            double vy = fabs(sin(angle) * b.bulletSpeed * spdMult);
            game.enemyBullets.push_back(Bullet{ b.pos, {vx, vy}, 1.0, col, true });
        }
    };

    auto fireCircle = [&](int count, double spdMult, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double a = (2.0 * PI / count) * i + b.phase * 0.5;
            double vy = fabs(sin(a) * b.bulletSpeed * spdMult);
            game.enemyBullets.push_back(Bullet{ b.pos, {cos(a) * b.bulletSpeed * spdMult, vy}, 1.0, col, true });
        }
    };

    auto fireSpiral = [&](int count, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double a = b.spiralAngle + (2.0 * PI / count) * i;
            double vy = fabs(sin(a) * b.bulletSpeed);
            game.enemyBullets.push_back(Bullet{ b.pos, {cos(a) * b.bulletSpeed, vy}, 1.0, col, true });
        }
        b.spiralAngle += 0.3;
    };

    auto fireAimed = [&](int count, double spdMult, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double dx = game.ship.x - b.pos.x + (i - (count - 1) / 2.0) * 40;
            // BUG 5: remove fabs() so bullets can aim upward
            double dy = game.ship.y - b.pos.y;
            double d = sqrt(dx * dx + dy * dy) + 0.001;
            game.enemyBullets.push_back(Bullet{ b.pos, {(dx / d) * b.bulletSpeed * spdMult, (dy / d) * b.bulletSpeed * spdMult}, 1.0, col, true });
        }
    };

    auto fireBurst = [&](int count, COLORREF col) {
        for (int i = 0; i < count; i++) {
            double a = (2.0 * PI / count) * i;
            double vy = fabs(sin(a) * b.bulletSpeed * 0.6) + 1.0;
            game.enemyBullets.push_back(Bullet{ b.pos, {cos(a) * b.bulletSpeed * 0.6, vy}, 1.0, col, true });
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
void spawnBoss(int level) {
    int bossIndex = 0;
    int bossLevels[] = { 5, 10, 20, 30, 40, 50 };
    for (int i = 0; i < 6; i++) { if (bossLevels[i] == level) { bossIndex = i; break; } }
    if (game.endless && level > 50) bossIndex = 5;

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
void spawnEnemyAt(LevelConfig& cfg, double x, double y, double phase) {
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

    if (game.level >= 20 && (e.type == ENEMY_SHOOTER || e.type == ENEMY_ELITE || e.type == ENEMY_SPLITTER)) {
        e.shieldN = game.level >= 35 ? 2 : 1;
    }

    game.enemies.push_back(e);
}

void spawnEnemy(LevelConfig& cfg) {
    spawnEnemyAt(cfg, (double)randRange(30, WINDOW_W - 30), -30.0, randf() * 2.0 * PI);
}

// --- Spawn Formation ---
void spawnFormation(LevelConfig& cfg) {
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
        case 0: {
            int k = i / 2;
            int side = (i % 2 == 0) ? -1 : 1;
            dx = side * (22.0 + k * 44.0);
            dy = k * 32.0;
            break;
        }
        case 1: {
            dx = (i - (squad - 1) / 2.0) * 50.0;
            dy = (i % 2) * 30.0;
            break;
        }
        default: {
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
void hitPlayer() {
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
void nextLevel() {
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

// --- Continue ---
void continueGame() {
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
void resetGame() {
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
void initStars() {
    game.stars.clear();
    for (int i = 0; i < 100; i++) {
        game.stars.push_back(Star{
            randf() * WINDOW_W,
            randf() * WINDOW_H,
            randf() * 3.0 + 0.5,
            randf() * 2.0 + 0.5,
            randf()
        });
    }
}

// --- Update ---
void update() {
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

    if (game.keys['P'] && !game.keysPrev['P'] && game.killArmed) {
        killAllEnemies();
        memcpy(game.keysPrev, game.keys, sizeof(game.keys));
    }
    if (console.state != CONS_CLOSED) return;

    // BUG 3: sync keysPrev at top of non-console section
    memcpy(game.keysPrev, game.keys, sizeof(game.keys));

    if (game.state == STATE_WIN) {
        game.winScroll += 0.9;
    }
    if (game.state != STATE_PLAYING) return;
    if (game.paused) return;

    game.shipThrustPhase += 0.15;

    double spd = 5.0 * game.cheatSpeed;
    if (game.keys['A'] || game.keys[VK_LEFT])  game.ship.x -= spd;
    if (game.keys['D'] || game.keys[VK_RIGHT]) game.ship.x += spd;
    if (game.keys['W'] || game.keys[VK_UP])    game.ship.y -= spd;
    if (game.keys['S'] || game.keys[VK_DOWN])  game.ship.y += spd;
    game.ship.x = max(game.shipW / 2.0, min((double)(WINDOW_W - game.shipW / 2.0), game.ship.x));
    game.ship.y = max(game.shipH / 2.0, min((double)(WINDOW_H - game.shipH / 2.0), game.ship.y));

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

    if (game.keys['G'] && !game.keysPrev['G']) useGrenade();
    if (game.keys['H'] && !game.keysPrev['H']) useRepairKit();
    if (game.keys['J'] && !game.keysPrev['J']) useTimeFreeze();
    memcpy(game.keysPrev, game.keys, sizeof(game.keys));

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

    LevelConfig cfg = getLevelConfig(game.level);
    if (!cfg.isBoss && game.levelEnemiesLeft > 0) {
        game.spawnTimer++;
        if (game.spawnTimer >= cfg.spawnRate) {
            game.spawnTimer = 0;
            spawnFormation(cfg);
        }
    }

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

        if (e.pos.y > WINDOW_H + 50) {
            e.pos.y = -30;
            e.pos.x = (double)randRange(30, WINDOW_W - 30);
            e.phase = randf() * 2.0 * PI;
            e.shootTimer = randf() * e.shootRate * 0.3;
            e.flash = 0;
        }
    }

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

    // BUG 2: save bullet position before moving off-screen
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
                Vec2 hitPos = b.pos;
                b.pos.y = -100;
                createExplosion({ hitPos.x, hitPos.y + 10 }, RGB(0, 255, 255), 3, 1.0);
                if (e.hp <= 0) killEnemy(e);
            }
        }

        if (game.bossAlive) {
            Boss& b2 = game.boss;
            if (fabs(b.pos.x - b2.pos.x) < (b2.size + 4) && fabs(b.pos.y - b2.pos.y) < (b2.size * 0.75 + 8)) {
                b2.hp -= (int)b.dmg;
                b2.flash = 3;
                Vec2 hitPos = b.pos;
                b.pos.y = -100;
                createExplosion({ hitPos.x, hitPos.y + 10 }, RGB(0, 255, 255), 3, 1.0);
                if (b2.hp <= 0) killBoss();
            }
        }
    }

    for (auto& b : game.enemyBullets) {
        if (fabs(b.pos.x - game.ship.x) < (game.shipW / 2 + 3) && fabs(b.pos.y - game.ship.y) < (game.shipH / 2 + 3)) {
            hitPlayer();
            b.pos.y = WINDOW_H + 100;
        }
    }

    for (auto& e : game.enemies) {
        if (e.hp > 0 && fabs(e.pos.x - game.ship.x) < (e.w / 2 + game.shipW / 2) &&
            fabs(e.pos.y - game.ship.y) < (e.h / 2 + game.shipH / 2)) {
            hitPlayer();
            e.hp -= 2;
            if (e.hp <= 0) killEnemy(e);
        }
    }

    game.enemies.erase(
        std::remove_if(game.enemies.begin(), game.enemies.end(), [](const Enemy& e) { return e.hp <= 0; }),
        game.enemies.end());

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

    // BUG 9: consolidated powerups into single pass
    for (auto& pu : game.powerups) {
        pu.pos.y += 2.2;
        pu.phase += 0.06;
        if (fabs(pu.pos.x - game.ship.x) < 32 && fabs(pu.pos.y - game.ship.y) < 32) {
            applyPowerUp(pu.type);
            pu.pos.y = WINDOW_H + 100;
        }
    }
    game.powerups.erase(
        std::remove_if(game.powerups.begin(), game.powerups.end(),
            [](const PowerUp& pu) { return pu.pos.y > WINDOW_H + 40; }),
        game.powerups.end());

    // BUG 9: consolidated consumables into single pass
    for (auto& c : game.consumables) {
        c.pos.y += 2.2;
        c.phase += 0.06;
        if (fabs(c.pos.x - game.ship.x) < 30 && fabs(c.pos.y - game.ship.y) < 30) {
            collectConsumable(c.type);
            c.pos.y = WINDOW_H + 100;
        }
    }
    game.consumables.erase(
        std::remove_if(game.consumables.begin(), game.consumables.end(),
            [](const ConsumableDrop& c) { return c.pos.y > WINDOW_H + 40; }),
        game.consumables.end());

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
                game.bombards.push_back(Bombard{
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

    // BUG 1: save rock position before moving off-screen for explosion
    for (auto& b : game.bullets) {
        for (auto& r : game.bombards) {
            if (dist(b.pos, r.pos) < r.size + 4) {
                Vec2 rockPos = r.pos;
                b.pos.y = -100;
                r.pos.y = WINDOW_H + 100;
                award(5);
                createExplosion(rockPos, RGB(255, 170, 60), 10, 2.0);
                screenShakeAmt(3.0);
            }
        }
    }
    game.bombards.erase(
        std::remove_if(game.bombards.begin(), game.bombards.end(),
            [](const Bombard& r) { return r.pos.y > WINDOW_H + 60; }),
        game.bombards.end());

    if (game.bombardActive && game.bombardLeft <= 0 && game.bombards.empty()) game.bombardActive = false;

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

    for (auto& p : game.particles) {
        p.pos = p.pos + p.vel;
        p.life -= p.decay;
        if (p.isSpark) { }
        else { p.vel.x *= 0.98; p.vel.y *= 0.98; }
    }
    game.particles.erase(
        std::remove_if(game.particles.begin(), game.particles.end(), [](const Particle& p) { return p.life <= 0; }),
        game.particles.end());

    for (auto& s : game.stars) {
        s.y += s.speed;
        if (s.y > WINDOW_H) { s.y = 0; s.x = randf() * WINDOW_W; }
    }

    // BUG 8: screenShake decay with zero-clamp
    if (game.screenShake > 0) {
        game.screenShake *= 0.85;
        if (game.screenShake < 0.01) game.screenShake = 0;
    }

    if (!cfg.isBoss && !game.bossActive && game.levelEnemiesLeft <= 0 && game.enemies.empty()) {
        game.levelTimer++;
        if (game.levelTimer > 60) nextLevel();
    }
    if (!game.bossActive && !game.bossAlive && cfg.isBoss && game.powerups.empty()) {
        game.levelTimer++;
        if (game.levelTimer > 60) nextLevel();
    }

    if (game.msgTimer > 0) game.msgTimer--;
    if (game.bannerTimer > 0) game.bannerTimer--;

    if (laserSoundCooldown > 0) laserSoundCooldown--;
    if (prioritySoundTimer > 0) prioritySoundTimer--;
}

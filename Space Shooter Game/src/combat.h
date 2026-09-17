#pragma once

#include "types.h"
#include "sound.h"
#include "game.h"

int randRange(int lo, int hi);
double randf();
double dist(Vec2 a, Vec2 b);
COLORREF hslToColor(int h, int s, int l);

void firePlayerBullet();
void fireLaser();
void fireEnemyBullet(Vec2 epos, double targetX, double targetY, double speed);
void fireBossBullets();
void spawnBoss(int level);
void spawnEnemyAt(LevelConfig& cfg, double x, double y, double phase);
void spawnEnemy(LevelConfig& cfg);
void spawnFormation(LevelConfig& cfg);
void hitPlayer();
void nextLevel();
void continueGame();
void resetGame();
void initStars();
void update();

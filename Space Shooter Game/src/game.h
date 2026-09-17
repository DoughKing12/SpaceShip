#pragma once

#include "types.h"

// Global game state
extern Game game;
extern ScoreEntry topScores[5];
extern int highScore;

// --- Helpers ---
double dist(Vec2 a, Vec2 b);
void addParticle(Vec2 pos, Vec2 vel, double life, double decay, double size, COLORREF color, bool spark = false);
void screenShakeAmt(double amt);
COLORREF hslToColor(int h, int s, int l);

// --- Level config ---
LevelConfig getLevelConfig(int level);

// --- Visual effects ---
void createExplosion(Vec2 pos, COLORREF color, int count, double size);
void createDebris(Vec2 pos, COLORREF color, int count);
void createShieldHit(Vec2 pos);

// --- Power-ups ---
COLORREF powerUpColor(PowerUpType t);
const char* powerUpName(PowerUpType t);
void applyPowerUp(PowerUpType t);

// --- Consumables ---
COLORREF consumableColor(ConsumableType t);
const char* consumableName(ConsumableType t);
char consumableKeyChar(ConsumableType t);
void dropConsumableChance(Vec2 pos);
void collectConsumable(ConsumableType t);
void useGrenade();
void useRepairKit();
void useTimeFreeze();
void dropBossRewards();

// --- Combo / scoring ---
int comboMult();
void award(int pts);
void bannerSet(const char* text);
bool currentIsBossLevel();
bool isBombardLevel();

// --- Score gems ---
void dropGemChance(Vec2 pos, int extraRoll = 30);
void dropGemShower(Vec2 pos, int count);

// --- Combat ---
void killEnemy(Enemy& e);
void killBoss();
void killAllEnemies();

// --- High scores ---
void saveHighScore();
void loadHighScore();
void updateTopScores();
void addScore(int pts);
const char* playerTitle(int score);

#include "console.h"
#include "types.h"
#include "sound.h"
#include "game.h"
#include "combat.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

const char* CONSOLE_PASSWORD = "3412";

void consoleLog(const char* text) {
    for (int i = 0; i < 7; i++) memcpy(console.log[i], console.log[i + 1], sizeof(console.log[i]));
    strncpy(console.log[7], text, sizeof(console.log[7]) - 1);
    console.log[7][sizeof(console.log[7]) - 1] = 0;
    if (console.logCount < 8) console.logCount++;
}

void consoleWelcome() {
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

void consoleSubmit() {
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

void execConsoleCommand(const char* cmd) {
    char buf[256];
    strncpy(buf, cmd, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    while (*buf == ' ' || *buf == '/') memmove(buf, buf + 1, strlen(buf));

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
        game.bossActive = false;
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
        game.bossActive = false;
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

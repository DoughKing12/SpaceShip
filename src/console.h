#pragma once
#include "types.h"

void consoleLog(const char* text);
void consoleWelcome();
void consoleSubmit();
void execConsoleCommand(const char* cmd);

extern const char* CONSOLE_PASSWORD;

#pragma once
#include "types.h"

HBRUSH getBrush(COLORREF c);
HPEN getPen(COLORREF c, int width);
void fillRect(HDC hdc, int x, int y, int w, int h, COLORREF c);
void drawCircle(HDC hdc, double cx, double cy, double r, COLORREF c);
void drawTriangle(HDC hdc, Vec2 p1, Vec2 p2, Vec2 p3, COLORREF fill, COLORREF stroke);
void drawEllipse(HDC hdc, double cx, double cy, double rx, double ry, COLORREF fill);
void drawLine(HDC hdc, double x1, double y1, double x2, double y2, COLORREF c, int width = 1);

void setTextColor(HDC hdc, COLORREF c);
HFONT getFont(int size);
void drawText(HDC hdc, const char* text, int x, int y, int size, COLORREF c);
int textWidth(HDC hdc, const char* text, int size);
void drawCenteredW(HDC hdc, const wchar_t* text, double cx, double y, int size, COLORREF c, const wchar_t* face);

void drawShip(HDC hdc);
void drawEnemy(HDC hdc, Enemy& e);
void drawBoss(HDC hdc);

void draw(HDC hdc);
void drawMenu(HDC hdc);
void drawGameOver(HDC hdc);
void drawWin(HDC hdc);
void drawPause(HDC hdc);
void drawConsole(HDC hdc);

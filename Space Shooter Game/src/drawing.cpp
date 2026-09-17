#include "drawing.h"
#include "game.h"

static std::map<DWORD, HBRUSH> brushCache;
static std::map<unsigned long long, HPEN> penCache;

HBRUSH getBrush(COLORREF c) {
    auto it = brushCache.find((DWORD)c);
    if (it != brushCache.end()) return it->second;
    HBRUSH b = CreateSolidBrush(c);
    brushCache[(DWORD)c] = b;
    return b;
}

HPEN getPen(COLORREF c, int width) {
    unsigned long long key = ((unsigned long long)(unsigned short)width << 32) | (DWORD)c;
    auto it = penCache.find(key);
    if (it != penCache.end()) return it->second;
    HPEN p = CreatePen(PS_SOLID, width, c);
    penCache[key] = p;
    return p;
}

void fillRect(HDC hdc, int x, int y, int w, int h, COLORREF c) {
    RECT r = { x, y, x + w, y + h };
    FillRect(hdc, &r, getBrush(c));
}

void drawCircle(HDC hdc, double cx, double cy, double r, COLORREF c) {
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(c));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(c, 1));
    Ellipse(hdc, (int)(cx - r), (int)(cy - r), (int)(cx + r), (int)(cy + r));
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

void drawTriangle(HDC hdc, Vec2 p1, Vec2 p2, Vec2 p3, COLORREF fill, COLORREF stroke) {
    POINT pts[3] = { {(int)p1.x, (int)p1.y}, {(int)p2.x, (int)p2.y}, {(int)p3.x, (int)p3.y} };
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(fill));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(stroke, 1));
    Polygon(hdc, pts, 3);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

void drawEllipse(HDC hdc, double cx, double cy, double rx, double ry, COLORREF fill) {
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, getBrush(fill));
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(fill, 1));
    Ellipse(hdc, (int)(cx - rx), (int)(cy - ry), (int)(cx + rx), (int)(cy + ry));
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
}

void drawLine(HDC hdc, double x1, double y1, double x2, double y2, COLORREF c, int width) {
    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(c, width));
    MoveToEx(hdc, (int)x1, (int)y1, NULL);
    LineTo(hdc, (int)x2, (int)y2);
    SelectObject(hdc, oldPen);
}

void setTextColor(HDC hdc, COLORREF c) {
    SetTextColor(hdc, c);
    SetBkMode(hdc, TRANSPARENT);
}

static std::map<int, HFONT> fontCache;

HFONT getFont(int size) {
    auto it = fontCache.find(size);
    if (it != fontCache.end()) return it->second;
    HFONT font = CreateFontA(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, "BM space");
    fontCache[size] = font;
    return font;
}

void drawText(HDC hdc, const char* text, int x, int y, int size, COLORREF c) {
    HFONT oldFont = (HFONT)SelectObject(hdc, getFont(size));
    setTextColor(hdc, c);
    TextOutA(hdc, x, y, text, (int)strlen(text));
    SelectObject(hdc, oldFont);
}

int textWidth(HDC hdc, const char* text, int size) {
    HFONT oldFont = (HFONT)SelectObject(hdc, getFont(size));
    SIZE sz;
    GetTextExtentPoint32A(hdc, text, (int)strlen(text), &sz);
    SelectObject(hdc, oldFont);
    return sz.cx;
}

void drawCenteredW(HDC hdc, const wchar_t* text, double cx, double y, int size, COLORREF c, const wchar_t* face) {
    static std::map<int, HFONT> emojiFontCache;
    auto it = emojiFontCache.find(size);
    HFONT font;
    if (it != emojiFontCache.end()) {
        font = it->second;
    } else {
        font = CreateFontW(size, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, face);
        emojiFontCache[size] = font;
    }
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    setTextColor(hdc, c);
    SIZE sz;
    GetTextExtentPoint32W(hdc, text, (int)wcslen(text), &sz);
    TextOutW(hdc, (int)(cx - sz.cx / 2.0), (int)y, text, (int)wcslen(text));
    SelectObject(hdc, oldFont);
}

void drawShip(HDC hdc) {
    if (game.shipInvincible > 0 && (game.shipInvincible / 4) % 2 == 0) return;

    double x = game.ship.x;
    double y = game.ship.y;
    double hw = game.shipW / 2;
    double hh = game.shipH / 2;

    int lv = game.level;
    double hue = 185 - (lv - 1) * 6.0;
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
    double lvDrift = lv * 0.02;
    double pulse = sin(game.abilityPulse * PI * 2) * (lv >= 20 ? 3.0 : 1.5);

    if (game.shieldCount > 0) {
        double r = 32.0 + sin(game.abilityPulse * PI * 2) * 2.0;
        drawCircle(hdc, x, y, r, RGB(0, 255, 180));
        drawCircle(hdc, x, y, r - 4, RGB(0, 160, 255));
        drawCircle(hdc, x, y, r - 8, RGB(0, 80, 160));
    }

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

    int tier = lv <= 5 ? 5 : lv <= 10 ? 10 : lv <= 20 ? 20 : lv <= 30 ? 30 : lv <= 40 ? 40 : 50;

    switch (tier) {
    case 5: {
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

void drawEnemy(HDC hdc, Enemy& e) {
    double x = e.pos.x, y = e.pos.y;
    double hw = e.w / 2, hh = e.h / 2;
    COLORREF col = e.flash > 0 ? RGB(255, 255, 255) : e.color;
    COLORREF dark = RGB(22, 30, 44);

    switch (e.type) {
    case ENEMY_FAST: {
        drawTriangle(hdc, { x - hw, y - hh * 0.25 }, { x + hw, y - hh * 0.25 }, { x, y - hh }, dark, col);
        drawTriangle(hdc, { x - hw, y - hh * 0.25 }, { x + hw, y - hh * 0.25 }, { x, y + hh }, dark, col);
        drawLine(hdc, x, y - hh, x, y + hh, col, 2);
        drawEllipse(hdc, x, y, 2.5, 2.5, RGB(255, 255, 255));
        break;
    }
    case ENEMY_SHOOTER: {
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
        drawTriangle(hdc, { x, y + hh }, { x - hw, y - hh }, { x, y - hh * 0.4 }, dark, col);
        drawTriangle(hdc, { x, y + hh }, { x + hw, y - hh }, { x, y - hh * 0.4 }, dark, col);
        drawEllipse(hdc, x, y + hh + 3, 3, 6, RGB(255, 120, 30));
        drawEllipse(hdc, x, y - hh * 0.1, 2.5, 2.5, RGB(255, 255, 255));
        break;
    }
    case ENEMY_SPLITTER: {
        drawTriangle(hdc, { x, y - hh }, { x - hw, y }, { x, y + hh }, dark, col);
        drawTriangle(hdc, { x, y - hh }, { x + hw, y }, { x, y + hh }, dark, col);
        drawEllipse(hdc, x - hw * 0.55, y, 3, 4, RGB(255, 255, 255));
        drawEllipse(hdc, x + hw * 0.55, y, 3, 4, RGB(255, 255, 255));
        drawEllipse(hdc, x, y, 3, 3, col);
        break;
    }
    default: {
        drawTriangle(hdc, { x - hw, y - hh }, { x + hw, y - hh }, { x, y + hh }, dark, col);
        drawTriangle(hdc, { x - hw, y + hh }, { x + hw, y + hh }, { x, y - hh }, dark, col);
        drawEllipse(hdc, x, y, hw * 0.32, hh * 0.35, col);
        drawEllipse(hdc, x, y, hw * 0.16, hh * 0.18, RGB(255, 255, 255));
        break;
    }
    }

    if (e.shieldN > 0) {
        drawCircle(hdc, x, y, hw + 7, RGB(0, 255, 200));
        drawCircle(hdc, x, y, hw + 3, RGB(130, 255, 220));
    }

    if (e.maxHp > 1) {
        int barW = (int)(e.w + 10);
        int barH = 3;
        int bx = (int)(x - barW / 2);
        int by = (int)(y - hh - 8);
        fillRect(hdc, bx, by, barW, barH, RGB(60, 60, 60));
        fillRect(hdc, bx, by, (int)(barW * e.hp / e.maxHp), barH, RGB(255, 255, 255));
    }
}

void drawBoss(HDC hdc) {
    if (!game.bossAlive) return;
    Boss& b = game.boss;
    double x = b.pos.x, y = b.pos.y;
    double s = b.size;
    int lv = game.level;
    COLORREF col = b.flash > 0 ? RGB(255, 255, 255) : b.color;
    COLORREF body = b.flash > 0 ? RGB(255, 255, 255) : RGB(50, 50, 50);
    COLORREF dark = b.flash > 0 ? RGB(255, 255, 255) : RGB(26, 26, 34);

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

    if (b.telegraph > 0) {
        double tr = s * (0.7 + (28 - b.telegraph) * 0.14);
        drawCircle(hdc, x, y, tr, RGB(255, 255, 0));
        drawCircle(hdc, x, y, tr - 7, RGB(255, 60, 60));
    }

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

void draw(HDC hdc) {
    fillRect(hdc, 0, 0, WINDOW_W, WINDOW_H, RGB(5, 5, 16));

    for (auto& s : game.stars) {
        int b = (int)(s.brightness * 128 + 128);
        COLORREF c = RGB(min(255, 180 + b / 3), min(255, 180 + b / 3), min(255, 200 + b / 4));
        fillRect(hdc, (int)s.x, (int)s.y, (int)s.size + 1, (int)s.size + 1, c);
    }

    for (auto& b : game.bullets) {
        fillRect(hdc, (int)b.pos.x - 2, (int)b.pos.y - 6, 4, 12, RGB(0, 255, 255));
        fillRect(hdc, (int)b.pos.x - 1, (int)b.pos.y - 8, 2, 16, RGB(200, 255, 255));
    }

    if (game.laserCooldown > 0 && game.laserTimer > 0) {
        double bx = game.ship.x;
        drawLine(hdc, bx, 0, bx, game.ship.y, RGB(255, 80, 30), 6);
        drawLine(hdc, bx, 0, bx, game.ship.y, RGB(255, 220, 120), 2);
        drawCircle(hdc, bx, 6, 8, RGB(255, 140, 40));
        drawCircle(hdc, bx, 6, 4, RGB(255, 255, 220));
    }

    for (auto& b : game.enemyBullets) {
        drawCircle(hdc, b.pos.x, b.pos.y, 5.0, b.color);
        drawCircle(hdc, b.pos.x, b.pos.y, 2.5, RGB(255, 255, 255));
    }

    for (auto& e : game.enemies) drawEnemy(hdc, e);

    drawBoss(hdc);

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

    for (auto& c : game.consumables) {
        COLORREF col = consumableColor(c.type);
        double bob = sin(c.phase * 2.0) * 3.0;
        double cx = c.pos.x, cy = c.pos.y + bob;
        fillRect(hdc, (int)cx - 10, (int)cy - 10, 20, 20, col);
        fillRect(hdc, (int)cx - 5, (int)cy - 5, 10, 10, RGB(255, 255, 255));
        char letter[2] = { consumableKeyChar(c.type), 0 };
        drawText(hdc, letter, (int)cx - 4, (int)cy - 9, 14, RGB(10, 10, 20));
    }

    for (auto& g : game.gems) {
        double bob = sin(g.phase * 2.0) * 3.0;
        double cx = g.pos.x, cy = g.pos.y + bob;
        drawEllipse(hdc, cx, cy, 4, 6, g.color);
        drawEllipse(hdc, cx, cy - 1, 1.6, 2.2, RGB(255, 255, 240));
    }

    drawShip(hdc);

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

    char buf[128];

    for (int i = 0; i < game.lives; i++) {
        double lx = 15 + i * 25;
        drawTriangle(hdc, { lx, 12 }, { lx - 8, 26 }, { lx + 8, 26 },
            RGB(0, 255, 0),
            RGB(0, 200, 0));
    }

    sprintf(buf, "SCORE: %d", game.score);
    drawText(hdc, buf, WINDOW_W - textWidth(hdc, buf, 18) - 15, 8, 18, RGB(0, 255, 255));

    sprintf(buf, "BEST: %d", highScore);
    drawText(hdc, buf, WINDOW_W - textWidth(hdc, buf, 14) - 15, 30, 14, RGB(255, 220, 40));

    sprintf(buf, "LEVEL: %d", game.level);
    drawText(hdc, buf, WINDOW_W / 2 - textWidth(hdc, buf, 18) / 2, 8, 18, RGB(100, 200, 255));

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

    if (game.bossAlive && !game.boss.entered) {
        drawText(hdc, "WARNING: BOSS APPROACHING",
            WINDOW_W / 2 - textWidth(hdc, "WARNING: BOSS APPROACHING", 32) / 2,
            WINDOW_H / 2 - 20, 32, RGB(255, 50, 50));
    }

    if (game.msgTimer > 0 && game.msgText[0] != '\0') {
        char banner[64];
        snprintf(banner, sizeof(banner), "ABILITY! %s", game.msgText);
        int bw = textWidth(hdc, banner, 26);
        drawText(hdc, banner, WINDOW_W / 2 - bw / 2, WINDOW_H / 2 - 60, 26,
            RGB(0, 255, 180));
    }

    if (game.bannerTimer > 0 && game.bannerText[0] != '\0') {
        int bw = textWidth(hdc, game.bannerText, 34);
        drawText(hdc, game.bannerText, WINDOW_W / 2 - bw / 2, WINDOW_H / 2 - 130, 34,
            game.bannerTimer > 30 ? RGB(255, 255, 0) : RGB(0, 255, 255));
    }
}

void drawMenu(HDC hdc) {
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

void drawGameOver(HDC hdc) {
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

void drawWin(HDC hdc) {
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

void drawPause(HDC hdc) {
    fillRect(hdc, 0, 0, WINDOW_W, WINDOW_H, RGB(6, 6, 18));
    drawText(hdc, "PAUSED", WINDOW_W / 2 - textWidth(hdc, "PAUSED", 48) / 2, 280, 48, RGB(0, 255, 255));
    drawText(hdc, "ESC TO RESUME", WINDOW_W / 2 - textWidth(hdc, "ESC TO RESUME", 18) / 2, 360, 18, RGB(150, 200, 255));
    drawText(hdc, "Q TO QUIT TO MENU", WINDOW_W / 2 - textWidth(hdc, "Q TO QUIT TO MENU", 16) / 2, 390, 16, RGB(150, 150, 170));
}

void drawConsole(HDC hdc) {
    if (console.state == CONS_CLOSED) return;
    int conH = 200;
    int y0 = WINDOW_H - conH;

    RECT rc = { 0, y0, WINDOW_W, WINDOW_H };
    FillRect(hdc, &rc, getBrush(RGB(0, 0, 0)));

    HPEN oldPen = (HPEN)SelectObject(hdc, getPen(RGB(0, 255, 0), 2));
    MoveToEx(hdc, 0, y0, NULL);
    LineTo(hdc, WINDOW_W, y0);
    SelectObject(hdc, oldPen);

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

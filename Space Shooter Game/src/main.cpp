#include "types.h"
#include "sound.h"
#include "game.h"
#include "combat.h"
#include "console.h"
#include "drawing.h"

#include <ctime>

char soundDir[MAX_PATH] = "";

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

    // Load fonts from Fonts folder (BUG 12 fix: bounds checking)
    char modulePath[MAX_PATH];
    GetModuleFileNameA(NULL, modulePath, MAX_PATH);
    char* slash2 = strrchr(modulePath, '\\');
    if (slash2) {
        *slash2 = '\0';
        size_t dirLen = strlen(modulePath);

        // sounds
        if (dirLen + 8 < MAX_PATH) {
            snprintf(soundDir, MAX_PATH, "%s\\sounds\\", modulePath);
        }
        // Font 1
        if (dirLen + 24 < MAX_PATH) {
            char fontPath[MAX_PATH];
            snprintf(fontPath, sizeof(fontPath), "%s\\Fonts\\BM space.TTF", modulePath);
            AddFontResourceExA(fontPath, FR_PRIVATE, 0);
        }
        // Font 2
        if (dirLen + 25 < MAX_PATH) {
            char fontPath[MAX_PATH];
            snprintf(fontPath, sizeof(fontPath), "%s\\Fonts\\8-BIT WONDER.TTF", modulePath);
            AddFontResourceExA(fontPath, FR_PRIVATE, 0);
        }
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

        // Pace rendering to 60 FPS
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

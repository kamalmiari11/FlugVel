#pragma once
#include "Screen.h"

class CaptivePortal;

class GamesScreen : public Screen {
public:
    GamesScreen(TFT_eSPI* display, CaptivePortal* portal);

    void init() override;
    void update() override;
    void draw() override;
    void onEncoderUp() override;
    void onEncoderDown() override;
    void onButtonPress() override;
    const char* getName() const override { return "Games"; }
    ScreenId id() const override { return ScreenId::Games; }
    void getActionLegend(String &line1, String &line2) const override;

    // The game table lives in the .cpp, but the config page needs to list
    // the games by name to offer a toggle per game - same reasoning as
    // ScreenRegistry: one table that everything else reads.
    static int         entryCount();
    static const char* entryName(int index);
    static bool        entryPlayable(int index);

private:
    CaptivePortal* _portal;

    // ---- Game list ----
    // Ideas captured here so they're not lost even before they're built -
    // see GAME_ENTRIES in the .cpp for the actual list (name + playable
    // flag + one-line description of how each would use the knob/button).
    // Only Flappy Plane is implemented so far; the rest show "Coming Soon"
    // when selected instead of launching anything.
    enum GameScreenState {
        STATE_MENU,
        STATE_FLAPPY_PLAYING, STATE_FLAPPY_GAMEOVER,
        STATE_PADDLE_PLAYING, STATE_PADDLE_GAMEOVER
    };
    GameScreenState _state;

    int _selectedGame;
    int _lastDrawnSelectedGame;
    bool _needsMenuRedraw;

    void drawMenu(bool fullRepaint);

    // ---- Flappy Plane ----
    // Single-button game: the button flaps (pushes the plane upward
    // against gravity), pipes auto-scroll right-to-left. The knob isn't
    // needed for play - turning it while playing or on the game-over
    // screen backs out to the game list instead, since KO_BUTTON is fully
    // dedicated to flap/restart.
    static const unsigned long FLAPPY_FRAME_MS = 40; // ~25fps
    static const int FLAPPY_PIPE_COUNT = 3;
    static const int FLAPPY_PIPE_GAP = 70;
    static const int FLAPPY_PIPE_WIDTH = 24;
    static const int FLAPPY_PIPE_SPACING = 110;
    static const int FLAPPY_PLANE_SIZE = 12;

    // The playable area's bottom bound leaves room for the persistent
    // bottom action-legend bar (this screen keeps it, unlike PlaneTracker -
    // see GamesScreen::getActionLegend()) so pipes/ground/collision never
    // run underneath it. Matches ActionLegend's own height.
    static const int FLAPPY_PLAY_BOTTOM_MARGIN = 30;

    float _flappyPlaneY;
    float _flappyPlaneVelocity;
    float _flappyPipeX[FLAPPY_PIPE_COUNT];
    int _flappyPipeGapY[FLAPPY_PIPE_COUNT];
    bool _flappyPipeScored[FLAPPY_PIPE_COUNT];
    int _flappyScore;
    int _flappyBestScore; // synced from/to CaptivePortal (EEPROM-backed) on start/game-over
    unsigned long _lastFlappyFrame;

    // Dirty-rect drawing state: what was actually painted last frame, so
    // each frame only erases/redraws the small strip that changed instead
    // of clearing and repainting the entire play area - a full-area
    // fillRect() every ~40ms over SPI was the cause of the flicker and
    // input lag (the main loop, including button polling, was stalling on
    // that big transfer every frame).
    bool _flappyFirstFrame;
    float _flappyLastDrawnPlaneY;
    float _flappyLastDrawnPipeX[FLAPPY_PIPE_COUNT];
    int _flappyLastDrawnPipeGapY[FLAPPY_PIPE_COUNT];
    bool _flappyPipeJustRecycled[FLAPPY_PIPE_COUNT]; // forces one full-column redraw the frame a pipe respawns
    int _flappyLastDrawnScore;

    void startFlappy();
    void updateFlappy();
    void drawFlappyFrame();
    void drawFlappyPipeColumn(int index, int x, int gapCenterY, uint16_t color); // paints a full pipe column (top+bottom) at rest, used on first frame and on recycle
    void drawFlappyGameOver();
    void resetFlappyPipe(int index, int startX);

    // ---- Paddle Catch ----
    // Knob game: turn the encoder to slide the paddle left/right along the
    // bottom of the play area and catch falling plane icons. KO_BUTTON
    // isn't a gameplay control here (catching is automatic) - pressing it
    // while playing quits back to the game list; on the game-over screen it
    // restarts. One plane falls at a time; the fall speed ramps with score.
    // Three misses ends the run.
    static const unsigned long PADDLE_FRAME_MS = 33;   // ~30 fps
    static const int PADDLE_W = 46;
    static const int PADDLE_H = 6;
    static const int PADDLE_PLANE_SIZE = 12;
    static const int PADDLE_STEP = 14;                 // px moved per encoder detent
    static const int PADDLE_START_LIVES = 3;
    // Same bottom bound as Flappy - keeps play clear of the action-legend bar.
    static const int PADDLE_PLAY_BOTTOM_MARGIN = 30;

    float _paddleX;
    float _paddlePlaneX;
    float _paddlePlaneY;
    int   _paddleScore;
    int   _paddleLives;
    int   _paddleBest;          // synced from/to CaptivePortal (EEPROM) on start/game-over
    unsigned long _lastPaddleFrame;
    bool  _paddlePlaneJustSpawned;   // forces a full erase+redraw of the plane the frame it teleports

    // Dirty-rect drawing state - what was actually painted last frame, so
    // each frame only touches the pixels that changed (same approach as
    // the Flappy renderer above).
    bool  _paddleFirstFrame;
    float _paddleLastDrawnX;
    float _paddleLastDrawnPlaneX;
    float _paddleLastDrawnPlaneY;
    int   _paddleLastDrawnScore;
    int   _paddleLastDrawnLives;

    void startPaddle();
    void updatePaddle();
    void drawPaddleFrame();
    void drawPaddleGameOver();
    void spawnPaddlePlane();
};
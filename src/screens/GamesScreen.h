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
    // Flappy Plane, Paddle Catch, Simon Says and Air Traffic are all
    // implemented; the rest show "Coming Soon" when selected instead of
    // launching anything.
    enum GameScreenState {
        STATE_MENU,
        STATE_FLAPPY_PLAYING, STATE_FLAPPY_GAMEOVER,
        STATE_PADDLE_PLAYING, STATE_PADDLE_GAMEOVER,
        STATE_SIMON_SHOWING, STATE_SIMON_INPUT, STATE_SIMON_GAMEOVER,
        STATE_AIRTRAFFIC_PLAYING, STATE_AIRTRAFFIC_GAMEOVER
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

    // ---- Simon Says ----
    // Turn the knob to move a highlight between four pads; press to confirm
    // each step of a sequence. The device plays the sequence back first (no
    // input taken - just watch), then it's the player's turn to repeat it:
    // select and confirm each step in order. Get through it and the
    // sequence grows by one step and plays again. One wrong pad ends the
    // run; score is how many rounds were cleared.
    static const int SIMON_PAD_COUNT = 4;
    static const int SIMON_MAX_LEN = 32;            // generous ceiling - a real run reaching this is not expected
    static const unsigned long SIMON_LIT_MS = 500;  // how long each step stays lit during playback
    static const unsigned long SIMON_GAP_MS = 250;  // dark pause between playback steps
    // Same bottom bound as Flappy/Paddle - keeps play clear of the action-legend bar.
    static const int SIMON_PLAY_BOTTOM_MARGIN = 30;

    uint8_t _simonSequence[SIMON_MAX_LEN];
    int _simonLength;          // steps in the sequence this round
    int _simonShowIndex;       // which step playback is currently on
    bool _simonShowLit;        // within that step: the lit half vs the dark gap half
    unsigned long _simonPhaseStart;

    int _simonInputIndex;      // how many steps of this round the player has confirmed so far
    int _simonSelectedPad;     // cursor position, moved by the knob
    int _simonLastDrawnSelectedPad;
    int _simonLastDrawnInputIndex;

    int _simonScore;           // rounds cleared this run
    int _simonBest;            // synced from/to CaptivePortal (EEPROM-backed) on start/game-over

    void startSimon();
    void updateSimonShow();  // drives the auto-playback timer, called from update() during STATE_SIMON_SHOWING
    void drawSimonBoard();   // full paint of all four idle pads + status text - once per phase entry, not every loop
    void drawSimonPad(int index, bool lit, bool selected);
    void drawSimonStatus(const String &text);
    void drawSimonGameOver();

    // ---- Air Traffic ----
    // Planes drift in from the right edge, one per lane, toward the runway
    // at the left. Turn the knob to cycle the selection between whichever
    // lanes currently have a plane in them; press to clear (land) the
    // selected one for a point. A plane that reaches the runway unlanded
    // costs a life instead of ending the run outright - three lost planes
    // ends it. Speed ramps up with score, same idea as Paddle Catch.
    static const int AIRTRAFFIC_LANES = 4;
    static const unsigned long AIRTRAFFIC_FRAME_MS = 40;   // ~25fps, matches Flappy
    static const int AIRTRAFFIC_RUNWAY_X = 26;             // planes crossing this x, unlanded, are a miss
    static const int AIRTRAFFIC_PLANE_W = 16;
    static const int AIRTRAFFIC_PLANE_H = 10;
    static const unsigned long AIRTRAFFIC_SPAWN_MS = 1400; // how often a new plane is attempted
    static const int AIRTRAFFIC_START_LIVES = 3;
    // Same bottom bound convention as the other games.
    static const int AIRTRAFFIC_PLAY_BOTTOM_MARGIN = 30;

    bool  _airPlaneActive[AIRTRAFFIC_LANES];
    float _airPlaneX[AIRTRAFFIC_LANES];
    bool  _airLastDrawnActive[AIRTRAFFIC_LANES];
    float _airLastDrawnX[AIRTRAFFIC_LANES];

    int _airSelectedLane;           // -1 when no lane currently has a plane to select
    int _airLastDrawnSelectedLane;

    int _airScore;
    int _airLives;
    int _airBest;                   // synced from/to CaptivePortal (EEPROM-backed) on start/game-over
    int _airLastDrawnScore;
    int _airLastDrawnLives;

    unsigned long _lastAirFrame;
    unsigned long _airLastSpawnAt;
    bool _airFirstFrame;

    void startAirTraffic();
    void updateAirTraffic();
    void drawAirTrafficGameOver();
    void spawnAirTrafficPlane();
    // Next/previous lane (from `from`, wrapping) that currently has an
    // active plane - same skip-and-wrap idea as the free-function
    // gameStep() the game list uses to step over hidden entries. Returns
    // `from` unchanged if no lane has a plane right now.
    int airLaneStep(int from, int dir);
};
#include "GamesScreen.h"
#include "../config/Config.h"
#include "../MyColors.h"
#include "../network/CaptivePortal.h"
#include "../ui/Theme.h"
#include "../ui/UiChrome.h"

// ---- Game ideas, saved here so they survive even before being built ----
// Only "playable" entries actually launch something on press; the rest
// show a "Soon" tag in the list and do nothing when selected. Each comment
// captures how the game would use the hardware (knob = rotary encoder,
// button = KO_BUTTON) for whenever it gets implemented for real.
struct GameEntry {
    const char* name;
    bool playable;
};
static const GameEntry GAME_ENTRIES[] = {
    // Flappy Plane - IMPLEMENTED. Button flaps against gravity, pipes
    // auto-scroll. Doesn't need the knob at all during play.
    { "Flappy Plane", true },

    // Paddle Catch - IMPLEMENTED. Turn the knob to slide a paddle
    // left/right along the bottom of the play area and catch falling plane
    // icons. Difficulty ramps by speeding up the fall rate; three misses
    // ends the run.
    { "Paddle Catch", true },

    // Reaction Timer - screen shows "Wait..." then flips to "GO!" after a
    // random delay; press the button as fast as possible and it shows the
    // reaction time in ms, with a best-time high score.
    { "Reaction Timer", false },

    // Simon Says - a growing sequence of positions/colors flashes on
    // screen; turn the knob to highlight a position and press to confirm
    // each step, repeating the sequence back correctly to advance.
    { "Simon Says", false },
};
static const int GAME_COUNT = sizeof(GAME_ENTRIES) / sizeof(GAME_ENTRIES[0]);
static const int FLAPPY_INDEX = 0;
static const int PADDLE_INDEX = 1;

// Height of the persistent top status bar (Header::headerHeight). Nothing
// on this screen may paint over it - the Header is drawn by ScreenManager
// and only repaints itself when its own content changes, so a full-panel
// fillScreen() here would leave it blank until the next minute tick.
static const int HEADER_H = 20;

GamesScreen::GamesScreen(TFT_eSPI* display, CaptivePortal* portal)
    : Screen(display), _portal(portal), _state(STATE_MENU), _selectedGame(0),
      _lastDrawnSelectedGame(-1), _needsMenuRedraw(true),
      _flappyPlaneY(0), _flappyPlaneVelocity(0), _flappyScore(0), _flappyBestScore(0),
      _lastFlappyFrame(0), _flappyFirstFrame(true), _flappyLastDrawnPlaneY(0),
      _flappyLastDrawnScore(-1),
      _paddleX(0), _paddlePlaneX(0), _paddlePlaneY(0), _paddleScore(0),
      _paddleLives(0), _paddleBest(0), _lastPaddleFrame(0), _paddlePlaneJustSpawned(false),
      _paddleFirstFrame(true), _paddleLastDrawnX(0), _paddleLastDrawnPlaneX(0),
      _paddleLastDrawnPlaneY(0), _paddleLastDrawnScore(-1), _paddleLastDrawnLives(-1)
{
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        _flappyPipeX[i] = 0;
        _flappyPipeGapY[i] = 0;
        _flappyPipeScored[i] = false;
        _flappyLastDrawnPipeX[i] = 0;
        _flappyLastDrawnPipeGapY[i] = 0;
        _flappyPipeJustRecycled[i] = false;
    }
}

// Whether a game appears in the list at all. One bit per table index; an
// all-zero mask would leave nothing to select, so that is read as "show
// everything" rather than as a deliberate choice.
static bool gameVisible(int index) {
    uint8_t mask = ConfigStore::get().gamesMask;
    if (mask == 0) return true;
    return (mask >> index) & 1;
}

// Row position of a game in the drawn list, skipping hidden entries, or -1
// if it is hidden itself. The selection stays a table index throughout -
// only the y it lands at depends on what is visible.
static int gameRow(int index) {
    if (!gameVisible(index)) return -1;
    int row = 0;
    for (int i = 0; i < index; i++) if (gameVisible(i)) row++;
    return row;
}

// Next/previous visible table index, wrapping. Returns `from` unchanged if
// nothing else is visible.
static int gameStep(int from, int dir) {
    for (int k = 1; k <= GAME_COUNT; k++) {
        int i = ((from + dir * k) % GAME_COUNT + GAME_COUNT) % GAME_COUNT;
        if (gameVisible(i)) return i;
    }
    return from;
}

int GamesScreen::entryCount() { return GAME_COUNT; }

const char* GamesScreen::entryName(int index) {
    if (index < 0 || index >= GAME_COUNT) return "";
    return GAME_ENTRIES[index].name;
}

bool GamesScreen::entryPlayable(int index) {
    if (index < 0 || index >= GAME_COUNT) return false;
    return GAME_ENTRIES[index].playable;
}

void GamesScreen::init() {
    // The mask can change while this screen is off-cycle, so the remembered
    // selection may now be pointing at a hidden game.
    if (!gameVisible(_selectedGame)) _selectedGame = gameStep(_selectedGame, +1);

    Serial.println("[GamesScreen] Initialized");
    _state = STATE_MENU;
    _selectedGame = 0;
    _lastDrawnSelectedGame = -1;
    _needsMenuRedraw = true;
}

void GamesScreen::update() {
    if (_state == STATE_FLAPPY_PLAYING) {
        updateFlappy();
    } else if (_state == STATE_PADDLE_PLAYING) {
        updatePaddle();
    }
}

void GamesScreen::draw() {
    switch (_state) {
        case STATE_MENU:
            if (_needsMenuRedraw) {
                drawMenu(true);
                _needsMenuRedraw = false;
            } else if (_selectedGame != _lastDrawnSelectedGame) {
                drawMenu(false);
            }
            break;

        case STATE_FLAPPY_PLAYING:
            // Actual frame painting happens from updateFlappy() (see its
            // comment) so physics and drawing stay in lockstep at exactly
            // FLAPPY_FRAME_MS apart - nothing to do here.
            break;

        case STATE_FLAPPY_GAMEOVER:
            // Painted once, right when the game ends - nothing needs
            // repainting every loop on this static screen.
            break;

        case STATE_PADDLE_PLAYING:
            // Frame painting happens from updatePaddle(), same as Flappy.
            break;

        case STATE_PADDLE_GAMEOVER:
            // Painted once when the run ends.
            break;
    }
}

void GamesScreen::drawMenu(bool fullRepaint) {
    const Theme &theme = ThemeManager::current();

    if (fullRepaint) {
        tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);
    }

    // Full-width rows, matching the Settings menu: left accent bar on the
    // selected row, a right column showing "BEST n >" for playable games
    // or a "SOON" chip for the rest.
    const int rowX = 6;
    const int rowW = tft->width() - 12;
    const int rowH = 36;
    const int y0   = 44;

    for (int i = 0; i < GAME_COUNT; i++) {
        int row = gameRow(i);
        if (row < 0) continue;   // hidden by the config
        if (!fullRepaint && i != _selectedGame && i != _lastDrawnSelectedGame) continue;

        int top = y0 + row * rowH;
        bool selected = (i == _selectedGame);
        bool playable = GAME_ENTRIES[i].playable;

        tft->fillRect(rowX, top, rowW, rowH, selected ? theme.selectBg : theme.bg);
        if (selected)
            tft->fillRect(rowX, top, 4, rowH, theme.accent);

        tft->setTextSize(2);
        tft->setTextDatum(TL_DATUM);
        tft->setTextColor(selected ? theme.selectFg : (playable ? theme.fg : theme.fgDim));
        tft->setCursor(rowX + 12, top + (rowH - 16) / 2);
        tft->print(GAME_ENTRIES[i].name);

        if (playable) {
            int best = (i == FLAPPY_INDEX) ? _portal->getFlappyBestScore()
                     : (i == PADDLE_INDEX) ? _portal->getPaddleCatchBestScore()
                     : 0;
            tft->setTextSize(1);
            tft->setTextDatum(MR_DATUM);
            tft->setTextColor(selected ? theme.selectFg : theme.fgDim);
            tft->drawString("BEST " + String(best) + "  >", rowX + rowW - 8, top + rowH / 2);
            tft->setTextDatum(TL_DATUM);
        } else {
            const int chipW = 46, chipH = 15;
            int chipX = rowX + rowW - 8 - chipW;
            int chipY = top + (rowH - chipH) / 2;
            uint16_t chipCol = selected ? theme.selectFg : theme.rule;
            tft->drawRect(chipX, chipY, chipW, chipH, chipCol);
            tft->setTextSize(1);
            tft->setTextDatum(MC_DATUM);
            tft->setTextColor(chipCol);
            tft->drawString("SOON", chipX + chipW / 2, chipY + chipH / 2);
            tft->setTextDatum(TL_DATUM);
        }
    }
    _lastDrawnSelectedGame = _selectedGame;
}

void GamesScreen::onEncoderUp() {
    switch (_state) {
        case STATE_MENU:
            _selectedGame = gameStep(_selectedGame, -1);
            break;

        case STATE_PADDLE_PLAYING: {
            // Knob IS the control in Paddle Catch - slide right.
            _paddleX += PADDLE_STEP;
            float maxX = tft->width() - PADDLE_W / 2.0f;
            if (_paddleX > maxX) _paddleX = maxX;
            break;
        }

        default:
            // Flappy play/game-over and Paddle game-over: the knob backs
            // out to the game list (KO_BUTTON owns flap/restart there).
            _state = STATE_MENU;
            _needsMenuRedraw = true;
            break;
    }
}

void GamesScreen::onEncoderDown() {
    switch (_state) {
        case STATE_MENU:
            _selectedGame = gameStep(_selectedGame, +1);
            break;

        case STATE_PADDLE_PLAYING: {
            _paddleX -= PADDLE_STEP;
            float minX = PADDLE_W / 2.0f;
            if (_paddleX < minX) _paddleX = minX;
            break;
        }

        default:
            _state = STATE_MENU;
            _needsMenuRedraw = true;
            break;
    }
}

void GamesScreen::onButtonPress() {
    switch (_state) {
        case STATE_MENU:
            if (GAME_ENTRIES[_selectedGame].playable) {
                if (_selectedGame == FLAPPY_INDEX) {
                    startFlappy();
                    _state = STATE_FLAPPY_PLAYING;
                } else if (_selectedGame == PADDLE_INDEX) {
                    startPaddle();
                    _state = STATE_PADDLE_PLAYING;
                }
            }
            // Non-playable entries: no-op - their "Soon" tag already
            // explains why nothing happens.
            break;

        case STATE_FLAPPY_PLAYING:
            _flappyPlaneVelocity = -4.5f; // flap impulse
            break;

        case STATE_FLAPPY_GAMEOVER:
            startFlappy();
            _state = STATE_FLAPPY_PLAYING;
            break;

        case STATE_PADDLE_PLAYING:
            // Catching is automatic, so the button has no gameplay role -
            // use it to quit back to the list.
            _state = STATE_MENU;
            _needsMenuRedraw = true;
            break;

        case STATE_PADDLE_GAMEOVER:
            startPaddle();
            _state = STATE_PADDLE_PLAYING;
            break;
    }
}

// ============ Flappy Plane ============

void GamesScreen::resetFlappyPipe(int index, int startX) {
    int top = 20 + 10; // just below the header, with a little breathing room
    int bottom = (tft->height() - FLAPPY_PLAY_BOTTOM_MARGIN) - 10;
    int minGapCenter = top + FLAPPY_PIPE_GAP / 2 + 10;
    int maxGapCenter = bottom - FLAPPY_PIPE_GAP / 2 - 10;
    if (maxGapCenter < minGapCenter) maxGapCenter = minGapCenter; // tiny-screen safety

    _flappyPipeX[index] = startX;
    _flappyPipeGapY[index] = random(minGapCenter, maxGapCenter + 1);
    _flappyPipeScored[index] = false;
}

void GamesScreen::startFlappy() {
    _flappyPlaneY = (tft->height() - FLAPPY_PLAY_BOTTOM_MARGIN) / 2.0f;
    _flappyPlaneVelocity = 0;
    _flappyScore = 0;
    _flappyBestScore = _portal->getFlappyBestScore();

    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        resetFlappyPipe(i, tft->width() + i * FLAPPY_PIPE_SPACING);
        _flappyPipeJustRecycled[i] = false;
    }

    _lastFlappyFrame = millis();
    _flappyFirstFrame = true; // forces one full clear + full paint on the very next drawFlappyFrame()
}

void GamesScreen::updateFlappy() {
    unsigned long now = millis();
    if (now - _lastFlappyFrame < FLAPPY_FRAME_MS) return;
    _lastFlappyFrame = now;

    const float GRAVITY = 0.35f;
    const float MAX_FALL_SPEED = 6.0f;
    // Difficulty ramp: starts at 2.5px/frame, gains 0.15 per point scored,
    // capped at 5.5 so it stays winnable instead of becoming unfair.
    float PIPE_SPEED = 2.5f + (_flappyScore * 0.15f);
    if (PIPE_SPEED > 5.5f) PIPE_SPEED = 5.5f;
    const int PLAY_TOP = 20; // just below the header
    const int planeX = 50;
    const int half = FLAPPY_PLANE_SIZE / 2;

    _flappyPlaneVelocity += GRAVITY;
    if (_flappyPlaneVelocity > MAX_FALL_SPEED) _flappyPlaneVelocity = MAX_FALL_SPEED;
    _flappyPlaneY += _flappyPlaneVelocity;

    bool hitSomething = false;

    // Ceiling/floor
    if (_flappyPlaneY - half < PLAY_TOP || _flappyPlaneY + half > (tft->height() - FLAPPY_PLAY_BOTTOM_MARGIN)) {
        hitSomething = true;
    }

    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        _flappyPipeX[i] -= PIPE_SPEED;

        // Recycle pipes that have scrolled fully off the left edge,
        // reappearing past the rightmost pipe with a fresh random gap.
        if (_flappyPipeX[i] + FLAPPY_PIPE_WIDTH < 0) {
            float rightmostX = _flappyPipeX[0];
            for (int j = 1; j < FLAPPY_PIPE_COUNT; j++) {
                if (_flappyPipeX[j] > rightmostX) rightmostX = _flappyPipeX[j];
            }
            resetFlappyPipe(i, (int)(rightmostX + FLAPPY_PIPE_SPACING));
            _flappyPipeJustRecycled[i] = true; // tells drawFlappyFrame() to do one full-column repaint for this pipe
            continue;
        }

        // Score once the plane's x has fully passed this pipe (right edge
        // of the pipe behind the plane's right edge) - using planeX - half
        // here scored too early, while the plane still visually overlapped
        // the column instead of having actually cleared it.
        if (!_flappyPipeScored[i] && _flappyPipeX[i] + FLAPPY_PIPE_WIDTH < planeX + half) {
            _flappyPipeScored[i] = true;
            _flappyScore++;
        }

        // Collision: plane's bounding box against this pipe's column,
        // split into a top obstacle and a bottom obstacle around the gap.
        bool withinPipeX = (planeX + half > _flappyPipeX[i]) && (planeX - half < _flappyPipeX[i] + FLAPPY_PIPE_WIDTH);
        if (withinPipeX) {
            int gapTop = _flappyPipeGapY[i] - FLAPPY_PIPE_GAP / 2;
            int gapBottom = _flappyPipeGapY[i] + FLAPPY_PIPE_GAP / 2;
            if (_flappyPlaneY - half < gapTop || _flappyPlaneY + half > gapBottom) {
                hitSomething = true;
            }
        }
    }

    if (hitSomething) {
        if (_flappyScore > _flappyBestScore) {
            _flappyBestScore = _flappyScore;
            _portal->setFlappyBestScore(_flappyBestScore);
            _portal->saveLocationEEPROM();
        }
        _state = STATE_FLAPPY_GAMEOVER;
        drawFlappyGameOver();
        return;
    }

    drawFlappyFrame();
}

// Paints (or erases, if color is MY_BLACK) one full pipe column - the top
// obstacle from just below the header down to the gap, and the bottom
// obstacle from the gap down to the floor. Used for the very first frame
// and whenever a pipe respawns (a teleport, not a scroll, so there's no
// "old edge / new edge" to diff against).
void GamesScreen::drawFlappyPipeColumn(int index, int x, int gapCenterY, uint16_t color) {
    (void)index; // not needed for drawing itself, kept for call-site clarity
    const int PLAY_TOP = 20;
    int gapTop = gapCenterY - FLAPPY_PIPE_GAP / 2;
    int gapBottom = gapCenterY + FLAPPY_PIPE_GAP / 2;

    tft->fillRect(x, PLAY_TOP, FLAPPY_PIPE_WIDTH, gapTop - PLAY_TOP, color);
    tft->fillRect(x, gapBottom, FLAPPY_PIPE_WIDTH, (tft->height() - FLAPPY_PLAY_BOTTOM_MARGIN) - gapBottom, color);
}

// Dirty-rect frame update: only touches the pixels that actually changed
// since the last frame - a thin scrolling strip per pipe edge, the small
// plane square, and the score digits when they change - instead of
// clearing and repainting the whole play area every ~40ms. That full-area
// clear was the actual cause of the flicker and the delayed button
// response (the main loop, including input polling, was stalling on the
// large SPI transfer every single frame).
void GamesScreen::drawFlappyFrame() {
    const Theme &theme = ThemeManager::current();
    const int PLAY_TOP = 20;
    const int planeX = 50;
    const int half = FLAPPY_PLANE_SIZE / 2;

    if (_flappyFirstFrame) {
        tft->fillRect(0, PLAY_TOP, tft->width(), (tft->height() - FLAPPY_PLAY_BOTTOM_MARGIN) - PLAY_TOP, theme.bg);

        for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
            drawFlappyPipeColumn(i, (int)_flappyPipeX[i], _flappyPipeGapY[i], theme.accent2);
            _flappyLastDrawnPipeX[i] = _flappyPipeX[i];
            _flappyLastDrawnPipeGapY[i] = _flappyPipeGapY[i];
        }

        tft->fillRect(planeX - half, (int)_flappyPlaneY - half, FLAPPY_PLANE_SIZE, FLAPPY_PLANE_SIZE, theme.accent);
        _flappyLastDrawnPlaneY = _flappyPlaneY;

        tft->setTextSize(2);
        tft->setTextColor(theme.fg);
        tft->setCursor(10, PLAY_TOP + 4);
        tft->print(_flappyScore);
        _flappyLastDrawnScore = _flappyScore;

        _flappyFirstFrame = false;
        return;
    }

    // ---- Pipes ----
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        if (_flappyPipeJustRecycled[i]) {
            // Teleported to a new x with a new gap - erase the old column
            // fully, paint the new one fully.
            drawFlappyPipeColumn(i, (int)_flappyLastDrawnPipeX[i], _flappyLastDrawnPipeGapY[i], theme.bg);
            drawFlappyPipeColumn(i, (int)_flappyPipeX[i], _flappyPipeGapY[i], theme.accent2);
            _flappyLastDrawnPipeX[i] = _flappyPipeX[i];
            _flappyLastDrawnPipeGapY[i] = _flappyPipeGapY[i];
            _flappyPipeJustRecycled[i] = false;
            continue;
        }

        int oldX = (int)_flappyLastDrawnPipeX[i];
        int newX = (int)_flappyPipeX[i];
        int delta = oldX - newX; // pipes only move left, so this is normally positive
        if (delta <= 0) continue; // hasn't moved a full pixel yet this frame

        int gapTop = _flappyPipeGapY[i] - FLAPPY_PIPE_GAP / 2;
        int gapBottom = _flappyPipeGapY[i] + FLAPPY_PIPE_GAP / 2;

        // Newly-covered strip on the left edge (background -> pipe)
        tft->fillRect(newX, PLAY_TOP, delta, gapTop - PLAY_TOP, theme.accent2);
        tft->fillRect(newX, gapBottom, delta, (tft->height() - FLAPPY_PLAY_BOTTOM_MARGIN) - gapBottom, theme.accent2);

        // Newly-exposed strip on the right edge (pipe -> background)
        tft->fillRect(newX + FLAPPY_PIPE_WIDTH, PLAY_TOP, delta, gapTop - PLAY_TOP, theme.bg);
        tft->fillRect(newX + FLAPPY_PIPE_WIDTH, gapBottom, delta, (tft->height() - FLAPPY_PLAY_BOTTOM_MARGIN) - gapBottom, theme.bg);

        _flappyLastDrawnPipeX[i] = _flappyPipeX[i];
    }

    // ---- Plane ---- (x is fixed, only y moves - small enough to just erase+redraw)
    int oldY = (int)_flappyLastDrawnPlaneY;
    int newY = (int)_flappyPlaneY;
    if (oldY != newY) {
        tft->fillRect(planeX - half, oldY - half, FLAPPY_PLANE_SIZE, FLAPPY_PLANE_SIZE, theme.bg);
        tft->fillRect(planeX - half, newY - half, FLAPPY_PLANE_SIZE, FLAPPY_PLANE_SIZE, theme.accent);
        _flappyLastDrawnPlaneY = _flappyPlaneY;
    }

    // ---- Score ---- always redrawn last, unconditionally, so nothing
    // drawn above (pipes scrolling through, the plane) can ever end up
    // painted over it - and the erase box is wide enough for double-digit
    // scores so a stray "1" doesn't get left behind when going 9 -> 10.
    tft->fillRect(8, PLAY_TOP + 2, 50, 20, theme.bg);
    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->setCursor(10, PLAY_TOP + 4);
    tft->print(_flappyScore);
    _flappyLastDrawnScore = _flappyScore;
}

void GamesScreen::drawFlappyGameOver() {
    const Theme &theme = ThemeManager::current();
    tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);

    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(3);
    tft->setTextColor(theme.danger);
    tft->drawString("Game Over", 160, 90);

    char scoreLabel[24];
    snprintf(scoreLabel, sizeof(scoreLabel), "Score: %d", _flappyScore);
    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString(scoreLabel, 160, 140);

    char bestLabel[24];
    snprintf(bestLabel, sizeof(bestLabel), "Best: %d", _flappyBestScore);
    tft->setTextColor(theme.accent);
    tft->drawString(bestLabel, 160, 170);

    tft->setTextDatum(TL_DATUM); // restore default for other screens
}

// ============ Paddle Catch ============

void GamesScreen::spawnPaddlePlane() {
    const int half = PADDLE_PLANE_SIZE / 2;
    _paddlePlaneX = random(half + 4, tft->width() - half - 4);
    _paddlePlaneY = 20 + half;              // enter just below the header
    _paddlePlaneJustSpawned = true;
}

void GamesScreen::startPaddle() {
    _paddleX = tft->width() / 2.0f;
    _paddleScore = 0;
    _paddleLives = PADDLE_START_LIVES;
    _paddleBest = _portal->getPaddleCatchBestScore();

    spawnPaddlePlane();

    _lastPaddleFrame = millis();
    _paddleFirstFrame = true;               // forces one full clear + full paint next frame
    _paddleLastDrawnScore = -1;
    _paddleLastDrawnLives = -1;
}

void GamesScreen::updatePaddle() {
    unsigned long now = millis();
    if (now - _lastPaddleFrame < PADDLE_FRAME_MS) return;
    _lastPaddleFrame = now;

    // Difficulty ramp: 2.0 px/frame at score 0, +0.20 per catch, capped so
    // it stays playable.
    float fallSpeed = 2.0f + _paddleScore * 0.20f;
    if (fallSpeed > 10.0f) fallSpeed = 10.0f;
    _paddlePlaneY += fallSpeed;

    const int playBottom = tft->height() - PADDLE_PLAY_BOTTOM_MARGIN;
    const int paddleTopY = playBottom - PADDLE_H - 2;
    const int half = PADDLE_PLANE_SIZE / 2;

    if (_paddlePlaneY + half >= paddleTopY) {
        bool xOverlap = (_paddlePlaneX + half > _paddleX - PADDLE_W / 2.0f) &&
                        (_paddlePlaneX - half < _paddleX + PADDLE_W / 2.0f);

        if (xOverlap && _paddlePlaneY - half <= paddleTopY + PADDLE_H) {
            // Caught it.
            _paddleScore++;
            spawnPaddlePlane();
        } else if (_paddlePlaneY - half > playBottom) {
            // Fell past the paddle line - a miss.
            _paddleLives--;
            if (_paddleLives <= 0) {
                if (_paddleScore > _paddleBest) {
                    _paddleBest = _paddleScore;
                    _portal->setPaddleCatchBestScore(_paddleBest);
                    _portal->saveLocationEEPROM();
                }
                _state = STATE_PADDLE_GAMEOVER;
                drawPaddleGameOver();
                return;
            }
            spawnPaddlePlane();
        }
    }

    drawPaddleFrame();
}

// fillRect, but vertically clamped to [clipTop, clipBottom]. Used for the
// falling plane so a fast drop (or its erase box) can never paint outside
// the play field - over the header, the action-legend bar, etc.
static void fillRectClipped(TFT_eSPI *tft, int x, int y, int w, int h,
                            int clipTop, int clipBottom, uint16_t color) {
    int y0 = y < clipTop ? clipTop : y;
    int y1 = (y + h > clipBottom) ? clipBottom : (y + h);
    if (y1 > y0) tft->fillRect(x, y0, w, y1 - y0, color);
}

// Small helper: repaint the lives pips (top-right of the play area). Clears
// a fixed band wide enough for the starting life count so a lost pip is
// actually erased.
static void drawPaddleLives(TFT_eSPI *tft, int lives, int startLives, uint16_t bg, uint16_t pip) {
    const int PLAY_TOP = 20;
    const int pipR = 4;
    const int pitch = 14;
    int bandW = startLives * pitch + 6;
    tft->fillRect(tft->width() - bandW, PLAY_TOP + 2, bandW, 16, bg);
    for (int i = 0; i < lives; i++) {
        tft->fillCircle(tft->width() - 8 - i * pitch, PLAY_TOP + 10, pipR, pip);
    }
}

void GamesScreen::drawPaddleFrame() {
    const Theme &theme = ThemeManager::current();
    const int PLAY_TOP = 20;
    const int playBottom = tft->height() - PADDLE_PLAY_BOTTOM_MARGIN;
    const int paddleTopY = playBottom - PADDLE_H - 2;
    const int half = PADDLE_PLANE_SIZE / 2;
    const int pw = PADDLE_W;

    if (_paddleFirstFrame) {
        tft->fillRect(0, PLAY_TOP, tft->width(), playBottom - PLAY_TOP, theme.bg);

        tft->fillRect((int)_paddleX - pw / 2, paddleTopY, pw, PADDLE_H, theme.accent2);
        _paddleLastDrawnX = _paddleX;

        tft->fillRect((int)_paddlePlaneX - half, (int)_paddlePlaneY - half,
                      PADDLE_PLANE_SIZE, PADDLE_PLANE_SIZE, theme.accent);
        _paddleLastDrawnPlaneX = _paddlePlaneX;
        _paddleLastDrawnPlaneY = _paddlePlaneY;
        _paddlePlaneJustSpawned = false;

        tft->fillRect(8, PLAY_TOP + 2, 60, 20, theme.bg);
        tft->setTextSize(2);
        tft->setTextColor(theme.fg);
        tft->setCursor(10, PLAY_TOP + 4);
        tft->print(_paddleScore);
        _paddleLastDrawnScore = _paddleScore;

        drawPaddleLives(tft, _paddleLives, PADDLE_START_LIVES, theme.bg, theme.accent);
        _paddleLastDrawnLives = _paddleLives;

        _paddleFirstFrame = false;
        return;
    }

    // All field drawing below is vertically clipped to the play band, so a
    // fast-falling plane and its erase box stay strictly inside the field.
    const int clipTop = PLAY_TOP;
    const int clipBot = playBottom;

    bool respawned = _paddlePlaneJustSpawned;
    _paddlePlaneJustSpawned = false;

    // ---- Falling plane ---- (erase old box, draw new box, both clipped)
    {
        int oldX = (int)_paddleLastDrawnPlaneX;
        int oldY = (int)_paddleLastDrawnPlaneY;
        int newX = (int)_paddlePlaneX;
        int newY = (int)_paddlePlaneY;

        if (respawned || oldX != newX || oldY != newY) {
            fillRectClipped(tft, oldX - half, oldY - half, PADDLE_PLANE_SIZE, PADDLE_PLANE_SIZE,
                            clipTop, clipBot, theme.bg);
            fillRectClipped(tft, newX - half, newY - half, PADDLE_PLANE_SIZE, PADDLE_PLANE_SIZE,
                            clipTop, clipBot, theme.accent);
            _paddleLastDrawnPlaneX = _paddlePlaneX;
            _paddleLastDrawnPlaneY = _paddlePlaneY;
        }
    }

    // ---- Paddle ---- (y is fixed; erase the old strip when it moved)
    int oldPX = (int)_paddleLastDrawnX;
    int newPX = (int)_paddleX;
    if (oldPX != newPX) {
        tft->fillRect(oldPX - pw / 2, paddleTopY, pw, PADDLE_H, theme.bg);
    }
    // Repaint the bar in place when it moved, when the plane is down at the
    // paddle line (its erase box just clipped the bar), or right after a
    // plane despawned there. A 46x6 strip is cheap - this is what stops the
    // paddle "growing" or leaving a trailing segment.
    bool planeAtPaddle = ((int)_paddlePlaneY + half >= paddleTopY - 1);
    if (oldPX != newPX || planeAtPaddle || respawned) {
        tft->fillRect((int)_paddleX - pw / 2, paddleTopY, pw, PADDLE_H, theme.accent2);
        _paddleLastDrawnX = _paddleX;
    }

    // ---- HUD row (score + lives) ----
    // The plane spawns in this row, so repaint both while it overlaps them,
    // as well as on any real value change.
    bool planeInHudRow = ((int)_paddlePlaneY - half <= PLAY_TOP + 20);

    if (_paddleScore != _paddleLastDrawnScore || respawned || planeInHudRow) {
        tft->fillRect(8, PLAY_TOP + 2, 60, 20, theme.bg);
        tft->setTextSize(2);
        tft->setTextColor(theme.fg);
        tft->setCursor(10, PLAY_TOP + 4);
        tft->print(_paddleScore);
        _paddleLastDrawnScore = _paddleScore;
    }

    if (_paddleLives != _paddleLastDrawnLives || respawned || planeInHudRow) {
        drawPaddleLives(tft, _paddleLives, PADDLE_START_LIVES, theme.bg, theme.accent);
        _paddleLastDrawnLives = _paddleLives;
    }
}

void GamesScreen::drawPaddleGameOver() {
    const Theme &theme = ThemeManager::current();
    tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);

    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(3);
    tft->setTextColor(theme.danger);
    tft->drawString("Game Over", 160, 90);

    char scoreLabel[24];
    snprintf(scoreLabel, sizeof(scoreLabel), "Caught: %d", _paddleScore);
    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString(scoreLabel, 160, 140);

    char bestLabel[24];
    snprintf(bestLabel, sizeof(bestLabel), "Best: %d", _paddleBest);
    tft->setTextColor(theme.accent);
    tft->drawString(bestLabel, 160, 170);

    tft->setTextDatum(TL_DATUM); // restore default for other screens
}

void GamesScreen::getActionLegend(String &line1, String &line2) const {
    switch (_state) {
        case STATE_MENU:
            line1 = "^v SELECT GAME";
            line2 = "o PLAY";
            break;
        case STATE_FLAPPY_PLAYING:
            line1 = "^v BACK TO LIST";
            line2 = "o FLAP";
            break;
        case STATE_FLAPPY_GAMEOVER:
            line1 = "^v BACK TO LIST";
            line2 = "o RETRY";
            break;
        case STATE_PADDLE_PLAYING:
            line1 = "^v MOVE PADDLE";
            line2 = "o QUIT";
            break;
        case STATE_PADDLE_GAMEOVER:
            line1 = "^v BACK TO LIST";
            line2 = "o RETRY";
            break;
    }
}
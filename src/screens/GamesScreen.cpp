#include "GamesScreen.h"
#include "../config/Config.h"
#include "../MyColors.h"
#include "../network/CaptivePortal.h"
#include "../ui/Theme.h"
#include "../ui/UiChrome.h"
#include "../ui/PlaneSprite.h"

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

    // Simon Says - IMPLEMENTED. Turn the knob to move the highlight among
    // four pads; the device plays a growing sequence back first (watch
    // only), then it's the player's turn to repeat it - select and confirm
    // each step in order. One wrong pad ends the run.
    { "Simon Says", true },

    // Air Traffic - IMPLEMENTED. Planes drift in from the right, one per
    // lane; turn the knob to move a cursor between the four lanes and press
    // to land the plane in the selected one - but only once it has reached
    // the approach zone short of the runway. Landing early, or pressing on
    // an empty lane, costs points. A plane that reaches the runway unlanded
    // costs a life - three losses ends the run.
    { "Air Traffic", true },
};
static const int GAME_COUNT = sizeof(GAME_ENTRIES) / sizeof(GAME_ENTRIES[0]);
static const int FLAPPY_INDEX = 0;
static const int PADDLE_INDEX = 1;
static const int SIMON_INDEX = 3;
static const int AIRTRAFFIC_INDEX = 4;

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
      _paddleLastDrawnPlaneY(0), _paddleLastDrawnScore(-1), _paddleLastDrawnLives(-1),
      _simonLength(0), _simonShowIndex(0), _simonShowLit(false), _simonPhaseStart(0),
      _simonInputIndex(0), _simonSelectedPad(0), _simonLastDrawnSelectedPad(-1),
      _simonLastDrawnInputIndex(-1), _simonScore(0), _simonBest(0),
      _airSelectedLane(0), _airLastDrawnSelectedLane(-1),
      _airScore(0), _airLives(0), _airBest(0), _airLastDrawnScore(-1), _airLastDrawnLives(-1),
      _lastAirFrame(0), _airLastSpawnAt(0), _airFirstFrame(true),
      _airMsg(nullptr), _airMsgUntil(0), _airMsgDrawn(false)
{
    for (int i = 0; i < FLAPPY_PIPE_COUNT; i++) {
        _flappyPipeX[i] = 0;
        _flappyPipeGapY[i] = 0;
        _flappyPipeScored[i] = false;
        _flappyLastDrawnPipeX[i] = 0;
        _flappyLastDrawnPipeGapY[i] = 0;
        _flappyPipeJustRecycled[i] = false;
    }
    for (int i = 0; i < SIMON_MAX_LEN; i++) {
        _simonSequence[i] = 0;
    }
    for (int i = 0; i < AIRTRAFFIC_LANES; i++) {
        _airPlaneActive[i] = false;
        _airPlaneX[i] = 0;
        _airLastDrawnActive[i] = false;
        _airLastDrawnX[i] = 0;
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
    } else if (_state == STATE_SIMON_SHOWING) {
        updateSimonShow();
    } else if (_state == STATE_AIRTRAFFIC_PLAYING) {
        updateAirTraffic();
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

        case STATE_SIMON_SHOWING:
            // Playback painting happens from updateSimonShow() so the
            // lit/gap timing and the drawing stay in lockstep - nothing to
            // do here, same reasoning as Flappy/Paddle's PLAYING cases.
            break;

        case STATE_SIMON_INPUT:
            // No timer driving this state - it just waits on the knob,
            // which mutates _simonSelectedPad directly (onEncoderUp/Down).
            // Same poll-for-a-diff approach as STATE_MENU above: repaint
            // only the pad that actually moved. The status text ("STEP
            // n/len") changes on a confirm instead of a cursor move, so
            // onButtonPress draws that directly rather than through here.
            if (_simonSelectedPad != _simonLastDrawnSelectedPad) {
                if (_simonLastDrawnSelectedPad >= 0) {
                    drawSimonPad(_simonLastDrawnSelectedPad, false, false);
                }
                drawSimonPad(_simonSelectedPad, false, true);
                _simonLastDrawnSelectedPad = _simonSelectedPad;
            }
            break;

        case STATE_SIMON_GAMEOVER:
            // Painted once, right when the round is lost (see onButtonPress).
            break;

        case STATE_AIRTRAFFIC_PLAYING:
            // Frame painting happens from updateAirTraffic(), same as
            // Flappy/Paddle - nothing to do here.
            break;

        case STATE_AIRTRAFFIC_GAMEOVER:
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
                     : (i == SIMON_INDEX) ? _portal->getSimonBestScore()
                     : (i == AIRTRAFFIC_INDEX) ? _portal->getAirTrafficBestScore()
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

        case STATE_SIMON_SHOWING:
            // Ignored - the sequence is only a few hundred ms per step, so
            // a knob bump mid-playback doesn't yank the player out of it.
            break;

        case STATE_SIMON_INPUT:
            _simonSelectedPad = (_simonSelectedPad + 1) % SIMON_PAD_COUNT;
            break;

        case STATE_AIRTRAFFIC_PLAYING:
            _airSelectedLane = airLaneStep(_airSelectedLane, +1);
            break;

        default:
            // Flappy play/game-over, Paddle game-over, Simon game-over and
            // Air Traffic game-over: the knob backs out to the game list
            // (KO_BUTTON owns flap/restart/retry there).
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

        case STATE_SIMON_SHOWING:
            break;

        case STATE_SIMON_INPUT:
            _simonSelectedPad = (_simonSelectedPad - 1 + SIMON_PAD_COUNT) % SIMON_PAD_COUNT;
            break;

        case STATE_AIRTRAFFIC_PLAYING:
            _airSelectedLane = airLaneStep(_airSelectedLane, -1);
            break;

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
                } else if (_selectedGame == SIMON_INDEX) {
                    startSimon();
                    _state = STATE_SIMON_SHOWING;
                } else if (_selectedGame == AIRTRAFFIC_INDEX) {
                    startAirTraffic();
                    _state = STATE_AIRTRAFFIC_PLAYING;
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

        case STATE_SIMON_SHOWING:
            // Watch-only phase - no input is taken until playback finishes
            // and control passes to STATE_SIMON_INPUT.
            break;

        case STATE_SIMON_INPUT: {
            if (_simonSelectedPad == _simonSequence[_simonInputIndex]) {
                _simonInputIndex++;
                if (_simonInputIndex >= _simonLength) {
                    // Round cleared - grow the sequence by one step and play
                    // the longer sequence back from the start.
                    _simonScore++;
                    if (_simonLength < SIMON_MAX_LEN) {
                        _simonSequence[_simonLength] = random(0, SIMON_PAD_COUNT);
                        _simonLength++;
                    }
                    _simonShowIndex = 0;
                    _simonShowLit = false;
                    _simonPhaseStart = millis();
                    _state = STATE_SIMON_SHOWING;
                    drawSimonBoard();
                    drawSimonStatus("WATCH");
                } else {
                    drawSimonStatus("STEP " + String(_simonInputIndex + 1) + "/" + String(_simonLength));
                }
            } else {
                if (_simonScore > _simonBest) {
                    _simonBest = _simonScore;
                    _portal->setSimonBestScore(_simonBest);
                    _portal->saveLocationEEPROM();
                }
                _state = STATE_SIMON_GAMEOVER;
                drawSimonGameOver();
            }
            break;
        }

        case STATE_SIMON_GAMEOVER:
            startSimon();
            _state = STATE_SIMON_SHOWING;
            break;

        case STATE_AIRTRAFFIC_PLAYING: {
            // Three outcomes, and only one of them scores: a plane in the
            // selected lane that has reached the approach zone lands; a
            // plane still too far out is waved off; an empty lane is just a
            // wasted press. The two failures cost points (see the header for
            // why points and not a life), which is what makes mashing KO
            // strictly worse than watching and timing. The cursor stays
            // where the player left it in every case - the just-cleared
            // lane's own erase happens on the next updateAirTraffic() frame
            // tick via its dirty-rect diff.
            bool hasPlane = _airPlaneActive[_airSelectedLane];
            bool inZone = hasPlane &&
                          (_airPlaneX[_airSelectedLane] - AIRTRAFFIC_PLANE_W / 2) <= AIRTRAFFIC_APPROACH_X;

            if (inZone) {
                _airPlaneActive[_airSelectedLane] = false;
                _airScore++;
            } else {
                _airScore -= AIRTRAFFIC_BAD_PRESS_PENALTY;
                if (_airScore < 0) _airScore = 0;
                _airMsg = hasPlane ? "TOO EARLY" : "EMPTY LANE";
                _airMsgUntil = millis() + AIRTRAFFIC_MSG_MS;
                _airMsgDrawn = false;   // forces a repaint even if a note is already up
            }
            break;
        }

        case STATE_AIRTRAFFIC_GAMEOVER:
            startAirTraffic();
            _state = STATE_AIRTRAFFIC_PLAYING;
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
    const int half = FLAPPY_HITBOX / 2;   // collision box, deliberately smaller
                                          // than the drawn sprite - see the header

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

        // Facing east: the plane holds station while the pipes scroll in
        // from the right, so on screen it is flying right.
        PlaneSprite::drawSmallCentered(tft, planeX, (int)_flappyPlaneY, PlaneSprite::E, theme.accent);
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
        PlaneSprite::drawSmallCentered(tft, planeX, newY, PlaneSprite::E, theme.accent);

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
    const int half = PADDLE_CATCH_SIZE / 2;   // catch box, deliberately smaller
                                              // than the drawn sprite - see the header

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

// The falling plane, clipped to the same vertical band fillRectClipped()
// keeps the erase boxes inside, so a fast drop can never paint the sprite
// over the header or the action-legend bar. A bitmap can't be clipped by
// shortening a rect the way a fill can, so this uses a viewport instead -
// with vpDatum false, so the coordinates passed in stay panel-absolute.
static void drawPaddlePlaneClipped(TFT_eSPI *tft, int cx, int cy, uint16_t color,
                                   int clipTop, int clipBottom) {
    if (clipBottom <= clipTop) return;
    tft->setViewport(0, clipTop, tft->width(), clipBottom - clipTop, false);
    // South: these are falling straight down the screen.
    PlaneSprite::drawSmallCentered(tft, cx, cy, PlaneSprite::S, color);
    tft->resetViewport();
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

        PlaneSprite::drawSmallCentered(tft, (int)_paddlePlaneX, (int)_paddlePlaneY,
                                       PlaneSprite::S, theme.accent);
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
            drawPaddlePlaneClipped(tft, newX, newY, theme.accent, clipTop, clipBot);
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

// ============ Simon Says ============
// Layout constants for the 2x2 pad grid - kept file-local since nothing
// outside this section needs them. Sized/positioned to sit below the
// one-line status text and above the action-legend bar, matching the
// SIMON_PLAY_BOTTOM_MARGIN the header already reserves.
static const int SIMON_PAD_W = 130;
static const int SIMON_PAD_H = 70;
static const int SIMON_GRID_GAP = 10;
static const int SIMON_GRID_X = 25;
static const int SIMON_GRID_Y = 50;

void GamesScreen::drawSimonPad(int index, bool lit, bool selected) {
    const Theme &theme = ThemeManager::current();
    int col = index % 2;
    int row = index / 2;
    int x = SIMON_GRID_X + col * (SIMON_PAD_W + SIMON_GRID_GAP);
    int y = SIMON_GRID_Y + row * (SIMON_PAD_H + SIMON_GRID_GAP);

    // The palette has no distinct per-pad hues (see MyColors.h/Theme.h), so
    // pads are told apart by position and label, and state is told apart
    // by fill/border instead of color: lit (playback) uses the accent fill,
    // selected (input cursor) uses the selection colors with a doubled
    // border, idle uses the plain background and a thin rule border.
    uint16_t fill = lit ? theme.accent : (selected ? theme.selectBg : theme.bg);
    uint16_t border = lit ? theme.accent : (selected ? theme.accent2 : theme.rule);
    uint16_t labelCol = lit ? theme.bg : (selected ? theme.selectFg : theme.fgDim);

    tft->fillRect(x, y, SIMON_PAD_W, SIMON_PAD_H, fill);
    tft->drawRect(x, y, SIMON_PAD_W, SIMON_PAD_H, border);
    if (selected && !lit) {
        tft->drawRect(x + 3, y + 3, SIMON_PAD_W - 6, SIMON_PAD_H - 6, border);
    }

    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(3);
    tft->setTextColor(labelCol);
    char label[2] = { (char)('1' + index), '\0' };
    tft->drawString(label, x + SIMON_PAD_W / 2, y + SIMON_PAD_H / 2);
    tft->setTextDatum(TL_DATUM);
}

void GamesScreen::drawSimonStatus(const String &text) {
    const Theme &theme = ThemeManager::current();
    const int PLAY_TOP = 20;
    tft->fillRect(0, PLAY_TOP, tft->width(), SIMON_GRID_Y - PLAY_TOP, theme.bg);
    tft->setTextDatum(MC_DATUM);
    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString(text, tft->width() / 2, PLAY_TOP + (SIMON_GRID_Y - PLAY_TOP) / 2);
    tft->setTextDatum(TL_DATUM);
}

void GamesScreen::drawSimonBoard() {
    const Theme &theme = ThemeManager::current();
    tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);
    for (int i = 0; i < SIMON_PAD_COUNT; i++) {
        drawSimonPad(i, false, false);
    }
    _simonLastDrawnSelectedPad = -1;
}

void GamesScreen::startSimon() {
    for (int i = 0; i < SIMON_MAX_LEN; i++) _simonSequence[i] = 0;
    _simonSequence[0] = random(0, SIMON_PAD_COUNT);
    _simonLength = 1;
    _simonScore = 0;
    _simonBest = _portal->getSimonBestScore();

    _simonInputIndex = 0;
    _simonSelectedPad = 0;
    _simonLastDrawnSelectedPad = -1;
    _simonLastDrawnInputIndex = -1;

    _simonShowIndex = 0;
    _simonShowLit = false;
    _simonPhaseStart = millis();

    drawSimonBoard();
    drawSimonStatus("WATCH");
}

void GamesScreen::updateSimonShow() {
    unsigned long now = millis();
    unsigned long elapsed = now - _simonPhaseStart;

    if (!_simonShowLit) {
        // Dark gap before this step - once it's elapsed, light the pad.
        if (elapsed >= SIMON_GAP_MS) {
            drawSimonPad(_simonSequence[_simonShowIndex], true, false);
            _simonShowLit = true;
            _simonPhaseStart = now;
        }
    } else {
        // Lit step has had its time - turn it back off and either move on
        // to the next step or hand control to the player.
        if (elapsed >= SIMON_LIT_MS) {
            drawSimonPad(_simonSequence[_simonShowIndex], false, false);
            _simonShowLit = false;
            _simonShowIndex++;
            _simonPhaseStart = now;

            if (_simonShowIndex >= _simonLength) {
                _simonInputIndex = 0;
                _simonSelectedPad = 0;
                _simonLastDrawnSelectedPad = -1;
                _state = STATE_SIMON_INPUT;
                drawSimonPad(_simonSelectedPad, false, true);
                _simonLastDrawnSelectedPad = _simonSelectedPad;
                drawSimonStatus("STEP 1/" + String(_simonLength));
            }
        }
    }
}

void GamesScreen::drawSimonGameOver() {
    const Theme &theme = ThemeManager::current();
    tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);

    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(3);
    tft->setTextColor(theme.danger);
    tft->drawString("Game Over", 160, 90);

    char scoreLabel[24];
    snprintf(scoreLabel, sizeof(scoreLabel), "Rounds: %d", _simonScore);
    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString(scoreLabel, 160, 140);

    char bestLabel[24];
    snprintf(bestLabel, sizeof(bestLabel), "Best: %d", _simonBest);
    tft->setTextColor(theme.accent);
    tft->drawString(bestLabel, 160, 170);

    tft->setTextDatum(TL_DATUM); // restore default for other screens
}

// ============ Air Traffic ============
// AIRTRAFFIC_LANES horizontal lanes stacked between the header and the
// action-legend bar; each lane holds at most one plane at a time, which is
// what avoids needing any pairwise collision math - a plane only ever
// interacts with the runway line, never with another plane.

// Draws (or erases, if color == the background) one plane, centered on its
// lane. West-facing: these drift in from the right edge and fly left toward
// the runway, so that is the way the silhouette points. Erasing works by
// passing the background color - drawBitmap only paints the set bits, so it
// clears exactly the pixels the same sprite put down at that position and
// nothing around them. Takes its size as parameters rather than reading the
// class's private AIRTRAFFIC_PLANE_W/H constants directly, same reasoning as
// fillRectClipped()/drawPaddleLives() above.
static void drawAirPlane(TFT_eSPI *tft, int x, int laneCenterY, int w, int h, uint16_t color) {
    (void)w; (void)h; // the shared sprite is a fixed 16x16 - see ui/PlaneSprite
    PlaneSprite::drawSmallCentered(tft, x, laneCenterY, PlaneSprite::W, color);
}

// What color a plane is drawn in, which is the game's whole read-at-a-glance
// state: dim while it is still too far out to land, full brightness once it
// crosses the approach gate, and accent when it is the one the cursor is on.
// A player should be able to tell "can I press now?" from the sprite alone,
// without measuring it against the dashed line.
// Takes the two booleans rather than the geometry so it stays a plain file-
// static helper, same as the drawing helpers above - the caller is a member
// function and already has the private constants to hand.
static uint16_t airPlaneColor(const Theme &theme, bool landable, bool selected) {
    if (selected) return theme.accent;
    return landable ? theme.fg : theme.fgDim;
}

// The lane cursor: a small bar in the strip left of the runway line, which
// no plane ever reaches (they are removed AT the runway), so it can sit
// there permanently without ever colliding with one. This is what makes an
// empty selected lane visible - without it the player would have no idea
// where the cursor was until a plane happened to arrive under it.
static void drawAirLaneCursor(TFT_eSPI *tft, int laneCenterY, uint16_t color) {
    tft->fillRect(4, laneCenterY - 7, 5, 14, color);
}

void GamesScreen::spawnAirTrafficPlane() {
    int freeLanes[AIRTRAFFIC_LANES];
    int freeCount = 0;
    for (int i = 0; i < AIRTRAFFIC_LANES; i++) {
        if (!_airPlaneActive[i]) freeLanes[freeCount++] = i;
    }
    if (freeCount == 0) return; // every lane occupied - try again next spawn tick

    int lane = freeLanes[random(0, freeCount)];
    _airPlaneActive[lane] = true;
    _airPlaneX[lane] = tft->width() + AIRTRAFFIC_PLANE_W; // enters just off the right edge

    // Deliberately does NOT touch _airSelectedLane. Handing the cursor to
    // each new plane is what let the game be played without ever turning
    // the knob - see the header. Steering is the player's job.
}

void GamesScreen::startAirTraffic() {
    for (int i = 0; i < AIRTRAFFIC_LANES; i++) {
        _airPlaneActive[i] = false;
        _airPlaneX[i] = 0;
        _airLastDrawnActive[i] = false;
        _airLastDrawnX[i] = 0;
    }
    _airSelectedLane = 0;      // a lane cursor, always valid - see the header
    _airLastDrawnSelectedLane = -1;
    _airMsg = nullptr;
    _airMsgUntil = 0;
    _airMsgDrawn = false;
    _airScore = 0;
    _airLives = AIRTRAFFIC_START_LIVES;
    _airBest = _portal->getAirTrafficBestScore();
    _airLastDrawnScore = -1;
    _airLastDrawnLives = -1;

    _lastAirFrame = millis();
    _airLastSpawnAt = millis();
    _airFirstFrame = true; // forces one full clear + full paint next frame

    spawnAirTrafficPlane(); // one plane waiting right away rather than a dead first second
}

void GamesScreen::updateAirTraffic() {
    unsigned long now = millis();
    if (now - _lastAirFrame < AIRTRAFFIC_FRAME_MS) return;
    _lastAirFrame = now;

    const Theme &theme = ThemeManager::current();
    const int PLAY_TOP = 20;
    const int playBottom = tft->height() - AIRTRAFFIC_PLAY_BOTTOM_MARGIN;
    const int laneH = (playBottom - PLAY_TOP) / AIRTRAFFIC_LANES;

    // Difficulty ramp: 2.0px/frame at score 0, +0.1 per point, capped at
    // 5.0 - same idea as Paddle Catch's fall-speed ramp.
    float speed = 2.0f + (_airScore * 0.1f);
    if (speed > 5.0f) speed = 5.0f;

    // Spawns come closer together as the score climbs, so late game the
    // lanes actually fill up and the player has to choose which approach to
    // take first - the speed ramp alone only ever made one plane at a time
    // arrive sooner.
    unsigned long spawnEvery = AIRTRAFFIC_SPAWN_MS;
    unsigned long quicker = (unsigned long)_airScore * AIRTRAFFIC_SPAWN_STEP_MS;
    spawnEvery = (quicker >= spawnEvery - AIRTRAFFIC_SPAWN_MIN_MS)
                     ? AIRTRAFFIC_SPAWN_MIN_MS
                     : spawnEvery - quicker;

    if (now - _airLastSpawnAt >= spawnEvery) {
        _airLastSpawnAt = now;
        spawnAirTrafficPlane();
    }

    // ---- Movement + miss detection ----
    const int half = AIRTRAFFIC_PLANE_W / 2;
    for (int i = 0; i < AIRTRAFFIC_LANES; i++) {
        if (!_airPlaneActive[i]) continue;
        _airPlaneX[i] -= speed;

        if (_airPlaneX[i] - half <= AIRTRAFFIC_RUNWAY_X) {
            // Reached the runway unlanded - costs a life instead of ending
            // the run outright.
            _airPlaneActive[i] = false;
            _airLives--;
            // The cursor is the player's, so a miss does not move it - it
            // stays on the lane they were watching.
        }
    }

    if (_airLives <= 0) {
        if (_airScore > _airBest) {
            _airBest = _airScore;
            _portal->setAirTrafficBestScore(_airBest);
            _portal->saveLocationEEPROM();
        }
        _state = STATE_AIRTRAFFIC_GAMEOVER;
        drawAirTrafficGameOver();
        return;
    }

    // ---- Drawing ----
    // Dirty-rect, same approach as Flappy/Paddle: only the pixels that
    // actually changed get touched. Folded directly into this function
    // (rather than a separate drawAirTrafficFrame()) since the only moving
    // state is each lane's plane x - nothing else needs to call into the
    // drawing half on its own.
    if (_airFirstFrame) {
        tft->fillRect(0, PLAY_TOP, tft->width(), playBottom - PLAY_TOP, theme.bg);

        tft->drawFastVLine(AIRTRAFFIC_RUNWAY_X, PLAY_TOP, playBottom - PLAY_TOP, theme.rule);

        // The approach gate: everything left of this line is landable. Drawn
        // dashed so it reads as a threshold to cross rather than as another
        // hard edge like the runway line, and so the two are never confused
        // at a glance.
        for (int y = PLAY_TOP + 2; y < playBottom; y += 8) {
            tft->drawFastVLine(AIRTRAFFIC_APPROACH_X, y, 4, theme.rule);
        }

        for (int i = 1; i < AIRTRAFFIC_LANES; i++) {
            tft->drawFastHLine(0, PLAY_TOP + laneH * i, tft->width(), theme.rule);
        }

        for (int i = 0; i < AIRTRAFFIC_LANES; i++) {
            _airLastDrawnActive[i] = _airPlaneActive[i];
            _airLastDrawnX[i] = _airPlaneX[i];
            int laneCenterY = PLAY_TOP + laneH * i + laneH / 2;
            if (_airPlaneActive[i]) {
                bool landable = (_airPlaneX[i] - half) <= AIRTRAFFIC_APPROACH_X;
                drawAirPlane(tft, (int)_airPlaneX[i], laneCenterY, AIRTRAFFIC_PLANE_W, AIRTRAFFIC_PLANE_H,
                             airPlaneColor(theme, landable, i == _airSelectedLane));
            }
            if (i == _airSelectedLane) drawAirLaneCursor(tft, laneCenterY, theme.accent);
        }

        tft->fillRect(8, PLAY_TOP + 2, 60, 20, theme.bg);
        tft->setTextSize(2);
        tft->setTextColor(theme.fg);
        tft->setCursor(10, PLAY_TOP + 4);
        tft->print(_airScore);
        _airLastDrawnScore = _airScore;

        drawPaddleLives(tft, _airLives, AIRTRAFFIC_START_LIVES, theme.bg, theme.danger);
        _airLastDrawnLives = _airLives;

        _airLastDrawnSelectedLane = _airSelectedLane;
        _airFirstFrame = false;
        return;
    }

    for (int i = 0; i < AIRTRAFFIC_LANES; i++) {
        int laneCenterY = PLAY_TOP + laneH * i + laneH / 2;
        bool wasActive = _airLastDrawnActive[i];
        bool isActive = _airPlaneActive[i];
        bool wasSelected = (_airLastDrawnSelectedLane == i);
        bool isSelected = (_airSelectedLane == i);

        if (wasActive && (!isActive || (int)_airLastDrawnX[i] != (int)_airPlaneX[i] || wasSelected != isSelected)) {
            drawAirPlane(tft, (int)_airLastDrawnX[i], laneCenterY, AIRTRAFFIC_PLANE_W, AIRTRAFFIC_PLANE_H, theme.bg);
        }
        if (isActive) {
            bool landable = (_airPlaneX[i] - half) <= AIRTRAFFIC_APPROACH_X;
            drawAirPlane(tft, (int)_airPlaneX[i], laneCenterY, AIRTRAFFIC_PLANE_W, AIRTRAFFIC_PLANE_H,
                         airPlaneColor(theme, landable, isSelected));
        }

        // The cursor only moves when the knob moves it, so this repaints on
        // the frame the selection actually changed and never again.
        if (wasSelected != isSelected) {
            drawAirLaneCursor(tft, laneCenterY, isSelected ? theme.accent : theme.bg);
        }

        _airLastDrawnActive[i] = isActive;
        _airLastDrawnX[i] = _airPlaneX[i];
    }
    _airLastDrawnSelectedLane = _airSelectedLane;

    if (_airScore != _airLastDrawnScore) {
        tft->fillRect(8, PLAY_TOP + 2, 60, 20, theme.bg);
        tft->setTextSize(2);
        tft->setTextColor(theme.fg);
        tft->setCursor(10, PLAY_TOP + 4);
        tft->print(_airScore);
        _airLastDrawnScore = _airScore;
    }

    if (_airLives != _airLastDrawnLives) {
        drawPaddleLives(tft, _airLives, AIRTRAFFIC_START_LIVES, theme.bg, theme.danger);
        _airLastDrawnLives = _airLives;
    }

    // ---- Bad-press note ----
    // Sits in the gap between the score (top left) and the lives pips (top
    // right), above where lane 0's plane flies, so it never fights with
    // either. Painted once when it appears and erased once when it expires;
    // no per-frame cost while nothing is showing.
    {
        const int msgX = tft->width() / 2;
        const int msgY = PLAY_TOP + 6;
        bool msgActive = (_airMsg != nullptr) && (now < _airMsgUntil);

        if (msgActive && !_airMsgDrawn) {
            // Cleared first: a second bad press can replace the note while
            // it is still up, and the two strings are different widths, so
            // drawing straight over the old one would leave a tail behind.
            tft->fillRect(msgX - 50, msgY - 2, 100, 12, theme.bg);
            tft->setTextDatum(TC_DATUM);
            tft->setTextSize(1);
            tft->setTextColor(theme.danger, theme.bg);
            tft->drawString(_airMsg, msgX, msgY);
            tft->setTextDatum(TL_DATUM);
            _airMsgDrawn = true;
        } else if (!msgActive && _airMsgDrawn) {
            tft->fillRect(msgX - 50, msgY - 2, 100, 12, theme.bg);
            _airMsgDrawn = false;
            _airMsg = nullptr;
        }
    }
}

void GamesScreen::drawAirTrafficGameOver() {
    const Theme &theme = ThemeManager::current();
    tft->fillRect(0, HEADER_H, tft->width(), tft->height() - HEADER_H, theme.bg);

    tft->setTextDatum(MC_DATUM);

    tft->setTextSize(3);
    tft->setTextColor(theme.danger);
    tft->drawString("Game Over", 160, 90);

    char scoreLabel[24];
    snprintf(scoreLabel, sizeof(scoreLabel), "Landed: %d", _airScore);
    tft->setTextSize(2);
    tft->setTextColor(theme.fg);
    tft->drawString(scoreLabel, 160, 140);

    char bestLabel[24];
    snprintf(bestLabel, sizeof(bestLabel), "Best: %d", _airBest);
    tft->setTextColor(theme.accent);
    tft->drawString(bestLabel, 160, 170);

    tft->setTextDatum(TL_DATUM); // restore default for other screens
}

// One lane up or down, wrapping. Every lane is a valid stop, empty or not:
// skipping the empty ones (which this used to do) meant the knob could only
// ever land on a plane, so the cursor was never actually wrong and pressing
// KO was never actually a decision.
int GamesScreen::airLaneStep(int from, int dir) {
    return ((from + dir) % AIRTRAFFIC_LANES + AIRTRAFFIC_LANES) % AIRTRAFFIC_LANES;
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
        case STATE_SIMON_SHOWING:
            line1 = "WATCH";
            line2 = "";
            break;
        case STATE_SIMON_INPUT:
            line1 = "^v SELECT PAD";
            line2 = "o CONFIRM";
            break;
        case STATE_SIMON_GAMEOVER:
            line1 = "^v BACK TO LIST";
            line2 = "o RETRY";
            break;
        case STATE_AIRTRAFFIC_PLAYING:
            line1 = "^v PICK LANE";
            line2 = "o LAND IN ZONE";
            break;
        case STATE_AIRTRAFFIC_GAMEOVER:
            line1 = "^v BACK TO LIST";
            line2 = "o RETRY";
            break;
    }
}
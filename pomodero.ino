#include <Arduino.h>
#include <LovyanGFX.hpp>
#include <Wire.h>
#include <Preferences.h>

// =====================================================
// CONFIGURATION ECRAN ESP32-2432S022C
// ST7789 parallele 8 bits
// =====================================================

class LGFX : public lgfx::LGFX_Device
{
private:
  lgfx::Panel_ST7789 _panel;
  lgfx::Bus_Parallel8 _bus;

public:
  LGFX()
  {
    // Bus parallele 8 bits
    {
      auto cfg = _bus.config();

      cfg.freq_write = 10000000;

      cfg.pin_wr = 4;
      cfg.pin_rd = 2;
      cfg.pin_rs = 16;

      cfg.pin_d0 = 15;
      cfg.pin_d1 = 13;
      cfg.pin_d2 = 12;
      cfg.pin_d3 = 14;
      cfg.pin_d4 = 27;
      cfg.pin_d5 = 25;
      cfg.pin_d6 = 33;
      cfg.pin_d7 = 32;

      _bus.config(cfg);
      _panel.setBus(&_bus);
    }

    // Dalle ST7789
    {
      auto cfg = _panel.config();

      cfg.pin_cs = 17;
      cfg.pin_rst = -1;
      cfg.pin_busy = -1;

      cfg.panel_width = 240;
      cfg.panel_height = 320;

      cfg.memory_width = 240;
      cfg.memory_height = 320;

      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;

      cfg.readable = false;

      // Couleurs validees pour cette carte
      cfg.invert = false;
      cfg.rgb_order = false;

      cfg.dlen_16bit = false;
      cfg.bus_shared = true;

      _panel.config(cfg);
    }

    setPanel(&_panel);
  }
};

// =====================================================
// OBJETS
// =====================================================

LGFX lcd;
LGFX_Sprite timeSprite(&lcd);
Preferences prefs;

// =====================================================
// BROCHES
// =====================================================

constexpr uint8_t TFT_BACKLIGHT = 5;

// Tactile capacitif CST820 / CST816 probable
constexpr uint8_t TOUCH_SDA = 21;
constexpr uint8_t TOUCH_SCL = 22;
constexpr uint8_t TOUCH_ADDR = 0x15;

// =====================================================
// COULEURS RGB565
// =====================================================

constexpr uint16_t COLOR_BLACK      = 0x0000;
constexpr uint16_t COLOR_WHITE      = 0xFFFF;
constexpr uint16_t COLOR_RED        = 0xF800;
constexpr uint16_t COLOR_GREEN      = 0x07E0;
constexpr uint16_t COLOR_BLUE       = 0x001F;
constexpr uint16_t COLOR_YELLOW     = 0xFFE0;
constexpr uint16_t COLOR_GREY       = 0x8410;
constexpr uint16_t COLOR_DARK_GREY  = 0x2104;
constexpr uint16_t COLOR_PANEL      = 0x1082;

// =====================================================
// DUREES DU POMODORO
// =====================================================

constexpr uint32_t WORK_DURATION = 25UL * 60UL;
constexpr uint32_t SHORT_BREAK_DURATION = 5UL * 60UL;
constexpr uint32_t LONG_BREAK_DURATION = 15UL * 60UL;

constexpr uint8_t SESSIONS_BEFORE_LONG_BREAK = 4;

/*
  POUR UN TEST RAPIDE :

  Remplace les durees ci-dessus par :

  constexpr uint32_t WORK_DURATION = 10;
  constexpr uint32_t SHORT_BREAK_DURATION = 5;
  constexpr uint32_t LONG_BREAK_DURATION = 10;
*/

// =====================================================
// MODES DU POMODORO
// =====================================================

enum PomodoroMode
{
  MODE_WORK,
  MODE_SHORT_BREAK,
  MODE_LONG_BREAK
};

PomodoroMode currentMode = MODE_WORK;

// =====================================================
// VARIABLES DU MINUTEUR
// =====================================================

uint32_t totalSeconds = WORK_DURATION;
uint32_t remainingSeconds = WORK_DURATION;

uint8_t completedSessions = 0;
uint32_t todayCount = 0;

bool timerRunning = false;
unsigned long previousSecondMillis = 0;

// =====================================================
// VARIABLES D'AFFICHAGE
// =====================================================

int16_t lastProgressWidth = -1;

// Animation de fin non bloquante
bool finishAnimActive = false;
bool pendingPeriodAdvance = false;
uint8_t finishAnimStep = 0;
unsigned long finishAnimMillis = 0;

// Retour visuel reset non bloquant
bool resetFlashActive = false;
unsigned long resetFlashMillis = 0;

// =====================================================
// VARIABLES DU TACTILE
// =====================================================

bool touchAvailable = false;

bool previousTouchState = false;
bool longTouchHandled = false;

unsigned long touchStartedMillis = 0;
unsigned long lastTouchActionMillis = 0;

// Filtrage tactile sans delay
uint8_t touchHistory = 0;
bool stableTouchState = false;
unsigned long lastTouchSampleMillis = 0;

// =====================================================
// INFORMATIONS DU MODE
// =====================================================

const char *getModeTitle()
{
  switch (currentMode)
  {
    case MODE_WORK:
      return "POMODORO";

    case MODE_SHORT_BREAK:
      return "PAUSE COURTE";

    case MODE_LONG_BREAK:
      return "PAUSE LONGUE";
  }

  return "POMODORO";
}

uint16_t getModeColor()
{
  switch (currentMode)
  {
    case MODE_WORK:
      return COLOR_RED;

    case MODE_SHORT_BREAK:
      return COLOR_GREEN;

    case MODE_LONG_BREAK:
      return COLOR_BLUE;
  }

  return COLOR_WHITE;
}

uint32_t getModeDuration()
{
  switch (currentMode)
  {
    case MODE_WORK:
      return WORK_DURATION;

    case MODE_SHORT_BREAK:
      return SHORT_BREAK_DURATION;

    case MODE_LONG_BREAK:
      return LONG_BREAK_DURATION;
  }

  return WORK_DURATION;
}

// =====================================================
// SAUVEGARDE
// =====================================================

void saveCounters()
{
  prefs.putUChar("cycle", completedSessions);
  prefs.putUInt("today", todayCount);
}

void loadCounters()
{
  completedSessions = prefs.getUChar("cycle", 0);
  todayCount = prefs.getUInt("today", 0);

  if (completedSessions > SESSIONS_BEFORE_LONG_BREAK)
  {
    completedSessions = 0;
  }
}

// =====================================================
// TEXTE CENTRE SANS String
// =====================================================

void drawCenteredText(
  const char *text,
  int16_t centerX,
  int16_t centerY,
  uint8_t textSize,
  uint16_t textColor,
  uint16_t backgroundColor
)
{
  lcd.setTextDatum(middle_center);
  lcd.setTextSize(textSize);
  lcd.setTextColor(textColor, backgroundColor);
  lcd.drawString(text, centerX, centerY);
}

// =====================================================
// PREPARATION D'UNE PERIODE
// =====================================================

void preparePeriod(PomodoroMode mode)
{
  currentMode = mode;
  timerRunning = false;

  totalSeconds = getModeDuration();
  remainingSeconds = totalSeconds;

  lastProgressWidth = -1;
}

// =====================================================
// INDICATEURS DES SESSIONS
// =====================================================

void drawSessionIndicators()
{
  lcd.fillRect(90, 38, 140, 25, COLOR_BLACK);

  constexpr int16_t startX = 124;
  constexpr int16_t y = 50;
  constexpr int16_t spacing = 24;
  constexpr int16_t radius = 6;

  for (uint8_t i = 0; i < SESSIONS_BEFORE_LONG_BREAK; i++)
  {
    int16_t x = startX + (i * spacing);

    if (i < completedSessions)
    {
      lcd.fillCircle(x, y, radius, COLOR_RED);
    }
    else
    {
      lcd.drawCircle(x, y, radius, COLOR_GREY);
      lcd.drawCircle(x, y, radius - 1, COLOR_DARK_GREY);
    }
  }
}

// =====================================================
// COMPTEUR SAUVEGARDE
// =====================================================

void drawDailyCounter()
{
  lcd.fillRect(8, 38, 82, 25, COLOR_BLACK);

  char text[16];
  snprintf(text, sizeof(text), "TOTAL %lu", (unsigned long)todayCount);

  lcd.setTextDatum(middle_left);
  lcd.setTextSize(1);
  lcd.setTextColor(COLOR_GREY, COLOR_BLACK);
  lcd.drawString(text, 12, 50);
}

// =====================================================
// AFFICHAGE DU TEMPS AVEC SPRITE
// =====================================================

void drawTime()
{
  uint32_t minutes = remainingSeconds / 60;
  uint8_t seconds = remainingSeconds % 60;

  char timeText[10];
  snprintf(
    timeText,
    sizeof(timeText),
    "%02lu:%02u",
    (unsigned long)minutes,
    seconds
  );

  timeSprite.fillSprite(COLOR_BLACK);

  timeSprite.fillRoundRect(
    0,
    0,
    230,
    67,
    8,
    COLOR_PANEL
  );

  timeSprite.drawRoundRect(
    0,
    0,
    230,
    67,
    8,
    getModeColor()
  );

  timeSprite.setTextDatum(middle_center);
  timeSprite.setTextSize(5);
  timeSprite.setTextColor(getModeColor(), COLOR_PANEL);
  timeSprite.drawString(timeText, 115, 34);

  timeSprite.pushSprite(45, 69);
}

// =====================================================
// BARRE DE PROGRESSION OPTIMISEE
// =====================================================

void drawProgressBar()
{
  constexpr int16_t barX = 28;
  constexpr int16_t barY = 147;
  constexpr int16_t barWidth = 264;
  constexpr int16_t barHeight = 18;

  float progress = 0.0f;

  if (totalSeconds > 0)
  {
    progress = 1.0f - ((float)remainingSeconds / (float)totalSeconds);
  }

  if (progress < 0.0f)
  {
    progress = 0.0f;
  }

  if (progress > 1.0f)
  {
    progress = 1.0f;
  }

  int16_t progressWidth = (int16_t)((barWidth - 4) * progress);

  if (progressWidth == lastProgressWidth)
  {
    return;
  }

  lastProgressWidth = progressWidth;

  lcd.fillRoundRect(
    barX,
    barY,
    barWidth,
    barHeight,
    5,
    COLOR_DARK_GREY
  );

  lcd.drawRoundRect(
    barX,
    barY,
    barWidth,
    barHeight,
    5,
    COLOR_GREY
  );

  if (progressWidth > 0)
  {
    lcd.fillRoundRect(
      barX + 2,
      barY + 2,
      progressWidth,
      barHeight - 4,
      3,
      getModeColor()
    );
  }
}

// =====================================================
// BOUTON PLAY / PAUSE VISUEL
// =====================================================

void drawControlIcon()
{
  constexpr int16_t x = 110;
  constexpr int16_t y = 178;
  constexpr int16_t w = 100;
  constexpr int16_t h = 45;

  uint16_t borderColor = getModeColor();

  lcd.fillRoundRect(x, y, w, h, 12, COLOR_PANEL);
  lcd.drawRoundRect(x, y, w, h, 12, borderColor);

  if (timerRunning)
  {
    // Icone pause
    lcd.fillRoundRect(145, 190, 10, 22, 3, COLOR_YELLOW);
    lcd.fillRoundRect(165, 190, 10, 22, 3, COLOR_YELLOW);
  }
  else
  {
    // Icone play
    lcd.fillTriangle(
      149, 188,
      149, 214,
      175, 201,
      COLOR_GREEN
    );
  }
}

void drawResetFlash()
{
  constexpr int16_t x = 110;
  constexpr int16_t y = 178;
  constexpr int16_t w = 100;
  constexpr int16_t h = 45;

  lcd.drawRoundRect(x - 2, y - 2, w + 4, h + 4, 14, COLOR_RED);
  lcd.drawRoundRect(x - 4, y - 4, w + 8, h + 8, 16, COLOR_RED);
}

// =====================================================
// ECRAN COMPLET
// =====================================================

void drawFullScreen()
{
  lcd.fillScreen(COLOR_BLACK);

  lcd.drawRoundRect(
    2,
    2,
    316,
    236,
    10,
    getModeColor()
  );

  lcd.drawRoundRect(
    5,
    5,
    310,
    230,
    8,
    COLOR_DARK_GREY
  );

  drawCenteredText(
    getModeTitle(),
    160,
    23,
    2,
    getModeColor(),
    COLOR_BLACK
  );

  drawDailyCounter();
  drawSessionIndicators();

  lastProgressWidth = -1;

  drawTime();
  drawProgressBar();
  drawControlIcon();
}

// =====================================================
// START / PAUSE
// =====================================================

void toggleTimer()
{
  timerRunning = !timerRunning;

  if (timerRunning)
  {
    previousSecondMillis = millis();
  }

  drawControlIcon();

  Serial.print("Minuteur : ");

  if (timerRunning)
  {
    Serial.println("demarre");
  }
  else
  {
    Serial.println("pause");
  }
}

// =====================================================
// REINITIALISER LA PERIODE
// =====================================================

void resetCurrentPeriod()
{
  timerRunning = false;

  totalSeconds = getModeDuration();
  remainingSeconds = totalSeconds;

  lastProgressWidth = -1;

  drawFullScreen();

  resetFlashActive = true;
  resetFlashMillis = millis();
  drawResetFlash();

  Serial.println("Minuteur reinitialise");
}

// =====================================================
// ANIMATION DE FIN NON BLOQUANTE
// =====================================================

void startFinishAnimation()
{
  finishAnimActive = true;
  pendingPeriodAdvance = true;
  finishAnimStep = 0;
  finishAnimMillis = millis();

  lcd.fillScreen(getModeColor());
}

void updateFinishAnimation()
{
  if (!finishAnimActive)
  {
    return;
  }

  if (millis() - finishAnimMillis < 180)
  {
    return;
  }

  finishAnimMillis = millis();
  finishAnimStep++;

  if (finishAnimStep >= 6)
  {
    finishAnimActive = false;
    lcd.fillScreen(COLOR_BLACK);
    return;
  }

  if (finishAnimStep % 2 == 0)
  {
    lcd.fillScreen(getModeColor());
  }
  else
  {
    lcd.fillScreen(COLOR_BLACK);
  }
}

// =====================================================
// CHANGEMENT DE PERIODE APRES ANIMATION
// =====================================================

void advancePeriodAfterFinish()
{
  if (finishAnimActive || !pendingPeriodAdvance)
  {
    return;
  }

  pendingPeriodAdvance = false;

  if (currentMode == MODE_WORK)
  {
    completedSessions++;
    todayCount++;

    if (completedSessions >= SESSIONS_BEFORE_LONG_BREAK)
    {
      preparePeriod(MODE_LONG_BREAK);
    }
    else
    {
      preparePeriod(MODE_SHORT_BREAK);
    }

    saveCounters();
  }
  else
  {
    if (currentMode == MODE_LONG_BREAK)
    {
      completedSessions = 0;
      saveCounters();
    }

    preparePeriod(MODE_WORK);
  }

  drawFullScreen();
}

// =====================================================
// LECTURE TACTILE BRUTE
// =====================================================

bool readTouchRaw()
{
  if (!touchAvailable)
  {
    return false;
  }

  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(0x02);

  if (Wire.endTransmission(false) != 0)
  {
    return false;
  }

  uint8_t received = Wire.requestFrom(
    (uint8_t)TOUCH_ADDR,
    (uint8_t)1
  );

  if (received != 1)
  {
    return false;
  }

  uint8_t value = Wire.read();

  return (value & 0x0F) > 0;
}

// =====================================================
// FILTRAGE TACTILE SANS DELAY
// =====================================================

bool readTouchStable()
{
  unsigned long now = millis();

  if (now - lastTouchSampleMillis >= 10)
  {
    lastTouchSampleMillis = now;

    touchHistory <<= 1;

    if (readTouchRaw())
    {
      touchHistory |= 0x01;
    }

    touchHistory &= 0x07;

    if (touchHistory == 0x07)
    {
      stableTouchState = true;
    }
    else if (touchHistory == 0x00)
    {
      stableTouchState = false;
    }
  }

  return stableTouchState;
}

// =====================================================
// GESTION DU TACTILE
// =====================================================

void handleTouch()
{
  if (finishAnimActive)
  {
    return;
  }

  bool currentTouchState = readTouchStable();
  unsigned long now = millis();

  // Debut de l'appui
  if (!previousTouchState && currentTouchState)
  {
    touchStartedMillis = now;
    longTouchHandled = false;

    Serial.println("Ecran touche");
  }

  // Appui long de 2 secondes
  if (
    currentTouchState &&
    !longTouchHandled &&
    now - touchStartedMillis >= 2000
  )
  {
    longTouchHandled = true;
    resetCurrentPeriod();
  }

  // Doigt retire
  if (previousTouchState && !currentTouchState)
  {
    unsigned long touchDuration = now - touchStartedMillis;

    // Appui court
    if (
      !longTouchHandled &&
      touchDuration >= 50 &&
      touchDuration < 2000 &&
      now - lastTouchActionMillis >= 300
    )
    {
      lastTouchActionMillis = now;
      toggleTimer();
    }
  }

  previousTouchState = currentTouchState;
}

// =====================================================
// RETOUR VISUEL RESET NON BLOQUANT
// =====================================================

void updateResetFlash()
{
  if (!resetFlashActive)
  {
    return;
  }

  if (millis() - resetFlashMillis >= 400)
  {
    resetFlashActive = false;
    drawControlIcon();
  }
}

// =====================================================
// MISE A JOUR DU MINUTEUR
// =====================================================

void updateTimer()
{
  if (!timerRunning || finishAnimActive)
  {
    return;
  }

  unsigned long currentMillis = millis();

  while (currentMillis - previousSecondMillis >= 1000)
  {
    previousSecondMillis += 1000;

    if (remainingSeconds > 0)
    {
      remainingSeconds--;
    }

    drawTime();
    drawProgressBar();

    if (remainingSeconds == 0)
    {
      timerRunning = false;
      startFinishAnimation();
      break;
    }
  }
}

// =====================================================
// DETECTION DU CONTROLEUR TACTILE
// =====================================================

void testTouchController()
{
  Wire.beginTransmission(TOUCH_ADDR);

  uint8_t error = Wire.endTransmission();

  if (error == 0)
  {
    touchAvailable = true;
    Serial.println("Controleur tactile detecte a l'adresse 0x15");
  }
  else
  {
    touchAvailable = false;
    Serial.println("Attention : tactile non detecte a l'adresse 0x15");
  }
}

// =====================================================
// INITIALISATION
// =====================================================

void setup()
{
  Serial.begin(115200);

  // Preferences
  prefs.begin("pomodoro", false);
  loadCounters();

  // Tactile
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000);
  testTouchController();

  // Retroeclairage
  pinMode(TFT_BACKLIGHT, OUTPUT);
  digitalWrite(TFT_BACKLIGHT, HIGH);

  // Ecran
  lcd.init();
  lcd.setRotation(3);
  lcd.setBrightness(255);
  lcd.fillScreen(COLOR_BLACK);

  // Sprite de la minuterie
  timeSprite.setColorDepth(16);
  timeSprite.createSprite(230, 67);

  // Premiere periode
  preparePeriod(MODE_WORK);

  // Affichage
  drawFullScreen();

  Serial.println("Pomodoro tactile pret");
}

// =====================================================
// BOUCLE PRINCIPALE
// =====================================================

void loop()
{
  handleTouch();
  updateTimer();

  updateFinishAnimation();
  advancePeriodAfterFinish();

  updateResetFlash();
}
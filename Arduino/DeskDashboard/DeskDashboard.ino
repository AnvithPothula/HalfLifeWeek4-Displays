#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_NeoPixel.h>
#include <time.h>
#include "secrets.h"

const char* TIMEZONE = "CST6CDT,M3.2.0,M11.1.0";  // cst
const char* CITY_NAME = "Eagan, MN";
const char* LATITUDE = "44.80";
const char* LONGITUDE = "-93.17";
const char* FRC_TEAM= "frc2220";
const char* GITHUB_USER = "AnvithPothula";
const char* LICHESS_USER = "neurochessmaster";
const char* CHESSCOM_USER = "neurochessmaster";
const bool  USE_STATUS_LED = true;
const bool  BL_DIRECT = false;

// pomodoro lengths (min)
const int FOCUS_MINUTES = 25;
const int SHORT_BREAK_MINUTES = 5;
const int LONG_BREAK_MINUTES = 15;
const int SESSIONS_PER_SET = 4;

struct Countdown {
  const char* name;
  int year;
  int month;
  int day;
};
// These are examples - change them to your real dates
const Countdown COUNTDOWNS[] = {
  {"Build week", 2026, 12, 14},
  {"FRC Kickoff", 2027, 1, 9},
  {"Science fair", 2027, 2, 27},
};
const int NUM_COUNTDOWNS = sizeof(COUNTDOWNS) / sizeof(COUNTDOWNS[0]);

const int BRIGHT_DAY = 255;  // 0-255
const int BRIGHT_NIGHT = 25;

// pins
#define TFT_SCK 12
#define TFT_MOSI 11
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8
#define TFT_BL 7
#define ENC_A 1
#define ENC_B 2
#define ENC_SW 3
#define LED_PIN 21

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
Adafruit_NeoPixel statusLed(1, LED_PIN, NEO_GRB + NEO_KHZ800);

// colors
const uint16_t COL_BG       = 0x0000;
const uint16_t COL_TEXT     = 0xFFFF;
const uint16_t COL_DIM      = 0x8410;
const uint16_t COL_CLOCK    = 0x07FF;  // cyan
const uint16_t COL_WEATHER  = 0xFD20;  // orange
const uint16_t COL_POMO     = 0xF800;  // red
const uint16_t COL_BREAK    = 0x07E0;  // green
const uint16_t COL_PUZZLE   = 0xC618;  // light grey
const uint16_t COL_CHESS    = 0x07E0;  // green
const uint16_t COL_FRC      = 0x001F;  // blue
const uint16_t COL_GITHUB   = 0xF81F;  // magenta
const uint16_t COL_QUOTE    = 0x867F;  // light blue
const uint16_t COL_COUNT    = 0xFFE0;  // yellow
const uint16_t COL_LIGHT_SQ = 0xEF19;
const uint16_t COL_DARK_SQ  = 0x9B4A;
const uint16_t COL_LAST_SQ  = 0xCE60;

// pages
enum Page {
  PAGE_CLOCK, PAGE_WEATHER, PAGE_POMODORO, PAGE_PUZZLE, PAGE_CHESS,
  PAGE_FRC, PAGE_GITHUB, PAGE_QUOTE, PAGE_COUNTDOWN, NUM_PAGES
};
int currentPage = PAGE_CLOCK;
bool needFullRedraw = true;

// internet data
struct WeatherData {
  bool ok = false;
  float tempF = 0;
  int humidity = 0;
  float windMph = 0;
  int code = 0;
  float highF = 0;
  float lowF = 0;
  unsigned long fetchedAt = 0;
} weather;

struct FrcData {
  bool ok = false;
  String eventName;
  String place;
  String startDate;
  String endDate;
  unsigned long fetchedAt = 0;
} frc;

struct ChessData {
  bool lichessOk = false;
  bool chesscomOk = false;
  int liBullet = 0, liBlitz = 0, liRapid = 0, liPuzzle = 0;
  int ccBullet = 0, ccBlitz = 0, ccRapid = 0, ccPuzzle = 0;
  unsigned long fetchedAt = 0;
} chess;

struct GithubData {
  bool ok = false;
  int repos = 0;
  int followers = 0;
  String lastRepo;
  String lastType;
  unsigned long fetchedAt = 0;
} github;

const int MAX_SOLUTION = 16;
struct PuzzleData {
  bool ok = false;
  char board[8][8];
  char startBoard[8][8];
  bool whiteToMove = true;
  int rating = 0;
  String theme;
  String lastMove;
  String highlightMove;
  String solution[MAX_SOLUTION];
  int solutionLength = 0;
  int movesShown = 0;
  unsigned long fetchedAt = 0;
} puzzle;

struct QuoteData {
  bool ok = false;
  bool isFact = false;
  String text;
  String author;
  unsigned long fetchedAt = 0;
} quote;

const unsigned long WEATHER_EVERY = 10UL * 60UL * 1000UL;
const unsigned long FRC_EVERY     = 30UL * 60UL * 1000UL;
const unsigned long CHESS_EVERY   = 30UL * 60UL * 1000UL;
const unsigned long GITHUB_EVERY  = 30UL * 60UL * 1000UL;
const unsigned long PUZZLE_EVERY  = 60UL * 60UL * 1000UL;
const unsigned long QUOTE_EVERY   = 60UL * 60UL * 1000UL;

// pomodoro
enum PomoState { POMO_IDLE, POMO_RUNNING, POMO_PAUSED };
PomoState pomoState = POMO_IDLE;
bool pomoOnBreak = false;
int pomoSessionsDone = 0;
unsigned long pomoLengthMs = FOCUS_MINUTES * 60UL * 1000UL;
unsigned long pomoRemainingMs = FOCUS_MINUTES * 60UL * 1000UL;
unsigned long pomoLastTick = 0;
long pomoLastShownSecond = -1;

// encoder
volatile int32_t encoderRaw = 0;
volatile uint8_t encoderState = 0;
int32_t encoderUsed = 0;

void IRAM_ATTR onEncoderChange() {
  static const int8_t table[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
  uint8_t ab = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  encoderState = ((encoderState << 2) | ab) & 0x0F;
  encoderRaw += table[encoderState];
}

int readEncoderClicks() {
  int32_t raw = encoderRaw;
  int32_t clicks = (raw - encoderUsed) / 4;
  encoderUsed += clicks * 4;
  return (int)clicks;
}

// button
bool buttonDown = false;
unsigned long buttonDownAt = 0;
bool longPressDone = false;
bool nightMode = false;

// backlight
void setBrightness(int level) {
  level = constrain(level, 0, 255);
  if (BL_DIRECT) ledcWrite(TFT_BL, level);
  else ledcWrite(TFT_BL, 255 - level);
}

// status led
void setStatusColor(uint8_t r, uint8_t g, uint8_t b) {
  if (!USE_STATUS_LED) return;
  int scale = nightMode ? 4 : 30;
  statusLed.setPixelColor(0, statusLed.Color(r * scale / 255, g * scale / 255, b * scale / 255));
  statusLed.show();
}

void showPageColor() {
  if (pomoState == POMO_RUNNING) {
    if (pomoOnBreak) setStatusColor(0, 255, 0);
    else setStatusColor(255, 0, 0);
    return;
  }
  switch (currentPage) {
    case PAGE_CLOCK:     setStatusColor(0, 255, 255); break;
    case PAGE_WEATHER:   setStatusColor(255, 140, 0); break;
    case PAGE_POMODORO:  setStatusColor(255, 0, 0); break;
    case PAGE_PUZZLE:    setStatusColor(200, 200, 200); break;
    case PAGE_CHESS:     setStatusColor(0, 255, 0); break;
    case PAGE_FRC:       setStatusColor(0, 0, 255); break;
    case PAGE_GITHUB:    setStatusColor(255, 0, 255); break;
    case PAGE_QUOTE:     setStatusColor(120, 200, 255); break;
    case PAGE_COUNTDOWN: setStatusColor(255, 255, 0); break;
  }
}

// drawing helper function
void drawPageDots(uint16_t color) {
  int startX = 160 - (NUM_PAGES * 14) / 2;
  for (int i = 0; i < NUM_PAGES; i++) {
    if (i == currentPage) tft.fillCircle(startX + i * 14 + 7, 230, 4, color);
    else tft.drawCircle(startX + i * 14 + 7, 230, 4, COL_DIM);
  }
}

void drawHeader(const char* title, uint16_t color) {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, 320, 34, color);
  tft.setTextColor(COL_BG);
  tft.setTextSize(3);
  tft.setCursor(10, 6);
  tft.print(title);
  drawPageDots(color);
}

void printAt(int x, int y, int size, uint16_t color, const String& text) {
  tft.setTextSize(size);
  tft.setTextColor(color, COL_BG);
  tft.setCursor(x, y);
  tft.print(text);
}

void drawMessage(const char* msg) {
  printAt(10, 110, 2, COL_DIM, msg);
}

String toAscii(const String& in) {
  String out;
  out.reserve(in.length());
  for (unsigned int i = 0; i < in.length(); i++) {
    uint8_t c = (uint8_t)in[i];
    if (c < 128) { out += (char)c; continue; }
    if (c == 0xE2 && i + 2 < in.length() && (uint8_t)in[i + 1] == 0x80) {
      uint8_t c3 = (uint8_t)in[i + 2];
      if (c3 == 0x98 || c3 == 0x99) out += '\'';
      else if (c3 == 0x9C || c3 == 0x9D) out += '"';
      else if (c3 == 0x93 || c3 == 0x94) out += '-';
      else if (c3 == 0xA6) out += "...";
      i += 2;
      continue;
    }
    if (c >= 0xF0) i += 3;
    else if (c >= 0xE0) i += 2;
    else if (c >= 0xC0) i += 1;
  }
  return out;
}

int printWrapped(int x, int y, int size, uint16_t color, const String& text, int maxLines) {
  int charsPerLine = (320 - x - 8) / (6 * size);
  int lineHeight = 8 * size + 4;
  int start = 0;
  int lines = 0;
  int len = text.length();
  while (start < len && lines < maxLines) {
    int end = start + charsPerLine;
    if (end >= len) {
      end = len;
    } else {
      int space = text.lastIndexOf(' ', end);
      if (space > start) end = space;
    }
    String line = text.substring(start, end);
    line.trim();
    printAt(x, y, size, color, line);
    y += lineHeight;
    lines++;
    start = end;
    while (start < len && text[start] == ' ') start++;
  }
  return y;
}

int countWrappedLines(int x, int size, const String& text) {
  int charsPerLine = (320 - x - 8) / (6 * size);
  int start = 0;
  int lines = 0;
  int len = text.length();
  while (start < len) {
    int end = start + charsPerLine;
    if (end >= len) {
      end = len;
    } else {
      int space = text.lastIndexOf(' ', end);
      if (space > start) end = space;
    }
    lines++;
    start = end;
    while (start < len && text[start] == ' ') start++;
  }
  return lines;
}

// time helper functions
bool timeIsSet() {
  struct tm now;
  return getLocalTime(&now, 0) && now.tm_year > (2024 - 1900);
}

long daysUntil(int year, int month, int day) {
  struct tm now;
  if (!getLocalTime(&now, 0)) return 0;
  struct tm today = {};
  today.tm_year = now.tm_year;
  today.tm_mon = now.tm_mon;
  today.tm_mday = now.tm_mday;
  today.tm_hour = 12;
  struct tm target = {};
  target.tm_year = year - 1900;
  target.tm_mon = month - 1;
  target.tm_mday = day;
  target.tm_hour = 12;
  double seconds = difftime(mktime(&target), mktime(&today));
  return lround(seconds / 86400.0);
}

bool parseDate(const String& s, int& y, int& m, int& d) {
  if (s.length() < 10) return false;
  y = s.substring(0, 4).toInt();
  m = s.substring(5, 7).toInt();
  d = s.substring(8, 10).toInt();
  return y > 0 && m > 0 && d > 0;
}

// internet helped functions
bool getJson(const String& url, JsonDocument& doc, JsonDocument& filter,
             const char* headerName = nullptr, const char* headerValue = nullptr) {
  if (WiFi.status() != WL_CONNECTED) return false;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(8000);
  if (!http.begin(client, url)) return false;
  http.addHeader("User-Agent", "DeskDashboard/1.0");
  if (headerName != nullptr) http.addHeader(headerName, headerValue);
  int code = http.GET();
  bool ok = false;
  if (code == 200) {
    DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    ok = !err;
  } else {
    Serial.printf("HTTP %d from %s\n", code, url.c_str());
  }
  http.end();
  return ok;
}

void fetchWeather() {
  String url = String("https://api.open-meteo.com/v1/forecast?latitude=") + LATITUDE +
               "&longitude=" + LONGITUDE +
               "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m"
               "&daily=temperature_2m_max,temperature_2m_min"
               "&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto&forecast_days=1";
  JsonDocument filter;
  filter["current"] = true;
  filter["daily"] = true;
  JsonDocument doc;
  weather.fetchedAt = millis();
  if (!getJson(url, doc, filter)) { weather.ok = false; return; }
  weather.tempF    = doc["current"]["temperature_2m"] | 0.0f;
  weather.humidity = doc["current"]["relative_humidity_2m"] | 0;
  weather.code     = doc["current"]["weather_code"] | 0;
  weather.windMph  = doc["current"]["wind_speed_10m"] | 0.0f;
  weather.highF    = doc["daily"]["temperature_2m_max"][0] | 0.0f;
  weather.lowF     = doc["daily"]["temperature_2m_min"][0] | 0.0f;
  weather.ok = true;
}

void fetchFrc() {
  frc.fetchedAt = millis();
  if (strlen(TBA_API_KEY) == 0) { frc.ok = false; return; }
  struct tm now;
  if (!getLocalTime(&now, 0)) { frc.ok = false; return; }
  int thisYear = now.tm_year + 1900;

  JsonDocument filter;
  filter[0]["name"] = true;
  filter[0]["city"] = true;
  filter[0]["state_prov"] = true;
  filter[0]["start_date"] = true;
  filter[0]["end_date"] = true;

  for (int year = thisYear; year <= thisYear + 1; year++) {
    String url = String("https://www.thebluealliance.com/api/v3/team/") + FRC_TEAM + "/events/" + year + "/simple";
    JsonDocument doc;
    if (!getJson(url, doc, filter, "X-TBA-Auth-Key", TBA_API_KEY)) continue;

    long bestDays = 100000;
    bool found = false;
    for (JsonObject ev : doc.as<JsonArray>()) {
      String endDate = ev["end_date"] | "";
      int y, m, d;
      if (!parseDate(endDate, y, m, d)) continue;
      long days = daysUntil(y, m, d);
      if (days >= 0 && days < bestDays) {
        bestDays = days;
        frc.eventName = ev["name"] | "";
        frc.place = String(ev["city"] | "") + ", " + String(ev["state_prov"] | "");
        frc.startDate = ev["start_date"] | "";
        frc.endDate = endDate;
        found = true;
      }
    }
    if (found) { frc.ok = true; return; }
  }
  frc.ok = false;
}

void fetchChess() {
  chess.fetchedAt = millis();

  // lichess
  {
    JsonDocument filter;
    filter["perfs"]["bullet"]["rating"] = true;
    filter["perfs"]["blitz"]["rating"] = true;
    filter["perfs"]["rapid"]["rating"] = true;
    filter["perfs"]["puzzle"]["rating"] = true;
    JsonDocument doc;
    String url = String("https://lichess.org/api/user/") + LICHESS_USER;
    chess.lichessOk = getJson(url, doc, filter);
    if (chess.lichessOk) {
      chess.liBullet = doc["perfs"]["bullet"]["rating"] | 0;
      chess.liBlitz  = doc["perfs"]["blitz"]["rating"] | 0;
      chess.liRapid  = doc["perfs"]["rapid"]["rating"] | 0;
      chess.liPuzzle = doc["perfs"]["puzzle"]["rating"] | 0;
    }
  }

  // chess.com
  {
    JsonDocument filter;
    filter["chess_bullet"]["last"]["rating"] = true;
    filter["chess_blitz"]["last"]["rating"] = true;
    filter["chess_rapid"]["last"]["rating"] = true;
    filter["tactics"]["highest"]["rating"] = true;
    JsonDocument doc;
    String url = String("https://api.chess.com/pub/player/") + CHESSCOM_USER + "/stats";
    chess.chesscomOk = getJson(url, doc, filter);
    if (chess.chesscomOk) {
      chess.ccBullet = doc["chess_bullet"]["last"]["rating"] | 0;
      chess.ccBlitz  = doc["chess_blitz"]["last"]["rating"] | 0;
      chess.ccRapid  = doc["chess_rapid"]["last"]["rating"] | 0;
      chess.ccPuzzle = doc["tactics"]["highest"]["rating"] | 0;
    }
  }
}

void fetchGithub() {
  github.fetchedAt = millis();
  JsonDocument filter;
  filter["public_repos"] = true;
  filter["followers"] = true;
  JsonDocument doc;
  String url = String("https://api.github.com/users/") + GITHUB_USER;
  if (!getJson(url, doc, filter)) { github.ok = false; return; }
  github.repos = doc["public_repos"] | 0;
  github.followers = doc["followers"] | 0;

  JsonDocument evFilter;
  evFilter[0]["type"] = true;
  evFilter[0]["repo"]["name"] = true;
  JsonDocument events;
  String evUrl = String("https://api.github.com/users/") + GITHUB_USER + "/events/public?per_page=1";
  if (getJson(evUrl, events, evFilter)) {
    github.lastType = events[0]["type"] | "";
    github.lastRepo = events[0]["repo"]["name"] | "";
  }
  github.ok = true;
}

bool loadFen(const String& fen, char board[8][8], bool& whiteToMove) {
  for (int r = 0; r < 8; r++)
    for (int c = 0; c < 8; c++) board[r][c] = '.';
  int row = 0, col = 0;
  unsigned int i = 0;
  for (; i < fen.length() && fen[i] != ' '; i++) {
    char ch = fen[i];
    if (ch == '/') { row++; col = 0; }
    else if (ch >= '1' && ch <= '8') { col += ch - '0'; }
    else {
      if (row > 7 || col > 7) return false;
      board[row][col] = ch;
      col++;
    }
  }
  whiteToMove = !(i + 1 < fen.length() && fen[i + 1] == 'b');
  return row == 7;
}

void fetchPuzzle() {
  puzzle.fetchedAt = millis();
  JsonDocument filter;
  filter["puzzle"]["fen"] = true;
  filter["puzzle"]["rating"] = true;
  filter["puzzle"]["solution"] = true;
  filter["puzzle"]["themes"] = true;
  filter["puzzle"]["lastMove"] = true;
  JsonDocument doc;
  if (!getJson("https://lichess.org/api/puzzle/daily", doc, filter)) { puzzle.ok = false; return; }

  String fen = doc["puzzle"]["fen"] | "";
  if (!loadFen(fen, puzzle.startBoard, puzzle.whiteToMove)) { puzzle.ok = false; return; }
  memcpy(puzzle.board, puzzle.startBoard, sizeof(puzzle.board));
  puzzle.rating = doc["puzzle"]["rating"] | 0;
  puzzle.lastMove = doc["puzzle"]["lastMove"] | "";
  puzzle.highlightMove = puzzle.lastMove;

  puzzle.theme = "";
  JsonArray themes = doc["puzzle"]["themes"];
  for (JsonVariant t : themes) {
    String name = t | "";
    if (name != "short" && name != "long" && name != "veryLong" && name != "oneMove") {
      puzzle.theme = name;
      break;
    }
  }

  puzzle.solutionLength = 0;
  JsonArray moves = doc["puzzle"]["solution"];
  for (JsonVariant m : moves) {
    if (puzzle.solutionLength >= MAX_SOLUTION) break;
    puzzle.solution[puzzle.solutionLength++] = m | "";
  }
  puzzle.movesShown = 0;
  puzzle.ok = true;
}

void fetchQuote() {
  quote.fetchedAt = millis();
  quote.isFact = !quote.isFact;
  if (quote.isFact) {
    JsonDocument filter;
    filter["text"] = true;
    JsonDocument doc;
    quote.ok = getJson("https://uselessfacts.jsph.pl/api/v2/facts/random?language=en", doc, filter);
    if (quote.ok) {
      quote.text = toAscii(doc["text"] | "");
      quote.author = "";
    }
  } else {
    JsonDocument filter;
    filter[0]["q"] = true;
    filter[0]["a"] = true;
    JsonDocument doc;
    quote.ok = getJson("https://zenquotes.io/api/random", doc, filter);
    if (quote.ok) {
      quote.text = toAscii(doc[0]["q"] | "");
      quote.author = toAscii(doc[0]["a"] | "");
    }
  }
}

void updateCurrentPageData(bool force) {
  unsigned long now = millis();
  bool fetched = false;
  if (currentPage == PAGE_WEATHER && (force || weather.fetchedAt == 0 || now - weather.fetchedAt > WEATHER_EVERY)) {
    fetchWeather(); fetched = true;
  } else if (currentPage == PAGE_FRC && (force || frc.fetchedAt == 0 || now - frc.fetchedAt > FRC_EVERY)) {
    fetchFrc(); fetched = true;
  } else if (currentPage == PAGE_CHESS && (force || chess.fetchedAt == 0 || now - chess.fetchedAt > CHESS_EVERY)) {
    fetchChess(); fetched = true;
  } else if (currentPage == PAGE_GITHUB && (force || github.fetchedAt == 0 || now - github.fetchedAt > GITHUB_EVERY)) {
    fetchGithub(); fetched = true;
  } else if (currentPage == PAGE_PUZZLE && (force || puzzle.fetchedAt == 0 || now - puzzle.fetchedAt > PUZZLE_EVERY)) {
    fetchPuzzle(); fetched = true;
  } else if (currentPage == PAGE_QUOTE && (force || quote.fetchedAt == 0 || now - quote.fetchedAt > QUOTE_EVERY)) {
    fetchQuote(); fetched = true;
  }
  if (fetched) needFullRedraw = true;
}

const char* weatherText(int code) {
  if (code == 0) return "Clear";
  if (code <= 2) return "Partly cloudy";
  if (code == 3) return "Cloudy";
  if (code == 45 || code == 48) return "Fog";
  if (code >= 51 && code <= 57) return "Drizzle";
  if (code >= 61 && code <= 67) return "Rain";
  if (code >= 71 && code <= 77) return "Snow";
  if (code >= 80 && code <= 82) return "Showers";
  if (code == 85 || code == 86) return "Snow showers";
  if (code >= 95) return "Thunderstorm";
  return "Unknown";
}

// pomodoro logic
void pomoStartPhase(bool onBreak) {
  pomoOnBreak = onBreak;
  int minutes = FOCUS_MINUTES;
  if (onBreak) minutes = (pomoSessionsDone >= SESSIONS_PER_SET) ? LONG_BREAK_MINUTES : SHORT_BREAK_MINUTES;
  pomoLengthMs = (unsigned long)minutes * 60UL * 1000UL;
  pomoRemainingMs = pomoLengthMs;
  pomoState = POMO_IDLE;
}

void pomoReset() {
  pomoSessionsDone = 0;
  pomoStartPhase(false);
}

void pomoTogglePause() {
  if (pomoState == POMO_RUNNING) {
    pomoState = POMO_PAUSED;
  } else {
    pomoState = POMO_RUNNING;
    pomoLastTick = millis();
  }
  showPageColor();
}

void blinkStatus(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < 3; i++) {
    setStatusColor(r, g, b);
    delay(200);
    setStatusColor(0, 0, 0);
    delay(200);
  }
}

void pomoTick() {
  if (pomoState != POMO_RUNNING) return;
  unsigned long now = millis();
  unsigned long passed = now - pomoLastTick;
  pomoLastTick = now;
  if (passed < pomoRemainingMs) {
    pomoRemainingMs -= passed;
    return;
  }
  pomoRemainingMs = 0;
  if (!pomoOnBreak) {
    pomoSessionsDone++;
    blinkStatus(0, 255, 0);
    pomoStartPhase(true);
  } else {
    if (pomoSessionsDone >= SESSIONS_PER_SET) pomoSessionsDone = 0;
    blinkStatus(255, 0, 0);
    pomoStartPhase(false);
  }
  currentPage = PAGE_POMODORO;
  needFullRedraw = true;
  showPageColor();
}

// chess board drawing
const int SQ = 24;
const int BOARD_X = 6;
const int BOARD_Y = 40;

bool squareToRowCol(char file, char rank, int& row, int& col) {
  if (file < 'a' || file > 'h' || rank < '1' || rank > '8') return false;
  col = file - 'a';
  row = 7 - (rank - '1');
  return true;
}

bool isHighlighted(int row, int col) {
  if (puzzle.highlightMove.length() < 4) return false;
  int r1, c1, r2, c2;
  if (!squareToRowCol(puzzle.highlightMove[0], puzzle.highlightMove[1], r1, c1)) return false;
  if (!squareToRowCol(puzzle.highlightMove[2], puzzle.highlightMove[3], r2, c2)) return false;
  return (row == r1 && col == c1) || (row == r2 && col == c2);
}

void drawBoard() {
  bool flip = !puzzle.whiteToMove;
  for (int row = 0; row < 8; row++) {
    for (int col = 0; col < 8; col++) {
      int drawRow = flip ? 7 - row : row;
      int drawCol = flip ? 7 - col : col;
      int x = BOARD_X + drawCol * SQ;
      int y = BOARD_Y + drawRow * SQ;
      bool light = ((row + col) % 2) == 0;
      uint16_t sqColor = light ? COL_LIGHT_SQ : COL_DARK_SQ;
      if (isHighlighted(row, col)) sqColor = COL_LAST_SQ;
      tft.fillRect(x, y, SQ, SQ, sqColor);

      char piece = puzzle.board[row][col];
      if (piece == '.') continue;
      bool whitePiece = (piece >= 'A' && piece <= 'Z');
      char letter = whitePiece ? piece : (char)(piece - 32);
      int cx = x + SQ / 2;
      int cy = y + SQ / 2;
      tft.fillCircle(cx, cy, 10, whitePiece ? COL_TEXT : COL_BG);
      tft.drawCircle(cx, cy, 10, whitePiece ? COL_BG : COL_TEXT);
      tft.setTextSize(2);
      tft.setTextColor(whitePiece ? COL_BG : COL_TEXT);
      tft.setCursor(cx - 5, cy - 7);
      tft.print(letter);
    }
  }
}

void applyMove(const String& mv) {
  if (mv.length() < 4) return;
  int r1, c1, r2, c2;
  if (!squareToRowCol(mv[0], mv[1], r1, c1)) return;
  if (!squareToRowCol(mv[2], mv[3], r2, c2)) return;
  char piece = puzzle.board[r1][c1];
  if (piece == '.') return;
  bool whitePiece = (piece >= 'A' && piece <= 'Z');

  if ((piece == 'K' || piece == 'k') && abs(c2 - c1) == 2) {
    int rookFrom = (c2 > c1) ? 7 : 0;
    int rookTo = (c2 > c1) ? 5 : 3;
    puzzle.board[r1][rookTo] = puzzle.board[r1][rookFrom];
    puzzle.board[r1][rookFrom] = '.';
  }
  if ((piece == 'P' || piece == 'p') && c1 != c2 && puzzle.board[r2][c2] == '.') {
    puzzle.board[r1][c2] = '.';
  }
  puzzle.board[r2][c2] = piece;
  puzzle.board[r1][c1] = '.';

  if (mv.length() >= 5) {
    char promo = mv[4];
    puzzle.board[r2][c2] = whitePiece ? (char)(promo - 32) : promo;
  }
  puzzle.highlightMove = mv;
}

void puzzleNextMove() {
  if (!puzzle.ok) return;
  if (puzzle.movesShown >= puzzle.solutionLength) {
    memcpy(puzzle.board, puzzle.startBoard, sizeof(puzzle.board));
    puzzle.movesShown = 0;
    puzzle.highlightMove = puzzle.lastMove;
  } else {
    applyMove(puzzle.solution[puzzle.movesShown]);
    puzzle.movesShown++;
  }
  needFullRedraw = true;
}

// pages
int lastShownSecond = -1;

void drawClockPage(bool full) {
  if (full) {
    drawHeader("Clock", COL_CLOCK);
    lastShownSecond = -1;
  }
  struct tm now;
  if (!getLocalTime(&now, 0) || now.tm_year < (2024 - 1900)) {
    if (full) drawMessage("Waiting for time...");
    return;
  }
  if (now.tm_sec == lastShownSecond) return;
  lastShownSecond = now.tm_sec;

  char timeStr[12];
  int hour12 = now.tm_hour % 12;
  if (hour12 == 0) hour12 = 12;
  snprintf(timeStr, sizeof(timeStr), "%2d:%02d", hour12, now.tm_min);
  printAt(30, 70, 7, COL_TEXT, timeStr);

  char secStr[8];
  snprintf(secStr, sizeof(secStr), ":%02d %s", now.tm_sec, now.tm_hour < 12 ? "AM" : "PM");
  printAt(225, 135, 2, COL_CLOCK, secStr);

  char dateStr[32];
  strftime(dateStr, sizeof(dateStr), "%A, %b %d   ", &now);
  printAt(30, 175, 2, COL_DIM, dateStr);
}

void drawWeatherPage() {
  drawHeader("Weather", COL_WEATHER);
  printAt(10, 44, 2, COL_DIM, CITY_NAME);
  if (!weather.ok) { drawMessage("No weather data yet"); return; }
  printAt(10, 75, 6, COL_TEXT, String((int)lround(weather.tempF)) + "F");
  printAt(10, 130, 2, COL_WEATHER, weatherText(weather.code));
  printAt(10, 160, 2, COL_TEXT, "High " + String((int)lround(weather.highF)) + "  Low " + String((int)lround(weather.lowF)));
  printAt(10, 185, 2, COL_TEXT, "Humid " + String(weather.humidity) + "%  Wind " + String((int)lround(weather.windMph)) + "mph");
}

void drawPomodoroPage(bool full) {
  uint16_t color = pomoOnBreak ? COL_BREAK : COL_POMO;
  if (full) {
    drawHeader(pomoOnBreak ? "Break" : "Focus", color);
    pomoLastShownSecond = -1;
    int setNumber = pomoSessionsDone + (pomoOnBreak ? 0 : 1);
    if (setNumber > SESSIONS_PER_SET) setNumber = SESSIONS_PER_SET;
    printAt(10, 44, 2, COL_DIM, "Session " + String(setNumber) + " of " + String(SESSIONS_PER_SET));
    printAt(10, 205, 1, COL_DIM, "Press: start/pause    Hold: reset");
  }
  long secondsLeft = (long)((pomoRemainingMs + 999) / 1000);
  if (secondsLeft == pomoLastShownSecond && !full) return;
  pomoLastShownSecond = secondsLeft;

  char timeStr[12];
  snprintf(timeStr, sizeof(timeStr), "%02ld:%02ld", secondsLeft / 60, secondsLeft % 60);
  printAt(55, 75, 7, COL_TEXT, timeStr);

  // Progress bar
  const int barWidth = 300;
  const int inner = barWidth - 2;
  int filled = 0;
  if (pomoLengthMs > 0) filled = (int)((pomoLengthMs - pomoRemainingMs) * (unsigned long)inner / pomoLengthMs);
  filled = constrain(filled, 0, inner);
  tft.drawRect(10, 150, barWidth, 14, COL_DIM);
  if (filled > 0) tft.fillRect(11, 151, filled, 12, color);
  if (filled < inner) tft.fillRect(11 + filled, 151, inner - filled, 12, COL_BG);

  const char* status = "Ready - press to start";
  if (pomoState == POMO_RUNNING) status = "Running               ";
  else if (pomoState == POMO_PAUSED) status = "Paused                ";
  printAt(10, 175, 2, color, status);
}

void drawPuzzlePage() {
  tft.fillScreen(COL_BG);
  printAt(212, 6, 2, COL_PUZZLE, "Daily");
  printAt(212, 24, 2, COL_PUZZLE, "puzzle");
  if (!puzzle.ok) { printAt(10, 110, 2, COL_DIM, "No puzzle yet"); return; }
  drawBoard();

  printAt(212, 56, 2, COL_TEXT, puzzle.whiteToMove ? "White" : "Black");
  printAt(212, 74, 2, COL_TEXT, "to move");
  printAt(212, 102, 1, COL_DIM, "Rating");
  printAt(212, 112, 2, COL_TEXT, String(puzzle.rating));
  if (puzzle.theme.length() > 0) {
    String theme = puzzle.theme;
    if (theme.length() > 17) theme = theme.substring(0, 17);
    printAt(212, 138, 1, COL_DIM, "Theme");
    printAt(212, 148, 1, COL_TEXT, theme);
  }
  if (puzzle.movesShown == 0) {
    printAt(212, 175, 1, COL_DIM, "Press for");
    printAt(212, 185, 1, COL_DIM, "the solution");
  } else if (puzzle.movesShown < puzzle.solutionLength) {
    String mv = puzzle.solution[puzzle.movesShown - 1];
    printAt(212, 175, 1, COL_DIM, "Move " + String(puzzle.movesShown) + "/" + String(puzzle.solutionLength));
    printAt(212, 187, 2, COL_CHESS, mv.substring(0, 2) + "-" + mv.substring(2));
  } else {
    printAt(212, 175, 2, COL_CHESS, "Solved!");
    printAt(212, 197, 1, COL_DIM, "Press to replay");
  }
}

void drawChessPage() {
  drawHeader("Chess", COL_CHESS);
  printAt(10, 42, 1, COL_DIM, LICHESS_USER);
  printAt(118, 58, 2, COL_TEXT, "lichess");
  printAt(210, 58, 2, COL_TEXT, "chess.com");

  const char* labels[4] = {"Bullet", "Blitz", "Rapid", "Puzzles"};
  int li[4] = {chess.liBullet, chess.liBlitz, chess.liRapid, chess.liPuzzle};
  int cc[4] = {chess.ccBullet, chess.ccBlitz, chess.ccRapid, chess.ccPuzzle};
  for (int i = 0; i < 4; i++) {
    int y = 92 + i * 30;
    printAt(10, y, 2, COL_DIM, labels[i]);
    printAt(118, y, 2, i == 3 ? COL_CHESS : COL_TEXT, chess.lichessOk && li[i] > 0 ? String(li[i]) : String("-"));
    printAt(210, y, 2, i == 3 ? COL_CHESS : COL_TEXT, chess.chesscomOk && cc[i] > 0 ? String(cc[i]) : String("-"));
  }
}

void drawFrcPage() {
  drawHeader("FRC 2220", COL_FRC);
  if (strlen(TBA_API_KEY) == 0) { drawMessage("Add your TBA key in secrets.h"); return; }
  if (!frc.ok) { drawMessage("No upcoming events found"); return; }
  printAt(10, 50, 2, COL_DIM, "Next event:");
  String name = toAscii(frc.eventName);
  if (name.length() > 26) name = name.substring(0, 25) + ".";
  printAt(10, 75, 2, COL_TEXT, name);
  printAt(10, 100, 2, COL_DIM, toAscii(frc.place));

  int y, m, d;
  if (parseDate(frc.startDate, y, m, d)) {
    long days = daysUntil(y, m, d);
    if (days > 0) {
      printAt(10, 135, 6, COL_TEXT, String(days));
      printAt(10, 190, 2, COL_DIM, days == 1 ? "day to go" : "days to go");
    } else {
      printAt(10, 140, 3, COL_CHESS, "Happening now!");
    }
  }
}

void drawGithubPage() {
  drawHeader("GitHub", COL_GITHUB);
  printAt(10, 44, 2, COL_DIM, GITHUB_USER);
  if (!github.ok) { drawMessage("No GitHub data yet"); return; }
  printAt(10, 75, 3, COL_TEXT, String(github.repos) + " repos");
  printAt(10, 110, 3, COL_TEXT, String(github.followers) + " followers");
  if (github.lastRepo.length() > 0) {
    String type = github.lastType;
    type.replace("Event", "");
    String repo = github.lastRepo;
    int slash = repo.indexOf('/');
    if (slash >= 0) repo = repo.substring(slash + 1);
    if (repo.length() > 25) repo = repo.substring(0, 24) + ".";
    printAt(10, 155, 2, COL_DIM, "Latest: " + type);
    printAt(10, 180, 2, COL_GITHUB, repo);
  }
}

void drawQuotePage() {
  drawHeader(quote.isFact ? "Fun fact" : "Quote", COL_QUOTE);
  if (!quote.ok) { drawMessage("Nothing yet - press to load"); return; }
  // Use big text if it fits in 7 lines, otherwise small text
  int size = (countWrappedLines(10, 2, quote.text) <= 7) ? 2 : 1;
  int maxLines = (size == 2) ? 7 : 15;
  int y = printWrapped(10, 46, size, COL_TEXT, quote.text, maxLines);
  if (quote.author.length() > 0) {
    printAt(10, y + 4, 2, COL_QUOTE, "- " + quote.author);
  }
}

void drawCountdownPage() {
  drawHeader("Countdowns", COL_COUNT);
  if (!timeIsSet()) { drawMessage("Waiting for time..."); return; }
  for (int i = 0; i < NUM_COUNTDOWNS && i < 4; i++) {
    long days = daysUntil(COUNTDOWNS[i].year, COUNTDOWNS[i].month, COUNTDOWNS[i].day);
    int y = 50 + i * 42;
    printAt(10, y, 2, COL_TEXT, COUNTDOWNS[i].name);
    String right = days > 0 ? String(days) + " days" : (days == 0 ? String("Today!") : String("Done"));
    printAt(200, y, 2, COL_COUNT, right);
  }
}

void drawCurrentPage(bool full) {
  switch (currentPage) {
    case PAGE_CLOCK:     drawClockPage(full); break;
    case PAGE_WEATHER:   if (full) drawWeatherPage(); break;
    case PAGE_POMODORO:  drawPomodoroPage(full); break;
    case PAGE_PUZZLE:    if (full) drawPuzzlePage(); break;
    case PAGE_CHESS:     if (full) drawChessPage(); break;
    case PAGE_FRC:       if (full) drawFrcPage(); break;
    case PAGE_GITHUB:    if (full) drawGithubPage(); break;
    case PAGE_QUOTE:     if (full) drawQuotePage(); break;
    case PAGE_COUNTDOWN: if (full) drawCountdownPage(); break;
  }
}

// button actions
void onShortPress() {
  if (currentPage == PAGE_POMODORO) {
    pomoTogglePause();
    needFullRedraw = true;
  } else if (currentPage == PAGE_PUZZLE) {
    puzzleNextMove();
  } else {
    updateCurrentPageData(true);
    needFullRedraw = true;
  }
}

void onLongPress() {
  if (currentPage == PAGE_POMODORO) {
    pomoReset();
    showPageColor();
    needFullRedraw = true;
  } else {
    nightMode = !nightMode;
    setBrightness(nightMode ? BRIGHT_NIGHT : BRIGHT_DAY);
    showPageColor();
  }
}

// setup
void setup() {
  Serial.begin(115200);

  // backlight
  ledcAttach(TFT_BL, 5000, 8);
  setBrightness(0);

  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  encoderState = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  attachInterrupt(digitalPinToInterrupt(ENC_A), onEncoderChange, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_B), onEncoderChange, CHANGE);

  if (USE_STATUS_LED) {
    statusLed.begin();
    statusLed.clear();
    statusLed.show();
  }

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.init(240, 320);
  tft.setRotation(1);
  tft.fillScreen(COL_BG);
  setBrightness(BRIGHT_DAY);

  printAt(10, 100, 2, COL_TEXT, "Connecting to Wi-Fi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(250);
  }
  configTzTime(TIMEZONE, "pool.ntp.org", "time.nist.gov");

  pomoReset();
  showPageColor();
  needFullRedraw = true;
}

// loop
void loop() {
  unsigned long now = millis();

  // knob to change page
  int clicks = readEncoderClicks();
  if (clicks != 0) {
    currentPage = ((currentPage + clicks) % NUM_PAGES + NUM_PAGES) % NUM_PAGES;
    showPageColor();
    drawCurrentPage(true);
    needFullRedraw = false;
    updateCurrentPageData(false);
  }

  // button
  bool pressed = digitalRead(ENC_SW) == LOW;
  if (pressed && !buttonDown) {
    buttonDown = true;
    buttonDownAt = now;
    longPressDone = false;
  } else if (pressed && buttonDown && !longPressDone && now - buttonDownAt > 700) {
    longPressDone = true;
    onLongPress();
  } else if (!pressed && buttonDown) {
    buttonDown = false;
    if (!longPressDone && now - buttonDownAt > 30) onShortPress();
  }

  // pomodoro
  pomoTick();

  updateCurrentPageData(false);

  // draw
  if (needFullRedraw) {
    drawCurrentPage(true);
    needFullRedraw = false;
  } else {
    drawCurrentPage(false);
  }

  // reconnect wifi if it drops
  static unsigned long lastWifiCheck = 0;
  if (WiFi.status() != WL_CONNECTED && now - lastWifiCheck > 30000) {
    lastWifiCheck = now;
    WiFi.reconnect();
  }

  delay(10);
}

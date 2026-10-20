/*
  LED Draw Wall
  ESP32-S3-mini + WS2801 grid (27 wide x 25 tall, zigzag, data starts top-right)

  - ESP32 runs its own Wi-Fi hotspot with a captive portal, so joining the
    network pops the drawing page open automatically.
  - Up to MAX_USERS people draw together on one shared canvas.
  - Nobody drawing -> slideshow of saved drawings + built-in animations.
  - Scroll button + speed slider marquees the drawing; saves remember it.
  - Admin page at http://192.168.4.1/admin?key=ADMIN_KEY to approve/delete drawings.
    New saves wait there (pending) before joining the slideshow.

  Libraries (Arduino Library Manager):
    FastLED                      (by Daniel Garcia)
    ESP Async WebServer          (by ESP32Async)
    Async TCP                    (by ESP32Async)
*/

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
// NOTE: pin FastLED to 3.9.x. On 3.10.x it routes clocked chipsets (WS2801)
// through the ESP32-S3 LCD_CAM peripheral, which panics on boot here
// (esp_cache_msync null pointer -> interrupt wdt timeout -> boot loop, before
// Wi-Fi ever starts), and disabling that driver leaves 3.10 with no WS2801
// bus for the S3 at all. 3.9.x uses the ordinary SPI path and works.
#include <FastLED.h>
#include <vector>
#include <mutex>
#include "index_html.h"
#include "admin_html.h"

// ============================ CONFIG ============================
#define DATA_PIN       5          // WS2801 DI  (SDI)  -- 3.3V->5V level shifter recommended
#define CLOCK_PIN      6          // WS2801 CI  (CKI)  -- same shifter, second channel
#define SPI_MHZ        1          // WS2801 is nominally a 1 MHz part; 2 MHz flickered here.
                                  // (The old "2 MHz minimum" only applied to FastLED 3.10's
                                  // LCD_CAM driver -- 3.9.x's plain SPI path has no such floor.)
#define GRID_W         27         // columns
#define GRID_H         25         // rows
#define FIRST_ROW_RTL  false      // top row runs left->right (data enters top-LEFT)
#define LED_ORDER      RBG        // these strips want R, then B, then G

static const uint8_t  BRIGHTNESS     = 90;      // global cap, 0-255
static const uint32_t PSU_MA         = 4000;    // FastLED keeps total draw under this
static const char*    AP_SSID        = "LED Draw Wall";
static const char*    AP_PASS        = "";      // "" = open network, or 8+ characters
static const char*    ADMIN_KEY      = "changeme";
static const uint8_t  MAX_USERS      = 2;
static const uint32_t IDLE_MS        = 120000;  // connected but not drawing -> slideshow
static const uint32_t EMPTY_CLEAR_MS = 30000;   // nobody connected this long -> wipe canvas
static const uint32_t SLIDE_MS       = 8000;    // how long a saved drawing shows
static const uint32_t ANIM_MS        = 15000;   // how long an animation runs
static const uint32_t FADE_MS        = 800;     // crossfade between slideshow items
static const uint16_t MAX_SAVES      = 300;     // approved + pending
static const bool     REQUIRE_APPROVAL = true;  // saves wait in /admin until approved
static const bool     GAMMA_CORRECT  = true;    // makes LED colors closer to the screen
static const uint8_t  MAX_SCROLL     = 20;      // fastest scroll, columns per second
// ================================================================

#define NUM_LEDS    (GRID_W * GRID_H)
#define FRAME_BYTES (NUM_LEDS * 3)

enum : uint8_t { OP_PIXELS = 0x01, OP_CLEAR = 0x02, OP_SAVE = 0x03, OP_SCROLL = 0x04,
                 OP_FULL = 0x10, OP_OFFSET = 0x11 };

// Slideshow item. Defined up here (not next to the slideshow code) because the
// .ino preprocessor hoists its generated prototypes above the first function,
// and renderItem()/pickNext() need the type to be complete by then.
struct Item { bool anim; uint16_t id; };   // anim: id = animation number; else saved id

CRGB leds[NUM_LEDS];       // physical order
CRGB canvas[NUM_LEDS];     // shared drawing, row-major from top-left
uint8_t gammaLut[256];

DNSServer dns;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

std::mutex savesLock;
std::vector<uint16_t> saves;     // approved drawings (/d) -> in the slideshow
std::vector<uint16_t> pending;   // awaiting approval (/p)
uint16_t nextId = 1;

volatile uint32_t lastInput = 0;
volatile bool saveRequested = false;

// Scrolling: the canvas wraps around and slides left. It's a view effect, so
// the drawing itself never changes; phones draw "onto the moving paper".
volatile bool    scrollOn    = false;
volatile uint8_t scrollSpeed = 6;     // columns per second
volatile uint8_t scrollOff   = 0;     // current shift, 0..GRID_W-1

void scrolledCopy(const CRGB* src, CRGB* dst, uint8_t off) {
  for (uint8_t y = 0; y < GRID_H; y++)
    for (uint8_t x = 0; x < GRID_W; x++)
      dst[y * GRID_W + x] = src[y * GRID_W + (x + off) % GRID_W];
}

// ---------------------------------------------------------------- mapping
uint16_t ledIndex(uint8_t x, uint8_t y) {
  bool rtl = (((y & 1) == 0) == FIRST_ROW_RTL);
  return y * GRID_W + (rtl ? (GRID_W - 1 - x) : x);
}

void putFrame(const CRGB* frame) {
  for (uint8_t y = 0; y < GRID_H; y++)
    for (uint8_t x = 0; x < GRID_W; x++) {
      const CRGB& c = frame[y * GRID_W + x];
      leds[ledIndex(x, y)] = CRGB(gammaLut[c.r], gammaLut[c.g], gammaLut[c.b]);
    }
}

// Boot check: a dot runs left->right along the TOP row, then rows wipe top->bottom.
// If the dot runs the wrong way or starts at the bottom, flip FIRST_ROW_RTL / rows.
void bootWipe() {
  for (uint8_t x = 0; x < GRID_W; x++) {
    FastLED.clear();
    leds[ledIndex(x, 0)] = CRGB(120, 0, 0);
    FastLED.show();
    delay(20);
  }
  for (uint8_t y = 0; y < GRID_H; y++) {
    FastLED.clear();
    for (uint8_t x = 0; x < GRID_W; x++) leds[ledIndex(x, y)] = CHSV(y * 10, 255, 120);
    FastLED.show();
    delay(30);
  }
  FastLED.clear(true);
}

// ---------------------------------------------------------------- saves
String savePath(uint16_t id, bool isPending = false) {
  char p[20];
  snprintf(p, sizeof p, "/%c/%05u.bin", isPending ? 'p' : 'd', id);
  return String(p);
}

void scanDir(const char* dir, std::vector<uint16_t>& out) {
  File root = LittleFS.open(dir);
  if (!root) return;
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    const char* n = f.name();
    const char* s = strrchr(n, '/');
    uint16_t id = atoi(s ? s + 1 : n);
    if (id) {
      out.push_back(id);
      if (id >= nextId) nextId = id + 1;
    }
  }
}

void loadSaveList() {
  scanDir("/d", saves);
  scanDir("/p", pending);
  Serial.printf("Loaded %u approved, %u pending drawings\n",
                (unsigned)saves.size(), (unsigned)pending.size());
}

void eraseId(std::vector<uint16_t>& v, uint16_t id) {
  for (auto it = v.begin(); it != v.end(); ++it)
    if (*it == id) { v.erase(it); return; }
}

// File = 2025 bytes of pixels + 1 byte scroll speed (0 = not scrolling)
bool loadDrawing(uint16_t id, CRGB* dst, uint8_t* speed) {
  File f = LittleFS.open(savePath(id), "r");
  if (!f) return false;
  size_t n = f.read((uint8_t*)dst, FRAME_BYTES);
  uint8_t sp = 0;
  if (f.available()) sp = f.read();
  f.close();
  *speed = min(sp, MAX_SCROLL);
  return n == FRAME_BYTES;
}

void doSave() {
  static CRGB snap[NUM_LEDS];
  memcpy(snap, canvas, FRAME_BYTES);

  bool blank = true;
  for (auto& c : snap) if (c) { blank = false; break; }
  if (blank) { ws.textAll("{\"t\":\"msg\",\"m\":\"Draw something first!\"}"); return; }

  {
    std::lock_guard<std::mutex> g(savesLock);
    if (saves.size() + pending.size() >= MAX_SAVES) {
      ws.textAll("{\"t\":\"msg\",\"m\":\"The gallery is full right now.\"}");
      return;
    }
  }

  uint16_t id = nextId++;
  File f = LittleFS.open(savePath(id, REQUIRE_APPROVAL), "w");
  if (!f) { ws.textAll("{\"t\":\"msg\",\"m\":\"Couldn't save, sorry!\"}"); return; }
  f.write((uint8_t*)snap, FRAME_BYTES);
  f.write((uint8_t)(scrollOn ? scrollSpeed : 0));
  f.close();

  {
    std::lock_guard<std::mutex> g(savesLock);
    (REQUIRE_APPROVAL ? pending : saves).push_back(id);
  }

  char m[56];
  snprintf(m, sizeof m, "{\"t\":\"saved\",\"id\":%u,\"pending\":%d}", id, REQUIRE_APPROVAL ? 1 : 0);
  ws.textAll(m);
}

// ---------------------------------------------------------------- websocket
void sendFull(AsyncWebSocketClient* c) {
  static uint8_t buf[1 + FRAME_BYTES];
  buf[0] = OP_FULL;
  memcpy(buf + 1, canvas, FRAME_BYTES);
  if (c) c->binary(buf, sizeof buf);
  else   ws.binaryAll(buf, sizeof buf);
}

void sendUsers() {
  char m[32];
  snprintf(m, sizeof m, "{\"t\":\"users\",\"n\":%u}", (unsigned)ws.count());
  ws.textAll(m);
}

void sendScroll(AsyncWebSocketClient* c) {
  char m[48];
  snprintf(m, sizeof m, "{\"t\":\"scroll\",\"on\":%d,\"sp\":%u}", scrollOn ? 1 : 0, scrollSpeed);
  uint8_t off[2] = { OP_OFFSET, scrollOff };
  if (c) { c->text(m); c->binary(off, 2); }
  else   { ws.textAll(m); ws.binaryAll(off, 2); }
}

void handleMessage(uint8_t* d, size_t len) {
  switch (d[0]) {
    case OP_PIXELS: {          // [1, r, g, b, x0, y0, x1, y1, ...]
      if (len < 4) return;
      CRGB c(d[1], d[2], d[3]);
      for (size_t i = 4; i + 1 < len; i += 2) {
        uint8_t x = d[i], y = d[i + 1];
        if (x < GRID_W && y < GRID_H) canvas[y * GRID_W + x] = c;
      }
      ws.binaryAll(d, len);    // relay so every screen matches the wall
      lastInput = millis();
      break;
    }
    case OP_CLEAR:
      memset((void*)canvas, 0, FRAME_BYTES);
      sendFull(nullptr);
      lastInput = millis();
      break;
    case OP_SAVE:
      saveRequested = true;    // file I/O happens in loop()
      lastInput = millis();
      break;
    case OP_SCROLL:            // [4, on, speed]
      if (len < 3) return;
      scrollOn = d[1];
      scrollSpeed = constrain(d[2], 1, MAX_SCROLL);
      if (!scrollOn) scrollOff = 0;   // snap back to the original position
      sendScroll(nullptr);
      lastInput = millis();
      break;
  }
}

void onWsEvent(AsyncWebSocket* s, AsyncWebSocketClient* client, AwsEventType type,
               void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    if (s->count() > MAX_USERS) {
      client->text("{\"t\":\"full\"}");
      client->close();
      return;
    }
    lastInput = millis();
    sendFull(client);
    sendScroll(client);
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!info->final || info->index != 0 || info->len != len ||
        info->opcode != WS_BINARY || len < 1) return;
    handleMessage(data, len);
  }
}

// ---------------------------------------------------------------- slideshow
const uint8_t NUM_ANIMS = 3;

CRGB imgA[NUM_LEDS], imgB[NUM_LEDS];
CRGB* curImg = imgA;
CRGB* prevImg = imgB;
Item curItem{true, 0}, prevItem{true, 0};
uint8_t curSpeed = 0, prevSpeed = 0;      // scroll speed of each drawing slot
uint32_t itemStart = 0, prevStart = 0;
bool showing = false;

void renderItem(const Item& it, const CRGB* img, uint8_t speed, uint32_t start,
                uint32_t t, CRGB* out) {
  if (!it.anim) {
    if (speed) scrolledCopy(img, out, ((t - start) * speed / 1000) % GRID_W);
    else       memcpy(out, img, FRAME_BYTES);
    return;
  }
  for (uint8_t y = 0; y < GRID_H; y++)
    for (uint8_t x = 0; x < GRID_W; x++) {
      CRGB& p = out[y * GRID_W + x];
      switch (it.id) {
        case 0:   // diagonal rainbow
          p = CHSV((x + y) * 6 - t / 12, 255, 255);
          break;
        case 1: { // plasma
          uint8_t v = sin8(x * 14 + t / 9) / 3 + sin8(y * 12 - t / 13) / 3 +
                      sin8((x + y) * 9 + t / 17) / 3;
          p = CHSV(v + t / 60, 230, 255);
          break;
        }
        default: { // ripples from the center
          int dx = x - GRID_W / 2, dy = y - GRID_H / 2;
          uint8_t d = sqrt16(dx * dx + dy * dy);
          p = CHSV(t / 40 + d * 6, 255, sin8(d * 20 - t / 5));
          break;
        }
      }
    }
}

Item pickNext() {
  std::vector<uint16_t> list;
  { std::lock_guard<std::mutex> g(savesLock); list = saves; }
  for (int tries = 0; tries < 6; tries++) {
    Item it;
    if (list.empty() || random8() < 64) it = { true, (uint16_t)random8(NUM_ANIMS) };
    else                                it = { false, list[random16(list.size())] };
    if (it.anim != curItem.anim || it.id != curItem.id) return it;
  }
  return { true, (uint16_t)(curItem.anim ? (curItem.id + 1) % NUM_ANIMS : 0) };
}

void advance(uint32_t now) {
  Item next = pickNext();
  std::swap(curImg, prevImg);
  prevItem = curItem; prevSpeed = curSpeed; prevStart = itemStart;
  uint8_t sp = 0;
  if (!next.anim && !loadDrawing(next.id, curImg, &sp)) { next = { true, 0 }; sp = 0; }
  curItem = next;
  curSpeed = sp;
  itemStart = now;
}

void renderShow(uint32_t now, CRGB* out) {
  if (!showing) {            // entering slideshow: fade out of whatever was drawn
    showing = true;
    memcpy(curImg, canvas, FRAME_BYTES);
    curItem = { false, 0 };
    curSpeed = scrollOn ? scrollSpeed : 0;
    itemStart = curSpeed ? now - scrollOff * 1000UL / curSpeed : now;  // no jump
    advance(now);
  }
  uint32_t dur = curItem.anim ? ANIM_MS : SLIDE_MS;
  if (curSpeed) dur = max(dur, (uint32_t)(GRID_W * 1000UL / curSpeed));  // one full loop
  if (now - itemStart > dur) advance(now);

  renderItem(curItem, curImg, curSpeed, itemStart, now, out);
  uint32_t el = now - itemStart;
  if (el < FADE_MS) {
    static CRGB tmp[NUM_LEDS];
    renderItem(prevItem, prevImg, prevSpeed, prevStart, now, tmp);
    uint8_t a = el * 255 / FADE_MS;
    for (int i = 0; i < NUM_LEDS; i++) out[i] = blend(tmp[i], out[i], a);
  }
}

// ---------------------------------------------------------------- http
bool authed(AsyncWebServerRequest* r) {
  return r->hasParam("key") && r->getParam("key")->value() == ADMIN_KEY;
}

void setupRoutes() {
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/html", INDEX_HTML);
  });

  server.on("/admin", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "text/html", ADMIN_HTML);
  });

  // {"pending":[..],"approved":[..]}
  server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!authed(r)) { r->send(403, "text/plain", "bad key"); return; }
    String j = "{\"pending\":[";
    {
      std::lock_guard<std::mutex> g(savesLock);
      for (size_t i = 0; i < pending.size(); i++) { if (i) j += ','; j += (unsigned)pending[i]; }
      j += "],\"approved\":[";
      for (size_t i = 0; i < saves.size(); i++) { if (i) j += ','; j += (unsigned)saves[i]; }
    }
    j += "]}";
    r->send(200, "application/json", j);
  });

  // ?id=N  (&p=1 for a pending drawing)
  server.on("/api/img", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!authed(r) || !r->hasParam("id")) { r->send(403, "text/plain", "no"); return; }
    String p = savePath(r->getParam("id")->value().toInt(), r->hasParam("p"));
    if (!LittleFS.exists(p)) { r->send(404, "text/plain", "missing"); return; }
    r->send(LittleFS, p, "application/octet-stream");
  });

  server.on("/api/approve", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!authed(r) || !r->hasParam("id")) { r->send(403, "text/plain", "no"); return; }
    uint16_t id = r->getParam("id")->value().toInt();
    if (!LittleFS.rename(savePath(id, true), savePath(id))) {
      r->send(404, "text/plain", "missing");
      return;
    }
    {
      std::lock_guard<std::mutex> g(savesLock);
      eraseId(pending, id);
      saves.push_back(id);
    }
    r->send(200, "text/plain", "ok");
  });

  server.on("/api/del", HTTP_GET, [](AsyncWebServerRequest* r) {
    if (!authed(r) || !r->hasParam("id")) { r->send(403, "text/plain", "no"); return; }
    uint16_t id = r->getParam("id")->value().toInt();
    bool isPending = r->hasParam("p");
    LittleFS.remove(savePath(id, isPending));
    {
      std::lock_guard<std::mutex> g(savesLock);
      eraseId(isPending ? pending : saves, id);
    }
    r->send(200, "text/plain", "ok");
  });

  // Captive portal: every unknown URL (phone connectivity checks included)
  // gets redirected to the drawing page, which makes phones pop it open.
  server.onNotFound([](AsyncWebServerRequest* r) {
    r->redirect("http://192.168.4.1/");
  });

  server.begin();
}

// ---------------------------------------------------------------- main
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 256; i++)
    gammaLut[i] = GAMMA_CORRECT ? (uint8_t)(powf(i / 255.0f, 2.2f) * 255.0f + 0.5f) : i;

  FastLED.addLeds<WS2801, DATA_PIN, CLOCK_PIN, LED_ORDER, DATA_RATE_MHZ(SPI_MHZ)>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  FastLED.setMaxPowerInVoltsAndMilliamps(5, PSU_MA);
  bootWipe();

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  LittleFS.mkdir("/d");
  LittleFS.mkdir("/p");
  loadSaveList();

  IPAddress ip(192, 168, 4, 1);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(ip, ip, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, strlen(AP_PASS) ? AP_PASS : nullptr, 1, 0, MAX_USERS);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", ip);

  random16_set_seed(esp_random());
  setupRoutes();
  Serial.printf("Ready: join \"%s\", open http://192.168.4.1\n", AP_SSID);
}

void loop() {
  dns.processNextRequest();

  uint32_t now = millis();
  static uint32_t lastFrame = 0, lastCleanup = 0, emptySince = 0;
  static size_t lastCount = 999;
  static bool canvasCleared = true;

  if (now - lastCleanup > 1000) { lastCleanup = now; ws.cleanupClients(); }

  size_t n = ws.count();
  if (n != lastCount) { lastCount = n; sendUsers(); }

  if (n > 0) {
    emptySince = now;
    canvasCleared = false;
  } else if (!canvasCleared && now - emptySince > EMPTY_CLEAR_MS) {
    memset((void*)canvas, 0, FRAME_BYTES);   // fresh canvas for the next visitor
    scrollOn = false;
    scrollOff = 0;
    canvasCleared = true;
  }

  if (saveRequested) { saveRequested = false; doSave(); }

  if (now - lastFrame >= 30) {        // ~33 fps (675 LEDs = 16 kbit, ~16 ms at 1 MHz)
    uint32_t dt = min(now - lastFrame, (uint32_t)100);
    lastFrame = now;
    static CRGB frame[NUM_LEDS];
    static uint32_t scrollAcc = 0;
    uint32_t li = lastInput;
    bool drawMode = n > 0 && (int32_t)(now - li) < (int32_t)IDLE_MS;
    if (drawMode) {
      showing = false;
      if (scrollOn) {                  // step the scroll and keep phones in sync
        uint32_t period = 1000 / scrollSpeed;
        bool moved = false;
        for (scrollAcc += dt; scrollAcc >= period; scrollAcc -= period) {
          scrollOff = (scrollOff + 1) % GRID_W;
          moved = true;
        }
        if (moved) { uint8_t m[2] = { OP_OFFSET, scrollOff }; ws.binaryAll(m, 2); }
      } else {
        scrollAcc = 0;
      }
      scrolledCopy(canvas, frame, scrollOff);
    } else {
      renderShow(now, frame);
    }
    putFrame(frame);
    FastLED.show();
  }
}

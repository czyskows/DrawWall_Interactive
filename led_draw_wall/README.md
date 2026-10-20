# LED Draw Wall

ESP32-S3-mini + 27x25 WS2801 grid (two-wire: data + clock). The ESP32 makes its own Wi-Fi hotspot;
phones join (QR sign included) and the drawing page pops up via captive portal.

## Arduino IDE setup
- Board package: **esp32 by Espressif** (3.x). Board: *ESP32S3 Dev Module* (or *LOLIN S3 Mini*).
- Tools → USB CDC On Boot: **Enabled** (for Serial over USB).
- Tools → Partition Scheme: anything with a filesystem, e.g. *Default 4MB with spiffs* (LittleFS uses that partition).
- Libraries (Library Manager): **FastLED 3.9.x** (see below), **ESP Async WebServer** (ESP32Async), **Async TCP** (ESP32Async).

> **Pin FastLED to 3.9.x — do not update it to 3.10.x.** On 3.10, FastLED drives
> clocked chipsets like the WS2801 through the ESP32-S3's LCD_CAM peripheral, and
> that path panics during `addLeds()`: `esp_cache_msync: invalid addr or null
> pointer`, then an interrupt watchdog timeout, in a continuous boot loop. Because
> it dies before `WiFi.softAP()` runs, the *symptom* is "the Wi-Fi network never
> appears" — which looks nothing like an LED problem. Disabling that driver with
> `-DFASTLED_ESP32_HAS_LCD_SPI=0` doesn't help either; 3.10 then has no WS2801 bus
> for the S3 at all and fails to compile. Built and verified against **3.9.20**.

## Wiring (WS2801)
- **Data (DI/SDI):** GPIO 5 → first pixel (top-left).
- **Clock (CI/CKI):** GPIO 6 → first pixel.
- Run both through a 3.3 V → 5 V buffer (e.g. one 74AHCT125, two channels). WS2801 is more forgiving than WS2812, but long runs still like 5 V edges.
- Power: 5 V supply sized for `PSU_MA`; inject power at both ends (and the middle) of the grid. Common ground with the ESP32.
- Watch the arrows on the strip: DI/CI go **in**, DO/CO come **out** to the next pixel.

## Config (top of `led_draw_wall.ino`)
`DATA_PIN`, `CLOCK_PIN`, `SPI_MHZ`, `LED_ORDER`, `FIRST_ROW_RTL`, `BRIGHTNESS`, `PSU_MA`, `AP_SSID`, `AP_PASS`, `ADMIN_KEY` (change it!), `MAX_USERS`, timings.

## Checking the layout
On boot a **red** dot runs left → right across the **top** row, then a color wipe goes top → bottom.

As built, this wall wants `FIRST_ROW_RTL false`, `LED_ORDER RBG`, `SPI_MHZ 1`. If you
rebuild the grid or swap in different strips, re-check the following.

- Image mirrored left/right (dot runs the wrong way): flip `FIRST_ROW_RTL`. It sets
  the phase of the whole zigzag, not just the top row.
- Wrong colors: change `LED_ORDER`, swapping only the channels that are actually
  wrong. Here red was already correct while green showed blue and blue showed green
  — the 2nd and 3rd bytes transposed — so `RGB` became `RBG`.
- Flickery or random colors: the WS2801 is nominally a **1 MHz** part, so start by
  lowering `SPI_MHZ` (2 MHz flickered on this wall). Also shorten the data/clock
  runs and check the level shifter and grounds.
- Flicker that tracks how bright the drawing is: that's not the signal, it's the
  power limiter. `PSU_MA` caps total draw, and 675 pixels at `BRIGHTNESS 90` want
  roughly 14 A on a full-white canvas, so `setMaxPowerInVoltsAndMilliamps` dims
  dynamically as content changes. Raise the supply (and `PSU_MA`) or drop
  `BRIGHTNESS`. Sparse drawings looking calm while full ones pulse is the tell.
- Everything starts at the bottom: the grid is mounted upside down relative to the
  code (rotate it, or ask for a flip option).

## Using it
- Draw: pen / fill / eraser, 3 brush sizes, 12 colors + custom picker. Both people share one canvas.
- **Scroll**: the button slides the drawing left (wrapping around) on the wall and on both screens; the slider sets 1–20 columns/sec. You can keep drawing on the moving "paper". Saves remember the scroll, so they scroll in the slideshow too.
- **Save to the show** stores the drawing (max 300 total, pending + approved).
- Slideshow runs when nobody's connected, or nobody has drawn for 2 minutes. The canvas wipes 30 s after the last person leaves.
- **Approval**: new saves wait as "pending" until approved on the admin page (set `REQUIRE_APPROVAL false` to skip). The phone sees "Saved! It'll join the show once it's approved."
- Admin: `http://192.168.4.1/admin?key=YOUR_KEY` shows pending drawings (Approve / Delete) and the approved slideshow (Delete) (the admin phone counts toward the 2-person limit while connected).

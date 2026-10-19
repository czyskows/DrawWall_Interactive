# LED Draw Wall

ESP32-S3-mini + 27x25 WS2801 grid (two-wire: data + clock). The ESP32 makes its own Wi-Fi hotspot;
phones join (QR sign included) and the drawing page pops up via captive portal.

## Arduino IDE setup
- Board package: **esp32 by Espressif** (3.x). Board: *ESP32S3 Dev Module* (or *LOLIN S3 Mini*).
- Tools → USB CDC On Boot: **Enabled** (for Serial over USB).
- Tools → Partition Scheme: anything with a filesystem, e.g. *Default 4MB with spiffs* (LittleFS uses that partition).
- Libraries (Library Manager): **FastLED**, **ESP Async WebServer** (ESP32Async), **Async TCP** (ESP32Async).

## Wiring (WS2801)
- **Data (DI/SDI):** GPIO 5 → first pixel (top-right).
- **Clock (CI/CKI):** GPIO 6 → first pixel.
- Run both through a 3.3 V → 5 V buffer (e.g. one 74AHCT125, two channels). WS2801 is more forgiving than WS2812, but long runs still like 5 V edges.
- Power: 5 V supply sized for `PSU_MA`; inject power at both ends (and the middle) of the grid. Common ground with the ESP32.
- Watch the arrows on the strip: DI/CI go **in**, DO/CO come **out** to the next pixel.

## Config (top of `led_draw_wall.ino`)
`DATA_PIN`, `CLOCK_PIN`, `SPI_MHZ`, `LED_ORDER`, `FIRST_ROW_RTL`, `BRIGHTNESS`, `PSU_MA`, `AP_SSID`, `AP_PASS`, `ADMIN_KEY` (change it!), `MAX_USERS`, timings.

## Checking the layout
On boot a **red** dot runs left → right across the **top** row, then a color wipe goes top → bottom.
- Dot runs right → left: set `FIRST_ROW_RTL false`.
- Dot isn't red (e.g. blue): change `LED_ORDER` (try `BGR`, then `GRB`).
- Flickery or random colors: lower `SPI_MHZ`, shorten the data/clock wires, check the level shifter and grounds.
- Everything starts at the bottom: the grid is mounted upside down relative to the code (rotate it, or ask for a flip option).

## Using it
- Draw: pen / fill / eraser, 3 brush sizes, 12 colors + custom picker. Both people share one canvas.
- **Scroll**: the button slides the drawing left (wrapping around) on the wall and on both screens; the slider sets 1–20 columns/sec. You can keep drawing on the moving "paper". Saves remember the scroll, so they scroll in the slideshow too.
- **Save to the show** stores the drawing (max 300 total, pending + approved).
- Slideshow runs when nobody's connected, or nobody has drawn for 2 minutes. The canvas wipes 30 s after the last person leaves.
- **Approval**: new saves wait as "pending" until approved on the admin page (set `REQUIRE_APPROVAL false` to skip). The phone sees "Saved! It'll join the show once it's approved."
- Admin: `http://192.168.4.1/admin?key=YOUR_KEY` shows pending drawings (Approve / Delete) and the approved slideshow (Delete) (the admin phone counts toward the 2-person limit while connected).

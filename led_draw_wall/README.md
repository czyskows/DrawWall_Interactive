# LED Draw Wall

ESP32-S3-mini + 27x25 WS2812B grid. The ESP32 makes its own Wi-Fi hotspot;
phones join (QR sign included) and the drawing page pops up via captive portal.

## Arduino IDE setup
- Board package: **esp32 by Espressif** (3.x). Board: *ESP32S3 Dev Module* (or *LOLIN S3 Mini*).
- Tools → USB CDC On Boot: **Enabled** (for Serial over USB).
- Tools → Partition Scheme: anything with a filesystem, e.g. *Default 4MB with spiffs* (LittleFS uses that partition).
- Libraries (Library Manager): **FastLED**, **ESP Async WebServer** (ESP32Async), **Async TCP** (ESP32Async).

## Wiring
- Data: GPIO 5 → 330 Ω → first LED (top-right). A 74AHCT125 level shifter (3.3 V → 5 V) is strongly recommended.
- Power: 5 V supply sized for `PSU_MA`; inject 5 V at both ends (and the middle) of the grid. Common ground with the ESP32.

## Config (top of `led_draw_wall.ino`)
`LED_PIN`, `FIRST_ROW_RTL`, `BRIGHTNESS`, `PSU_MA`, `AP_SSID`, `AP_PASS`, `ADMIN_KEY` (change it!), `MAX_USERS`, timings.

## Checking the layout
On boot a red dot runs left → right across the **top** row, then a color wipe goes top → bottom.
- Dot runs right → left: set `FIRST_ROW_RTL false`.
- Everything starts at the bottom: the grid is mounted upside down relative to the code (rotate it, or ask for a flip option).

## Using it
- Draw: pen / fill / eraser, 3 brush sizes, 12 colors + custom picker. Both people share one canvas.
- **Scroll**: the button slides the drawing left (wrapping around) on the wall and on both screens; the slider sets 1–20 columns/sec. You can keep drawing on the moving "paper". Saves remember the scroll, so they scroll in the slideshow too.
- **Save to the show** stores the drawing (max 300 total, pending + approved).
- Slideshow runs when nobody's connected, or nobody has drawn for 2 minutes. The canvas wipes 30 s after the last person leaves.
- **Approval**: new saves wait as "pending" until approved on the admin page (set `REQUIRE_APPROVAL false` to skip). The phone sees "Saved! It'll join the show once it's approved."
- Admin: `http://192.168.4.1/admin?key=YOUR_KEY` shows pending drawings (Approve / Delete) and the approved slideshow (Delete) (the admin phone counts toward the 2-person limit while connected).

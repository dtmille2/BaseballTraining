/*
 * =============================================================================
 *  BASEBALL TIMER  (ESP32)
 * =============================================================================
 *
 *  WHAT IT DOES
 *  ------------
 *  The device listens through a microphone for a sharp sound (a "hit": a clap,
 *  a bat striking a ball, etc.). When it hears one, it starts a countdown timer.
 *  During the countdown it can play "tick" beeps, and when the countdown reaches
 *  zero it plays an end sound and turns on the red LED. Then it goes back to
 *  listening for the next hit.
 *
 *  The device creates its own Wi-Fi network called "Baseball Timer". When a
 *  phone joins it, the control page pops up automatically (a "captive portal",
 *  like hotel Wi-Fi sign-in pages). You can also open http://192.168.4.1 in a
 *  browser. The page shows a live display and lets you change the
 *  settings. Settings are saved in the ESP32's flash memory, so they survive a
 *  power cycle and the device can run without the phone once it's set up.
 *
 *  MODES
 *  -----
 *    DISARMED  : The mic still runs (the web graph still works), but hits are
 *                ignored. Useful for setup and tuning.
 *    ARMED     : Listening for a hit. A hit starts the countdown.
 *    COUNTING  : Countdown running. Further hits are ignored until it finishes.
 *
 *  Switch between armed and disarmed with the big button on the web page, or
 *  by pressing the BOOT button on the ESP32 board.
 *
 *  HARDWARE
 *  --------
 *    Board: ESP32 dev board (ESP-WROOM-32), 30-pin or 38-pin. In the Arduino
 *           IDE, select "ESP32 Dev Module". Requires the "esp32 by Espressif"
 *           board package, version 3.x or newer. On 30-pin boards the 5V pin
 *           is labeled VIN and GPIO pins are labeled D18, D19, and so on.
 *
 *    MAX98357A amplifier + speaker (one or two; wire a second amp exactly
 *    like the first, sharing the same three signal pins):
 *        VIN  -> 5V        GND -> GND
 *        BCLK -> GPIO 26   LRC -> GPIO 25   DIN -> GPIO 22
 *        SD, GAIN -> not connected
 *        Speaker wires -> amp's + and - terminals (never to GND)
 *        A 470-1000 uF capacitor across 5V and GND near the amps is
 *        recommended (stripe (-) to GND).
 *
 *    INMP441 microphone:
 *        VDD -> 3.3V (NOT 5V)   GND -> GND
 *        SCK -> GPIO 33         WS  -> GPIO 32   SD -> GPIO 35
 *        L/R -> GND
 *
 *    Status LEDs (each one: GPIO -> 330 ohm resistor -> LED long leg (+),
 *                 LED short leg (-) -> GND):
 *        Blue   GPIO 18   Power/running. Fast blink = mic or speaker problem.
 *        Yellow GPIO 19   Slow "breathing" pulse while armed.
 *        Red    GPIO 21   On when the timer expires.
 *        Green  GPIO 23   Flashes with each tick during the countdown
 *                         (solid for the whole countdown if ticks are off).
 *
 *    BOOT button (built into the board, GPIO 0): toggles armed/disarmed.
 *
 *  QUICK CUSTOMIZATION GUIDE (where to change common things)
 *  --------------------------------------------------------
 *    Wi-Fi network name / password .......... AP_SSID, AP_PASS
 *                                             (the case's QR code must match these)
 *    Page title / heading ................... <title> and <h1> in page.h
 *    Pin assignments ........................ "PIN ASSIGNMENTS" section
 *    LED color balance ...................... BAL_BLUE, BAL_YELLOW, ...
 *    Red LED "3 seconds" option length ...... RED_HOLD_MS
 *    Yellow breathing speed ................. ARMED_PULSE_MS
 *    What each sound sounds like ............ playSound()
 *    Tick lengths for each tick speed ....... tickLengthMs()
 *    Tick speed choices on the web page ..... the <select id="tickint"> in page.h,
 *                                             plus the checks in handleSet()
 *                                             and loadSettings()
 *    Hit detection tuning ................... "HIT DETECTION TUNING" section
 *    Default settings for a fresh board ..... loadSettings() (the second
 *                                             value in each prefs.get call)
 *
 *  HOW THE CODE IS ORGANIZED
 *  -------------------------
 *  The ESP32 has two processor cores, and this sketch runs three jobs at once:
 *
 *    1. micTask()   (core 1) : Reads the mic continuously, measures loudness,
 *                              and detects hits.
 *    2. soundTask() (core 0) : Waits for sound requests and plays them. Keeping
 *                              this separate means playing a sound never
 *                              freezes the web page or the mic.
 *    3. loop()      (core 1) : Runs the web server, checks the countdown timer,
 *                              reads the BOOT button, and updates the LEDs.
 *
 *  Because these run at the same time, variables shared between them are
 *  marked "volatile", and changes to the timer state are wrapped in
 *  portENTER_CRITICAL / portEXIT_CRITICAL so two tasks can't change it at the
 *  same moment.
 *
 *  Sections in this file, in order:
 *    1. Libraries
 *    2. Pin assignments
 *    3. Tunable constants (network, audio, hit detection, LEDs)
 *    4. Named values (sound IDs, tick modes, timer states)
 *    5. User settings (saved to flash)
 *    6. Runtime state (changes while running)
 *    7. Status LEDs
 *    8. Web page (the page itself is in page.h)
 *    9. Settings storage (loading from flash)
 *   10. Speaker / sound playback
 *   11. Timer logic (arming, hits, ticks, expiry)
 *   12. Microphone / hit detection
 *   13. Web request handlers (including captive portal)
 *   14. setup() and loop()
 * =============================================================================
 */


// =============================================================================
//  1. LIBRARIES
// =============================================================================
#include <WiFi.h>          // Wi-Fi hotspot
#include <DNSServer.h>     // Answers all web-name lookups, for the captive portal
#include <WebServer.h>     // Simple web server to serve the control page
#include <ESP_I2S.h>       // I2S digital audio, used for both mic and speaker
#include <Preferences.h>   // Saves settings to flash memory
#include <math.h>          // sinf, cosf, log10f


// =============================================================================
//  2. PIN ASSIGNMENTS
// =============================================================================
// Speaker amplifier (MAX98357A)
const int SPK_BCLK = 26;   // Bit clock
const int SPK_LRC  = 25;   // Left/right clock (also called word select)
const int SPK_DOUT = 22;   // Audio data going OUT to the amp

// Microphone (INMP441)
const int MIC_SCK  = 33;   // Bit clock
const int MIC_WS   = 32;   // Word select
const int MIC_SD   = 35;   // Audio data coming IN from the mic (35 is input-only, fine here)

// Status LEDs
const int LED_BLUE   = 18;
const int LED_YELLOW = 19;
const int LED_RED    = 21;
const int LED_GREEN  = 23;

// Other
const int LED_ONBOARD = 2;   // Small blue LED on the board. Not used; kept off.
const int BOOT_BTN    = 0;   // Built-in BOOT button. Reads LOW when pressed.

// Pins to AVOID if you rewire: 0, 2, 12, 15 (affect startup), 6-11 (flash
// memory), 1 and 3 (USB serial), 34-39 (input only, can't drive LEDs).


// =============================================================================
//  3. TUNABLE CONSTANTS
// =============================================================================

// ---- Network ----
const char* AP_SSID = "Baseball Timer";   // Wi-Fi network name the device creates
const char* AP_PASS = NULL;   // NULL = open network, no password. To add one, use a quoted
                              // string of 8+ characters, e.g. "baseball". NOTE: the Wi-Fi QR
                              // code printed on the case must be regenerated if this changes.

// ---- Audio ----
const int SAMPLE_RATE = 16000;   // Audio samples per second, for both mic and speaker.
                                 // 16 kHz is plenty for beeps and hit detection.
const int FADE_MS     = 5;       // Each tone fades in/out over this many ms to avoid clicks.
const int POST_SOUND_QUIET_MS = 150;  // Mic ignores hits for this long after a sound
                                      // finishes, so the device doesn't "hear" itself.

// ---- Hit detection tuning ----
// The mic is measured in blocks. For each block we compute a loudness in dB
// (0 dB = the loudest the mic can report; quieter sounds are negative numbers,
// e.g. a quiet room is roughly -85 dB, a nearby clap roughly -30 to -20 dB).
//
// A hit is detected when ALL of these are true:
//   - the block is louder than MIN_DB
//   - the block is louder than the background level by at least thresholdDb()
//     (thresholdDb() is set by the Sensitivity slider)
//   - at least REFRACTORY_MS has passed since the last hit
//   - the speaker isn't playing (or just finished playing)
const int   MIC_BLOCK     = 128;     // Samples per analysis block. 128 at 16 kHz = 8 ms.
const float MIN_DB        = -55.0f;  // Absolute minimum loudness for a hit. Raise (e.g.
                                     // -45) to ignore distant sounds; lower to catch
                                     // quieter ones.
const int   REFRACTORY_MS = 250;     // After a hit, ignore new hits for this long so one
                                     // clap (and its echo) only counts once.
const float BG_ADAPT_RATE = 0.01f;   // How fast the background level follows the room.
                                     // 0.01 per 8 ms block = about a 1 second response.
                                     // Larger = adapts faster.
const int   BG_HOLD_MS    = 300;     // After a hit, don't update the background for this
                                     // long, so the hit itself doesn't raise it.

// Sensitivity slider (0-100) -> how many dB above background a hit must jump.
//   Sensitivity   0 -> 30 dB (only very sharp, loud hits)
//   Sensitivity  50 -> 19 dB
//   Sensitivity 100 ->  8 dB (triggers easily)
const float THRESH_AT_SENS_0   = 30.0f;
const float THRESH_AT_SENS_100 = 8.0f;

// ---- LEDs ----
// Brightness balance, 0.0 - 1.0. Blue and green LEDs usually look much
// brighter than red and yellow at the same current, so they start lower.
// Adjust until all four look similar at the same brightness setting.
const float BAL_BLUE   = 0.5f;
const float BAL_YELLOW = 1.0f;
const float BAL_GREEN  = 0.5f;
const float BAL_RED    = 1.0f;

const int   ARMED_PULSE_MS  = 3000;   // One full yellow "breath" (dim -> bright -> dim)
const float ARMED_PULSE_MIN = 0.08f;  // Dimmest point of the yellow pulse (0.0 - 1.0)
const float GREEN_IDLE_GLOW = 0.25f;  // Green level between tick flashes during a countdown
const int   GREEN_FLASH_MAX_MS = 100; // Longest a green tick flash lasts
const int   RED_HOLD_MS     = 3000;   // How long red stays on when set to "3 seconds"
const int   ERROR_BLINK_MS  = 125;    // Blue blink speed when there's a hardware problem
const int   LAMP_TEST_MS    = 200;    // How long each LED lights during the power-up test


// =============================================================================
//  4. NAMED VALUES
// =============================================================================
// These give readable names to numbers. The numbers themselves matter in two
// places: the web page's <option value="..."> entries use them, and they're
// saved to flash. If you add new ones, add them at the END of a list so saved
// settings keep their meaning.

// Sound IDs, used with requestSound(). See playSound() for what each sounds like.
enum {
  SND_NONE = 0,
  SND_HIT,        // 1  Short high chirp when a hit starts the timer
  SND_BEEP,       // 2  Single beep (also the power-on beep)
  SND_TRIPLE,     // 3  Three beeps
  SND_DINGDONG,   // 4  Two-note doorbell
  SND_LONG,       // 5  One long tone
  SND_GO,         // 6  High "go" tone
  SND_TICK,       // 7  Countdown tick
  SND_ARM,        // 8  Rising two notes when armed
  SND_DISARM      // 9  Falling two notes when disarmed
};

// Countdown tick modes (the "Countdown ticks" dropdown)
enum {
  TICKS_OFF = 0,  // No ticks
  TICKS_ALL,      // Tick for the whole countdown
  TICKS_LAST3,    // Tick only during the last 3 seconds
  TICKS_LAST5     // Tick only during the last 5 seconds
};

// Timer states
enum {
  LISTENING = 0,  // Waiting for a hit (if armed)
  COUNTING  = 1   // Countdown running
};


// =============================================================================
//  5. USER SETTINGS (changed from the web page, saved to flash)
// =============================================================================
// The values below are just placeholders; the real starting values are loaded
// from flash in loadSettings(), which also holds the defaults used on a board
// that has never been set up.

volatile uint32_t timerDurMs     = 5000;      // Countdown length in milliseconds
volatile int      sensitivity    = 50;        // Hit sensitivity, 0-100 (see thresholdDb())
volatile float    volume         = 0.5f;      // Speaker volume, 0.0-1.0
volatile bool     beepOnHit      = true;      // Chirp when a hit starts the timer?
volatile bool     armOnBoot      = false;     // Start armed at power-up?
volatile int      ledBright      = 60;        // Overall LED brightness, 10-100 (%)
volatile bool     redHold3s      = false;     // true: red LED stays on RED_HOLD_MS.
                                              // false: red stays on until next hit/arm/disarm.
volatile int      endSound       = SND_GO;    // Sound played when the timer expires
volatile int      tickMode       = TICKS_ALL; // One of TICKS_OFF / ALL / LAST3 / LAST5
volatile uint32_t tickIntervalMs = 500;       // Time between ticks: 150, 250, 500 or 1000 ms


// =============================================================================
//  6. RUNTIME STATE (changes while the device runs; not saved)
// =============================================================================

// ---- Library objects ----
WebServer     server(80);   // Web server on the standard HTTP port
DNSServer     dnsServer;    // Captive portal: sends every web address to this device
I2SClass      i2sOut;       // I2S channel for the speaker
I2SClass      i2sMic;       // I2S channel for the microphone
Preferences   prefs;        // Flash storage for settings
QueueHandle_t soundQueue;   // "Mailbox" of sound IDs waiting for soundTask() to play

// Lock used to change the timer state safely from different tasks.
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// ---- Timer ----
volatile bool     armed      = false;      // true = hits start the timer
volatile int      timerState = LISTENING;  // LISTENING or COUNTING
volatile uint32_t timerEndMs = 0;          // millis() time when the countdown ends
volatile uint32_t nextTickAt = 0;          // millis() time of the next tick
volatile uint32_t ticksLeft  = 0;          // Ticks still to play in this countdown
volatile uint32_t doneCount  = 0;          // Countdowns finished since power-up. The web
                                           // page watches this to show "Time's up!".

// ---- Hit detection ----
volatile uint32_t hitCount      = 0;       // Total hits detected since power-up
                                           // (counted even when disarmed or counting)
volatile uint32_t lastHitMs     = 0;       // millis() time of the most recent hit
volatile float    levelDb       = -120;    // Loudness of the most recent mic block (dB)
volatile float    peakSincePoll = -120;    // Loudest block since the web page last asked.
                                           // Makes sure short claps show up on the graph.
volatile float    bgDb          = -70;     // Background (room) loudness, slowly updated
int               micChannel    = 0;       // Which I2S channel the mic uses: 0=left, 1=right.
                                           // Detected automatically at startup.

// ---- Speaker ----
volatile bool     speakerActive = false;   // true while a sound is playing
volatile uint32_t ignoreUntil   = 0;       // Mic ignores hits until this millis() time

// ---- LEDs ----
volatile bool     hwError         = false; // true if mic or speaker failed to start
volatile uint32_t greenFlashUntil = 0;     // Green is at full brightness until this time
volatile bool     redOn           = false; // Is the red "time's up" LED on?
volatile uint32_t redOffAt        = 0;     // When to turn red off. 0 = stay on.


// =============================================================================
//  Small helper used in several places
// =============================================================================

// Converts the Sensitivity slider (0-100) into how many dB above the background
// a sound must jump to count as a hit. Higher sensitivity = smaller jump needed.
float thresholdDb() {
  return THRESH_AT_SENS_0 + (THRESH_AT_SENS_100 - THRESH_AT_SENS_0) * (sensitivity / 100.0f);
}


// =============================================================================
//  7. STATUS LEDs
// =============================================================================
// LEDs are driven with PWM (rapid on/off switching) so their brightness can be
// set anywhere from off to full, which allows the pulsing and dim-glow effects.

// Sets one LED's brightness.
//   pin     : which LED
//   level   : 0.0 (off) to 1.0 (full), before the overall brightness setting
//   balance : that LED's BAL_ value, to even out the colors
// The value is squared because the eye doesn't see brightness linearly; this
// makes fades and brightness settings look smooth and even.
void ledSet(int pin, float level, float balance) {
  float b = level * (ledBright / 100.0f);
  uint32_t duty = (uint32_t)(b * b * balance * 255.0f + 0.5f);   // 0-255
  ledcWrite(pin, duty);
}

// Prepares the four LED pins for PWM and turns everything off.
void setupLeds() {
  const int pins[] = { LED_BLUE, LED_YELLOW, LED_GREEN, LED_RED };
  for (int p : pins) {
    ledcAttach(p, 5000, 8);   // 5 kHz PWM (no visible flicker), 8-bit (0-255) brightness
    ledcWrite(p, 0);
  }
  pinMode(LED_ONBOARD, OUTPUT);
  digitalWrite(LED_ONBOARD, LOW);
}

// Lights each LED in turn at power-up so you can check the wiring.
// Order: blue, yellow, green, red.
void lampTest() {
  const int   pins[] = { LED_BLUE, LED_YELLOW, LED_GREEN, LED_RED };
  const float bal[]  = { BAL_BLUE, BAL_YELLOW, BAL_GREEN, BAL_RED };
  for (int i = 0; i < 4; i++) {
    ledSet(pins[i], 1.0f, bal[i]);
    delay(LAMP_TEST_MS);
    ledcWrite(pins[i], 0);
  }
}

// Works out what every LED should be doing right now and sets it.
// Called constantly from loop().
void updateLeds() {
  uint32_t now = millis();

  // BLUE: solid while running. Blinks fast if the mic or speaker failed to start.
  float blue = 1.0f;
  if (hwError) blue = ((now / ERROR_BLINK_MS) % 2) ? 1.0f : 0.0f;

  // YELLOW: smooth "breathing" pulse while armed, off when disarmed.
  // A cosine wave goes smoothly from dim to bright and back once per ARMED_PULSE_MS.
  float yellow = 0;
  if (armed) {
    float phase = (now % ARMED_PULSE_MS) / (float)ARMED_PULSE_MS;          // 0.0 -> 1.0
    float wave  = 0.5f - 0.5f * cosf(2.0f * PI * phase);                   // 0 -> 1 -> 0
    yellow = ARMED_PULSE_MIN + (1.0f - ARMED_PULSE_MIN) * wave;
  }

  // GREEN: only during a countdown.
  //   Ticks off        -> solid for the whole countdown
  //   Ticks on         -> full brightness briefly on each tick, dim glow in between
  float green = 0;
  if (timerState == COUNTING) {
    if (tickMode == TICKS_OFF)                        green = 1.0f;
    else if ((int32_t)(greenFlashUntil - now) > 0)    green = 1.0f;
    else                                              green = GREEN_IDLE_GLOW;
  }

  // RED: on after the timer expires. If a turn-off time is set, turn off when reached.
  if (redOn && redOffAt && (int32_t)(now - redOffAt) >= 0) redOn = false;
  float red = redOn ? 1.0f : 0.0f;

  ledSet(LED_BLUE,   blue,   BAL_BLUE);
  ledSet(LED_YELLOW, yellow, BAL_YELLOW);
  ledSet(LED_GREEN,  green,  BAL_GREEN);
  ledSet(LED_RED,    red,    BAL_RED);
}


// =============================================================================
//  8. WEB PAGE
// =============================================================================
// The page itself (HTML, CSS and JavaScript) is in page.h, the other tab in
// the Arduino IDE. It's kept separate because the Arduino IDE mistakes the
// page's JavaScript for C++ when it's inside a .ino file.
//
// page.h also documents how the page talks to the ESP32 and how to add a
// new setting to the page.

#include "page.h"   // defines PAGE, the web page text


// =============================================================================
//  9. SETTINGS STORAGE
// =============================================================================
// Settings live in flash under the name "hittimer". Each has a short key.
// The second number in each prefs.get... call is the DEFAULT, used the first
// time the sketch runs on a board (or after flash is erased).
//
//   Key       Meaning                           Default
//   dur       Timer length (ms)                 5000
//   sens      Sensitivity 0-100                 50
//   vol       Volume 0-100                      50
//   beep      Chirp when timer starts           true
//   armboot   Start armed at power-up           false
//   bright    LED brightness 10-100             60
//   redhold   Red LED for 3 s (vs. until hit)   false
//   endsnd    End sound (SND_ number)           SND_GO
//   ticks     Tick mode (TICKS_ number)         TICKS_ALL
//   tickint   Tick interval (ms)                500
//
// Note: keys must be 15 characters or less.

void loadSettings() {
  prefs.begin("hittimer", false);   // false = read/write
  timerDurMs     = prefs.getUInt("dur", 5000);
  sensitivity    = prefs.getInt("sens", 50);
  volume         = prefs.getInt("vol", 50) / 100.0f;
  beepOnHit      = prefs.getBool("beep", true);
  armOnBoot      = prefs.getBool("armboot", false);
  ledBright      = prefs.getInt("bright", 60);
  redHold3s      = prefs.getBool("redhold", false);
  endSound       = prefs.getInt("endsnd", SND_GO);
  tickMode       = prefs.getInt("ticks", TICKS_ALL);
  tickIntervalMs = prefs.getUInt("tickint", 500);

  // Guard against an unexpected saved tick interval
  if (tickIntervalMs != 150 && tickIntervalMs != 250 && tickIntervalMs != 1000) tickIntervalMs = 500;

  armed = armOnBoot;   // start armed or disarmed as configured

  Serial.printf("Settings: timer %.1f s, sens %d, vol %d%%, endSound %d, ticks %d every %lu ms\n",
                timerDurMs / 1000.0f, sensitivity, (int)(volume * 100), endSound,
                tickMode, (unsigned long)tickIntervalMs);
}

// Tip: to reset all settings to defaults, temporarily add prefs.clear(); right
// after prefs.begin(...) above, upload, then remove it and upload again.


// =============================================================================
//  10. SPEAKER / SOUND PLAYBACK
// =============================================================================
// Sounds are generated in code as pure tones (sine waves); there are no sound
// files. Audio is sent to the amp as 16-bit stereo samples (the same value on
// left and right).

// Plays one tone and waits until it has been handed to the amp.
//   freq : pitch in Hz (higher = higher pitch; 440 = the note A)
//   ms   : length in milliseconds
void playTone(float freq, int ms) {
  const int   total = SAMPLE_RATE * ms / 1000;              // total samples to play
  const int   fade  = SAMPLE_RATE * FADE_MS / 1000;         // samples in each fade
  const float amp   = volume * volume * 0.8f * 32767;       // peak loudness. Squared for a
                                                            // natural volume curve; 0.8 leaves
                                                            // headroom to avoid distortion.
  const float inc   = 2.0f * PI * freq / SAMPLE_RATE;       // how far the wave advances per sample
  static int16_t buf[256 * 2];                              // 256 stereo samples at a time
  float phase = 0;
  int i = 0;

  while (i < total) {
    int n = min(256, total - i);
    for (int k = 0; k < n; k++, i++) {
      // Fade in at the start and out at the end to avoid clicks
      float env = 1.0f;
      if (i < fade)              env = (float)i / fade;
      else if (i > total - fade) env = (float)(total - i) / fade;

      int16_t s = (int16_t)(sinf(phase) * amp * env);
      buf[2 * k]     = s;   // left
      buf[2 * k + 1] = s;   // right
      phase += inc;
      if (phase > 2.0f * PI) phase -= 2.0f * PI;
    }
    i2sOut.write((uint8_t*)buf, n * 4);   // 4 bytes per stereo sample
  }
}

// Plays silence. Used for gaps between notes, and after each sound to flush
// the amp so it doesn't repeat leftover audio.
void playSilence(int ms) {
  static int16_t zeros[256 * 2] = {0};
  int total = SAMPLE_RATE * ms / 1000;
  while (total > 0) {
    int n = min(256, total);
    i2sOut.write((uint8_t*)zeros, n * 4);
    total -= n;
  }
}

// Length of a tick tone for the current tick speed. Faster ticks need shorter
// tones so each one is still distinct.
int tickLengthMs() {
  if (tickIntervalMs <= 150) return 45;
  if (tickIntervalMs <= 250) return 70;
  return 120;
}

// Defines what each sound ID sounds like. Edit the pitches (Hz) and lengths
// (ms) here to change the sounds.
void playSound(int id) {
  switch (id) {
    case SND_HIT:      playTone(1760, 60); break;                      // quick high chirp
    case SND_BEEP:     playTone(880, 200); break;
    case SND_TRIPLE:
      for (int j = 0; j < 3; j++) { playTone(1000, 150); if (j < 2) playSilence(100); }
      break;
    case SND_DINGDONG: playTone(1319, 250); playTone(1047, 400); break;
    case SND_LONG:     playTone(1000, 800); break;
    case SND_TICK:     playTone(660, tickLengthMs()); break;           // "ready, set..."
    case SND_GO:       playTone(1320, 450); break;                     // "...go!"
    case SND_ARM:      playTone(660, 100); playTone(990, 160); break;  // rising
    case SND_DISARM:   playTone(990, 100); playTone(660, 160); break;  // falling
  }
  // Short silence to flush the amp. Ticks use a shorter one to keep fast ticks on time.
  playSilence(id == SND_TICK ? 20 : 50);
}

// Asks for a sound to be played. Returns immediately; soundTask() plays it.
// If 4 sounds are already waiting, the new one is dropped.
void requestSound(int id) {
  xQueueSend(soundQueue, &id, 0);
}

// Background task: waits for sound requests and plays them one at a time.
// While playing (and briefly after), it tells the mic to ignore hits.
void soundTask(void*) {
  int id;
  for (;;) {
    if (xQueueReceive(soundQueue, &id, portMAX_DELAY)) {   // sleep until a request arrives
      speakerActive = true;
      playSound(id);
      ignoreUntil = millis() + POST_SOUND_QUIET_MS;
      speakerActive = false;
    }
  }
}


// =============================================================================
//  11. TIMER LOGIC
// =============================================================================

// Plans the ticks for a countdown that's just starting.
// Ticks are lined up with the END of the timer, so the last tick is always
// exactly one interval before the end sound. Example: 5 s timer, 0.5 s ticks
//   -> ticks at 0.5, 1.0, ... 4.5 s, then the end sound at 5.0 s.
// In "last 3 s" / "last 5 s" modes, only the ticks inside that window play.
// Must be called inside a timerMux critical section.
void armTicks() {
  ticksLeft = 0;
  if (tickMode == TICKS_OFF) return;

  uint32_t interval = tickIntervalMs;
  uint32_t k = (timerDurMs - 1) / interval;    // how many ticks fit before the end
  uint32_t cap = k;
  if (tickMode == TICKS_LAST3) cap = 3000 / interval;
  if (tickMode == TICKS_LAST5) cap = 5000 / interval;
  if (cap < k) k = cap;

  ticksLeft  = k;
  nextTickAt = timerEndMs - k * interval;      // time of the first tick
}

// Arms (true) or disarms (false) the device. Disarming cancels any countdown.
// Either one turns off the red "time's up" LED.
void setArmed(bool a) {
  portENTER_CRITICAL(&timerMux);
  armed = a;
  redOn = false;
  if (!a) {
    timerState = LISTENING;
    ticksLeft = 0;
  }
  portEXIT_CRITICAL(&timerMux);

  requestSound(a ? SND_ARM : SND_DISARM);
  Serial.println(a ? "ARMED - listening for a hit" : "DISARMED - not listening");
}

// Called by the mic task whenever a hit is detected.
//   now : millis() time of the hit
//   db  : how loud it was (for the Serial Monitor)
// Starts the countdown if armed and not already counting; otherwise ignores it.
void onHit(uint32_t now, float db) {
  hitCount++;
  bool started = false, wasArmed;

  portENTER_CRITICAL(&timerMux);
  wasArmed = armed;
  if (armed && timerState == LISTENING) {
    timerState = COUNTING;
    timerEndMs = now + timerDurMs;
    armTicks();
    redOn = false;           // clear the previous "time's up" light
    started = true;
  }
  portEXIT_CRITICAL(&timerMux);

  // Serial printing and sound requests happen outside the lock (they're slow)
  if (started) {
    Serial.printf("HIT (%.1f dB) -> timer started, %.1f s\n", db, timerDurMs / 1000.0f);
    if (beepOnHit) requestSound(SND_HIT);
  } else if (!wasArmed) {
    Serial.printf("HIT (%.1f dB) ignored, disarmed\n", db);
  } else {
    Serial.printf("HIT (%.1f dB) during countdown, ignored\n", db);
  }
}

// Called constantly from loop(). During a countdown, plays each tick when it's
// due, and handles the end of the countdown (end sound + red LED).
void checkTimer() {
  if (timerState != COUNTING) return;

  bool expired = false, tick = false;
  uint32_t now = millis();

  portENTER_CRITICAL(&timerMux);
  if (timerState == COUNTING) {
    // Note: "(int32_t)(a - b) >= 0" means "time a has reached time b". Written
    // this way, it still works when millis() wraps around after ~49 days.
    if ((int32_t)(now - timerEndMs) >= 0) {
      // Countdown finished
      timerState = LISTENING;
      ticksLeft = 0;
      doneCount++;
      redOn = true;
      redOffAt = redHold3s ? now + RED_HOLD_MS : 0;
      expired = true;
    } else if (ticksLeft > 0 && (int32_t)(now - nextTickAt) >= 0) {
      // A tick is due
      ticksLeft--;
      nextTickAt += tickIntervalMs;
      greenFlashUntil = now + min(GREEN_FLASH_MAX_MS, (int)(tickIntervalMs * 2 / 5));
      tick = true;
    }
  }
  portEXIT_CRITICAL(&timerMux);

  if (tick) requestSound(SND_TICK);
  if (expired) {
    Serial.println("Timer done!");
    requestSound(endSound);
  }
}


// =============================================================================
//  12. MICROPHONE / HIT DETECTION
// =============================================================================
// The INMP441 sends 24-bit audio inside 32-bit slots, on either the left or
// right channel. We read both channels and use whichever one has the mic.

static int32_t micBuf[MIC_BLOCK * 2];   // one block of raw stereo mic data

// Reads one block from the mic. Waits until the block is ready.
// Returns true if a full block was read.
bool readMicBlock() {
  size_t want = sizeof(micBuf);
  return i2sMic.readBytes((char*)micBuf, want) == want;
}

// Gets sample number k from channel ch (0=left, 1=right) of the current block,
// converted to a number between -1.0 and 1.0.
inline float micSample(int k, int ch) {
  return (micBuf[2 * k + ch] >> 8) / 8388608.0f;   // drop the unused low 8 bits; 2^23 = full scale
}

// Runs once at startup. Listens briefly and picks whichever channel has real
// audio on it (the one that varies the most). Flags a hardware error if the
// mic sends nothing usable.
void pickMicChannel() {
  for (int b = 0; b < 20; b++) readMicBlock();   // skip the first ~160 ms while the mic settles

  double sum[2] = {0, 0}, sq[2] = {0, 0};
  long n = 0;
  for (int b = 0; b < 40; b++) {                 // listen for ~320 ms
    if (!readMicBlock()) continue;
    for (int k = 0; k < MIC_BLOCK; k++) {
      for (int ch = 0; ch < 2; ch++) {
        float x = micSample(k, ch);
        sum[ch] += x;
        sq[ch]  += x * x;
      }
      n++;
    }
  }
  if (n == 0) {
    Serial.println("MIC: no data read - check wiring");
    hwError = true;
    return;
  }

  // Variance = how much each channel's signal moves around
  double var0 = sq[0] / n - pow(sum[0] / n, 2);
  double var1 = sq[1] / n - pow(sum[1] / n, 2);
  micChannel = (var1 > var0) ? 1 : 0;

  if (var0 == 0 && var1 == 0) {
    Serial.println("MIC: all zeros - check SD/SCK/WS wiring and 3.3V");
    hwError = true;
  }
  Serial.printf("MIC: using %s channel\n", micChannel ? "RIGHT" : "LEFT");
}

// Background task: runs forever, processing one 8 ms block of audio at a time.
// For each block it:
//   1. Removes any constant offset (DC) from the signal with a simple filter
//   2. Measures loudness in dB
//   3. Checks whether this block is a hit
//   4. Otherwise, slowly updates the background level
void micTask(void*) {
  float xPrev = 0, yPrev = 0;   // memory for the DC-removal filter
  bool bgReady = false;         // set after the first block initializes bgDb

  for (;;) {
    if (!readMicBlock()) continue;

    // 1 + 2: DC-removal filter, then average power of the block
    double sum = 0;
    for (int k = 0; k < MIC_BLOCK; k++) {
      float x = micSample(k, micChannel);
      float y = x - xPrev + 0.995f * yPrev;
      xPrev = x;
      yPrev = y;
      sum += y * y;
    }
    float db = 10.0f * log10f(sum / MIC_BLOCK + 1e-12f);   // +1e-12 avoids log(0)
    levelDb = db;
    if (db > peakSincePoll) peakSincePoll = db;

    uint32_t now = millis();
    if (!bgReady) { bgDb = db; bgReady = true; }

    // Don't listen while our own speaker is making noise (or just finished)
    bool muted = speakerActive || (int32_t)(now - ignoreUntil) < 0;

    // 3: Hit check (see "Hit detection tuning" at the top)
    if (!muted &&
        now - lastHitMs > REFRACTORY_MS &&
        db > MIN_DB &&
        db - bgDb > thresholdDb()) {
      lastHitMs = now;
      onHit(now, db);
    }
    // 4: Otherwise, let the background drift toward the current level
    else if (!muted && now - lastHitMs > BG_HOLD_MS) {
      bgDb = (1.0f - BG_ADAPT_RATE) * bgDb + BG_ADAPT_RATE * db;
    }
  }
}


// =============================================================================
//  13. WEB REQUEST HANDLERS
// =============================================================================
// Each function below answers one kind of request from the web page.
// They're connected to their web addresses in setup().

// GET /state : sends all current settings as JSON (text like {"dur":5.0,...}).
// The page uses this once when it opens to fill in the controls.
void sendState() {
  String json = String("{\"dur\":") + String(timerDurMs / 1000.0f, 1) +
                ",\"sens\":"    + sensitivity +
                ",\"vol\":"     + (int)(volume * 100) +
                ",\"beep\":"    + (beepOnHit ? "true" : "false") +
                ",\"armboot\":" + (armOnBoot ? "true" : "false") +
                ",\"bright\":"  + ledBright +
                ",\"redhold\":" + (redHold3s ? "true" : "false") +
                ",\"endsnd\":"  + endSound +
                ",\"ticks\":"   + tickMode +
                ",\"tickint\":" + tickIntervalMs + "}";
  server.send(200, "application/json", json);
}

// GET /level : sends live status as JSON. The page asks for this every 100 ms.
//   lvl   loudest mic level since last request (dB)
//   bg    background level (dB)
//   thr   how far above background a hit must be (dB)
//   hits  total hits detected
//   st    timer state: 0 = listening, 1 = counting
//   rem   ms remaining in the countdown (0 if not counting)
//   done  number of countdowns finished (page shows "Time's up!" when it rises)
//   armed true/false
void handleLevel() {
  float lvl = peakSincePoll;
  peakSincePoll = -120;                 // reset for the next request
  if (lvl < -119) lvl = levelDb;        // no new block since last time: use the latest

  uint32_t rem = 0;
  if (timerState == COUNTING) {
    int32_t r = (int32_t)(timerEndMs - millis());
    rem = r > 0 ? r : 0;
  }

  String json = String("{\"lvl\":") + String(lvl, 1) +
                ",\"bg\":"    + String((float)bgDb, 1) +
                ",\"thr\":"   + String(thresholdDb(), 1) +
                ",\"hits\":"  + hitCount +
                ",\"st\":"    + timerState +
                ",\"rem\":"   + rem +
                ",\"done\":"  + doneCount +
                ",\"armed\":" + (armed ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

// POST /set?k=<key>&v=<value> : changes one setting, saves it to flash, and
// replies with all settings. Values are checked and limited to safe ranges.
// See the key list in section 9.
void handleSet() {
  String k = server.arg("k");
  String v = server.arg("v");

  if (k == "dur") {
    float s = constrain(v.toFloat(), 0.1f, 3600.0f);   // 0.1 s to 1 hour
    timerDurMs = (uint32_t)(s * 1000);
    prefs.putUInt("dur", timerDurMs);

  } else if (k == "sens") {
    sensitivity = constrain(v.toInt(), 0, 100);
    prefs.putInt("sens", sensitivity);

  } else if (k == "vol") {
    int p = constrain(v.toInt(), 0, 100);
    volume = p / 100.0f;
    prefs.putInt("vol", p);

  } else if (k == "bright") {
    ledBright = constrain(v.toInt(), 10, 100);
    prefs.putInt("bright", ledBright);

  } else if (k == "redhold") {
    redHold3s = (v == "1");
    prefs.putBool("redhold", redHold3s);

  } else if (k == "armboot") {
    armOnBoot = (v == "1");
    prefs.putBool("armboot", armOnBoot);

  } else if (k == "beep") {
    beepOnHit = (v == "1");
    prefs.putBool("beep", beepOnHit);

  } else if (k == "endsnd") {
    int s = v.toInt();
    if (s >= SND_BEEP && s <= SND_GO) {   // only the sounds offered on the page
      endSound = s;
      prefs.putInt("endsnd", s);
    }

  } else if (k == "ticks") {
    tickMode = constrain(v.toInt(), TICKS_OFF, TICKS_LAST5);
    prefs.putInt("ticks", tickMode);

  } else if (k == "tickint") {
    int t = v.toInt();
    tickIntervalMs = (t == 150 || t == 250 || t == 1000) ? t : 500;   // allowed speeds only
    prefs.putUInt("tickint", tickIntervalMs);
  }

  Serial.printf("Setting %s = %s (saved)\n", k.c_str(), v.c_str());
  sendState();
}

// POST /arm?on=1 (arm) or /arm?on=0 (disarm)
void handleArm() {
  setArmed(server.arg("on") == "1");
  server.send(200, "text/plain", "ok");
}

// POST /cancel : stops a running countdown without playing the end sound
void handleCancel() {
  portENTER_CRITICAL(&timerMux);
  timerState = LISTENING;
  ticksLeft = 0;
  portEXIT_CRITICAL(&timerMux);
  Serial.println("Timer cancelled");
  server.send(200, "text/plain", "ok");
}

// POST /test : plays the currently selected end sound
void handleTest() {
  requestSound(endSound);
  server.send(200, "text/plain", "ok");
}

// ---- Captive portal ----
// When a phone joins a Wi-Fi network, it quietly checks whether it can reach
// the internet by loading a known test address (each phone maker uses its
// own). Two things make the control page pop up automatically:
//   1. dnsServer (started in setup) answers EVERY web-name lookup with this
//      device's address, so those test requests come here instead.
//   2. We answer them with a redirect to our page instead of the expected
//      reply. The phone decides it's a "sign-in" network and opens the page.
//
// Test addresses used by common systems:
//   Android / Chrome : /generate_204, /gen_204
//   Apple iOS / macOS: /hotspot-detect.html, /library/test/success.html
//   Windows          : /connecttest.txt, /ncsi.txt, /redirect, /fwlink
//   Firefox          : /canonical.html, /success.txt
// Anything not listed is also caught by onNotFound, which does the same thing.

// Sends the browser to the main page, using the full address so it works
// no matter which website name the phone asked for.
void redirectToPortal() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
  server.send(302, "text/plain", "");
}

// Any other address: send the browser to the main page
void handleNotFound() {
  redirectToPortal();
}


// =============================================================================
//  14. SETUP AND LOOP
// =============================================================================

// Runs once at power-up or reset.
void setup() {
  Serial.begin(115200);   // Serial Monitor speed; set the monitor to 115200 baud
  delay(1000);            // give the Serial Monitor time to connect
  pinMode(BOOT_BTN, INPUT_PULLUP);

  loadSettings();
  setupLeds();
  lampTest();             // blue, yellow, green, red in turn

  // Speaker: 16-bit stereo output
  i2sOut.setPins(SPK_BCLK, SPK_LRC, SPK_DOUT);
  if (!i2sOut.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Speaker I2S init FAILED");
    hwError = true;
  }

  // Microphone: 32-bit stereo input (-1 = no output pin)
  i2sMic.setPins(MIC_SCK, MIC_WS, -1, MIC_SD);
  if (!i2sMic.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO)) {
    Serial.println("Mic I2S init FAILED");
    hwError = true;
  } else {
    pickMicChannel();
  }

  // Start the background tasks.
  // Arguments: function, name, memory (bytes), unused, priority (higher wins), unused, core
  soundQueue = xQueueCreate(4, sizeof(int));   // holds up to 4 waiting sounds
  xTaskCreatePinnedToCore(soundTask, "sound", 4096, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(micTask,   "mic",   4096, NULL, 3, NULL, 1);

  // Start the Wi-Fi hotspot
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("Hotspot '%s' started. Open http://%s\n",
                AP_SSID, WiFi.softAPIP().toString().c_str());

  // Captive portal DNS: port 53 is the standard DNS port; "*" = every name
  dnsServer.start(53, "*", WiFi.softAPIP());

  // Connect web addresses to their handler functions
  server.on("/",       HTTP_GET,  [] { server.send_P(200, "text/html", PAGE); });
  server.on("/state",  HTTP_GET,  sendState);
  server.on("/level",  HTTP_GET,  handleLevel);
  server.on("/set",    HTTP_POST, handleSet);
  server.on("/cancel", HTTP_POST, handleCancel);
  server.on("/arm",    HTTP_POST, handleArm);
  server.on("/test",   HTTP_POST, handleTest);

  // Captive portal test addresses (see redirectToPortal())
  const char* portalChecks[] = {
    "/generate_204", "/gen_204",                              // Android
    "/hotspot-detect.html", "/library/test/success.html",     // Apple
    "/connecttest.txt", "/ncsi.txt", "/redirect", "/fwlink",  // Windows
    "/canonical.html", "/success.txt"                         // Firefox
  };
  for (const char* path : portalChecks) server.on(path, redirectToPortal);

  server.onNotFound(handleNotFound);
  server.begin();

  requestSound(SND_BEEP);              // power-on beep
  if (armed) requestSound(SND_ARM);    // plus the arm sound if starting armed
  Serial.println(armed ? "Started ARMED - listening for a hit" : "Started DISARMED");
}

// Runs over and over, forever (about 1000 times per second).
void loop() {
  dnsServer.processNextRequest();   // answer web-name lookups (captive portal)
  server.handleClient();            // answer any web requests
  checkTimer();            // ticks and countdown end

  // BOOT button: toggle armed/disarmed on each press.
  // Debounce: ignore changes within 50 ms of the last one (buttons "bounce"
  // electrically for a few ms when pressed).
  static bool lastBtn = HIGH;
  static unsigned long lastBtnChange = 0;
  bool btn = digitalRead(BOOT_BTN);
  if (btn != lastBtn && millis() - lastBtnChange > 50) {
    lastBtnChange = millis();
    lastBtn = btn;
    if (btn == LOW) setArmed(!armed);   // act on press, not release
  }

  updateLeds();

  // Status line to the Serial Monitor every 5 seconds
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 5000) {
    lastPrint = millis();
    Serial.printf("[%s] Level %.1f dB  Background %.1f dB  Hits %lu\n",
                  !armed ? "DISARMED" : (timerState == COUNTING ? "COUNTING" : "LISTENING"),
                  (float)levelDb, (float)bgDb, (unsigned long)hitCount);
  }

  delay(1);   // brief pause so this loop doesn't hog the processor
}

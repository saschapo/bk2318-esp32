// B&K 2318 / ZI 0054 test console for ESP32-C6, v0.
//
// Wiring (docs/wiring.md):
//   ESP32 3V3/GND -> SP3232 VCC/GND (3.3 V only!)
//   ESP32 TX -> T1IN,  T1OUT -> LEMO pin 5 (RD, printer input)
//   ESP32 RX <- R1OUT, R1IN  <- LEMO pin 3 (TD, printer output)
//   SP3232 GND -> LEMO pin 4 (SG). Pins 1 (EXTC) and 2 (HS) unconnected.
// The SP3232 inverts the signals, so the UART is NOT inverted here.
//
// Talk to it over USB Serial at 115200; type /help.

#include <Arduino.h>

// Board: MuseLab nanoESP32-C6. GPIO2/3 are broken out, not
// strapping, and free here. Avoid strapping 4, 5, 8, 9, 15, USB 12/13, UART0 16/17.
constexpr int PRN_TX_PIN = 2;
constexpr int PRN_RX_PIN = 3;
constexpr int NEOPIXEL_PIN = 8;  // onboard WS2812
constexpr uint32_t PRN_BAUD = 1200;  // ZI 0054 factory setting, matches seller's self-test

constexpr uint8_t XON = 0x11;
constexpr uint8_t XOFF = 0x13;

// One 8N1 character takes 10 bit times. Feeding the UART one character per
// character time keeps at most ~1 byte in flight when XOFF arrives, instead of
// a full 128-byte hardware FIFO.
constexpr uint32_t CHAR_TIME_US = 10UL * 1000000UL / PRN_BAUD + 200;

constexpr size_t QUEUE_SIZE = 4096;
uint8_t queueBuf[QUEUE_SIZE];
size_t queueHead = 0, queueTail = 0;

// The last printed line sits under the head, about 2 lines below the tear edge.
// After a job, once the queue has been idle for TAIL_IDLE_MS, feed tailLines
// empty lines so the end of the text comes out (docs/protocol.md). Off by
// default: at the printer the Paper Feed button does it (Power switch in the
// top On position); 3 lines is enough when nobody is there to press it.
constexpr uint32_t TAIL_IDLE_MS = 2000;
int tailLines = 0;
bool tailPending = false;
uint8_t lastQueued = 0;

bool paused = false;  // set by XOFF, cleared by XON
uint32_t lastSendUs = 0, lastSendMs = 0;
uint32_t sentBytes = 0, rxBytes = 0, xoffCount = 0, xonCount = 0;

enum class Eol { None, CR, LF, CRLF };
Eol eol = Eol::CRLF;

// Onboard LED: green = idle, blue = sending, red = paused by XOFF.
enum class Led { Off, Idle, Sending, Paused };
Led ledState = Led::Off;

void updateLed() {
  Led want = paused ? Led::Paused : (queueHead != queueTail ? Led::Sending : Led::Idle);
  if (want == ledState) return;
  ledState = want;
  switch (want) {
    case Led::Idle: rgbLedWrite(NEOPIXEL_PIN, 0, 8, 0); break;
    case Led::Sending: rgbLedWrite(NEOPIXEL_PIN, 0, 0, 24); break;
    case Led::Paused: rgbLedWrite(NEOPIXEL_PIN, 24, 0, 0); break;
    default: rgbLedWrite(NEOPIXEL_PIN, 0, 0, 0); break;
  }
}

// Loopback self-test capture (SP3232 RS-232 TX shorted to RX, printer not connected).
bool capturing = false;
String captured;

// Scope test: one byte every SCOPE_PERIOD_MS with idle line in between, so even a
// scope without single-shot trigger shows a stable frame. Printer NOT connected.
constexpr uint32_t SCOPE_PERIOD_MS = 25;
int scopeByte = -1;  // -1 = off
uint32_t lastScopeMs = 0;

size_t queueCount() { return (queueHead + QUEUE_SIZE - queueTail) % QUEUE_SIZE; }

bool enqueue(uint8_t b) {
  size_t next = (queueHead + 1) % QUEUE_SIZE;
  if (next == queueTail) return false;  // full
  queueBuf[queueHead] = b;
  queueHead = next;
  lastQueued = b;
  return true;
}

// n empty lines. The printer ignores CR right after LF and LF right after CR,
// so repeat whichever of them came last; after text, a CR first ends the line.
void enqueueFeeds(int n) {
  if (n <= 0) return;
  uint8_t c = lastQueued == '\n' ? '\n' : '\r';
  if (lastQueued != '\n' && lastQueued != '\r') enqueue('\r');
  for (int i = 0; i < n; i++) enqueue(c);
}

void enqueueBytes(const uint8_t *data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (!enqueue(data[i])) {
      Serial.println("! queue full, data dropped");
      return;
    }
  }
}

void enqueueString(const String &s) { enqueueBytes((const uint8_t *)s.c_str(), s.length()); }

void enqueueEol() {
  switch (eol) {
    case Eol::CR: enqueue('\r'); break;
    case Eol::LF: enqueue('\n'); break;
    case Eol::CRLF: enqueue('\r'); enqueue('\n'); break;
    case Eol::None: break;
  }
}

const char *eolName() {
  switch (eol) {
    case Eol::CR: return "CR";
    case Eol::LF: return "LF";
    case Eol::CRLF: return "CRLF";
    default: return "none";
  }
}

void pumpTx() {
  if (paused || queueCount() == 0) return;
  uint32_t now = micros();
  if (now - lastSendUs < CHAR_TIME_US) return;
  Serial1.write(queueBuf[queueTail]);
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  lastSendUs = now;
  lastSendMs = millis();
  sentBytes++;
}

void pumpRx() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    rxBytes++;
    if (capturing) {
      captured += (char)b;
      continue;
    }
    // In 7-bit mode the printer sends 7 data bits, so with 8N1 the 8th bit reads
    // as 1: XOFF arrives as 0x93 and XON as 0x91 (seen after NSR, docs/protocol.md).
    if ((b & 0x7F) == XOFF) {
      paused = true;
      xoffCount++;
      Serial.printf("[%lu ms] RX %02X XOFF, paused, %u byte(s) queued\n", millis(), b, (unsigned)queueCount());
    } else if ((b & 0x7F) == XON) {
      paused = false;
      xonCount++;
      Serial.printf("[%lu ms] RX %02X XON, resumed\n", millis(), b);
    } else {
      Serial.printf("[%lu ms] RX %02X%s%c\n", millis(), b, (b >= 0x20 && b < 0x7F) ? " '" : "", (b >= 0x20 && b < 0x7F) ? b : ' ');
    }
  }
}

// Parse "1B 29 4D" or "1b294d" into bytes.
bool parseHex(const String &s, String &out) {
  String digits;
  for (char c : s) {
    if (isxdigit((unsigned char)c)) digits += c;
    else if (c != ' ' && c != ',') return false;
  }
  if (digits.length() == 0 || digits.length() % 2) return false;
  for (size_t i = 0; i < digits.length(); i += 2) out += (char)strtol(digits.substring(i, i + 2).c_str(), nullptr, 16);
  return true;
}

void waitQueueEmpty(uint32_t timeoutMs) {
  uint32_t start = millis();
  while (queueCount() && millis() - start < timeoutMs) {
    pumpRx();
    pumpTx();
  }
}

void loopbackTest() {
  const String pattern = "LOOPBACK 0123 ABC xyz";
  captured = "";
  capturing = true;
  enqueueString(pattern);
  waitQueueEmpty(2000);
  uint32_t start = millis();
  while (captured.length() < pattern.length() && millis() - start < 500) pumpRx();
  capturing = false;
  Serial.printf("loopback: sent \"%s\", got \"%s\" -> %s\n", pattern.c_str(), captured.c_str(),
                captured == pattern ? "OK" : "FAIL");
}

void printHelp() {
  Serial.println(
      "Text without '/' is sent to the printer followed by the current line ending.\n"
      "/test        send \"HELLO 2318\" + line ending\n"
      "/cr /lf /crlf  send only that line ending\n"
      "/eol none|cr|lf|crlf  set the line ending appended to text (now: " + String(eolName()) + ")\n"
      "/ascii       print 0x20..0x7E in rows of 24 characters\n"
      "/hex 1B 4B   send raw bytes, manual ch. 5 only (1B 4B wide, 1B 4C normal, 04 EOT, 1F reset)\n"
      "/long N      send N lines of 24 characters to test XON/XOFF\n"
      "/feed N      feed N empty lines now\n"
      "/tail N      empty lines fed 2 s after each job so it clears the tear edge, 0 = off (now: " + String(tailLines) + ")\n"
      "/resume      clear the XOFF pause by hand\n"
      "/clear       drop everything still queued\n"
      "/loop        loopback self-test (SP3232 RS-232 TX tied to RX, no printer)\n"
      "/scope FF    repeat one byte every 25 ms for a scope, no printer; any command stops it\n"
      "/status      counters and state");
}

void handleCommand(String line) {
  line.trim();
  if (line.length() == 0) return;
  if (scopeByte >= 0) { scopeByte = -1; Serial.println("scope test stopped"); }
  if (line[0] != '/') {
    // Only printable ASCII goes out as text. Anything else is a printer command
    // (manual ch. 5): 0x04 EOT, 0x0E/0x0F/0x19/0x1D shifts, 0x1B ESC, 0x1F reset;
    // 0x80..0x9F are C1 commands in 8-bit mode, and in 7-bit mode the high bit is
    // dropped, so a UTF-8 byte such as 0x9F turns into 0x1F (reset).
    for (size_t i = 0; i < line.length(); i++) {
      uint8_t c = line[i];
      enqueue(c >= 0x20 && c < 0x7F ? c : '?');
    }
    enqueueEol();
    tailPending = true;
    return;
  }
  String cmd = line, arg;
  int sp = line.indexOf(' ');
  if (sp > 0) {
    cmd = line.substring(0, sp);
    arg = line.substring(sp + 1);
    arg.trim();
  }
  cmd.toLowerCase();

  if (cmd == "/help") printHelp();
  else if (cmd == "/test") { enqueueString("HELLO 2318"); enqueueEol(); tailPending = true; }
  else if (cmd == "/cr") enqueue('\r');
  else if (cmd == "/lf") enqueue('\n');
  else if (cmd == "/crlf") { enqueue('\r'); enqueue('\n'); }
  else if (cmd == "/eol") {
    arg.toLowerCase();
    if (arg == "none") eol = Eol::None;
    else if (arg == "cr") eol = Eol::CR;
    else if (arg == "lf") eol = Eol::LF;
    else if (arg == "crlf") eol = Eol::CRLF;
    else { Serial.println("usage: /eol none|cr|lf|crlf"); return; }
    Serial.printf("line ending: %s\n", eolName());
  } else if (cmd == "/ascii") {
    int col = 0;
    for (int c = 0x20; c < 0x7F; c++) {
      enqueue((uint8_t)c);
      if (++col == 24) { enqueueEol(); col = 0; }
    }
    if (col) enqueueEol();
    tailPending = true;
  } else if (cmd == "/hex") {
    String bytes;
    if (!parseHex(arg, bytes)) { Serial.println("usage: /hex 1B 29 4D"); return; }
    enqueueBytes((const uint8_t *)bytes.c_str(), bytes.length());
    tailPending = true;
  } else if (cmd == "/long") {
    int n = arg.length() ? arg.toInt() : 20;
    for (int i = 1; i <= n; i++) {
      char buf[32];
      snprintf(buf, sizeof buf, "LINE %03d ABCDEFGHIJKLMNO", i);  // exactly 24 chars
      enqueueString(buf);
      enqueueEol();
    }
    tailPending = true;
  } else if (cmd == "/feed") enqueueFeeds(arg.length() ? arg.toInt() : 1);
  else if (cmd == "/tail") {
    if (arg.length()) tailLines = constrain(arg.toInt(), 0, 20);
    Serial.printf("tail: %d line(s) after each job\n", tailLines);
  } else if (cmd == "/resume") { paused = false; Serial.println("resumed"); }
  else if (cmd == "/clear") { queueTail = queueHead; Serial.println("queue cleared"); }
  else if (cmd == "/loop") loopbackTest();
  else if (cmd == "/scope") {
    String b;
    if (arg.length() == 0) arg = "FF";
    if (!parseHex(arg, b) || b.length() != 1) { Serial.println("usage: /scope FF"); return; }
    scopeByte = (uint8_t)b[0];
    Serial.printf("scope test: 0x%02X every %lu ms, send any line to stop\n", scopeByte, SCOPE_PERIOD_MS);
  }
  else if (cmd == "/status") {
    Serial.printf("baud %lu, eol %s, tail %d, %s, queued %u, sent %lu, rx %lu, XOFF %lu, XON %lu\n", PRN_BAUD, eolName(), tailLines,
                  paused ? "PAUSED by XOFF" : "running", (unsigned)queueCount(), sentBytes, rxBytes, xoffCount, xonCount);
  } else Serial.println("unknown command, /help");
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(PRN_BAUD, SERIAL_8N1, PRN_RX_PIN, PRN_TX_PIN);
  delay(1500);
  Serial.printf("\nB&K 2318 console v0: UART1 %lu 8N1, TX GPIO%d, RX GPIO%d. /help\n", PRN_BAUD, PRN_TX_PIN, PRN_RX_PIN);
}

void loop() {
  static String line;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      handleCommand(line);
      line = "";
    } else {
      line += c;
    }
  }
  if (scopeByte >= 0 && queueCount() == 0 && millis() - lastScopeMs >= SCOPE_PERIOD_MS) {
    enqueue((uint8_t)scopeByte);
    lastScopeMs = millis();
  }
  if (tailPending && queueCount() == 0 && millis() - lastSendMs >= TAIL_IDLE_MS) {
    tailPending = false;
    enqueueFeeds(tailLines);
  }
  pumpRx();
  pumpTx();
  updateLed();
}

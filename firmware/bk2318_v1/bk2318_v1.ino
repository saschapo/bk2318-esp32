// B&K 2318 / ZI 0054 print server for ESP32-C6, v1: line protocol over USB.
//
// Wiring is the same as v0 (docs/wiring.md): GPIO2 TX -> SP3232 T1IN,
// GPIO3 RX <- R1OUT, module on 3V3; LEMO pins 5 (RD), 3 (TD), 4 (SG).
//
// Protocol: firmware/API_v1.md. One UTF-8 command per line; every command gets
// exactly one reply, "OK ..." or "ERR <code> ...". "EVT ..." lines may come at
// any time. Cyrillic is converted to the printer's Russian set (SCS 0x4C,
// uppercase only) via G1 + SO/SI, tested on the printer (docs/protocol.md).

#include <Arduino.h>

constexpr const char *FW_VERSION = "1";
constexpr int PRN_TX_PIN = 2;
constexpr int PRN_RX_PIN = 3;
constexpr int NEOPIXEL_PIN = 8;
constexpr uint32_t PRN_BAUD = 1200;  // ZI 0054 set-up on SN 1941478

constexpr uint8_t XON = 0x11, XOFF = 0x13, SO = 0x0E, SI = 0x0F, ESC = 0x1B, NSR = 0x1F;

// One character per character time, so at most ~1 byte is in flight when XOFF comes.
constexpr uint32_t CHAR_TIME_US = 10UL * 1000000UL / PRN_BAUD + 200;
// XOFF followed by XON this fast means the printer has just reset (power-on or NSR).
constexpr uint32_t RESET_PAIR_MS = 50;
constexpr size_t MAX_LINE = 1024;
constexpr int MAX_FEED = 20;

constexpr size_t QUEUE_SIZE = 4096;
uint8_t queueBuf[QUEUE_SIZE];
size_t queueHead = 0, queueTail = 0;
uint8_t lastQueued = 0;

bool paused = false;
uint32_t lastSendUs = 0, lastXoffMs = 0;
uint32_t sentBytes = 0, rxBytes = 0, xoffCount = 0, xonCount = 0, resetCount = 0;
bool drainPending = false;

// What we last told the printer; the printer forgets it on reset.
bool wide = false, inverse = false, alignRight = false;

size_t queueCount() { return (queueHead + QUEUE_SIZE - queueTail) % QUEUE_SIZE; }
size_t queueFree() { return QUEUE_SIZE - 1 - queueCount(); }

void enqueue(uint8_t b) {  // callers check queueFree() first
  queueBuf[queueHead] = b;
  queueHead = (queueHead + 1) % QUEUE_SIZE;
  lastQueued = b;
}

void enqueueAll(const String &bytes) {
  for (size_t i = 0; i < bytes.length(); i++) enqueue((uint8_t)bytes[i]);
}

// ---- UTF-8 text -> printer bytes ----

// KOI-7 positions of А..Я (U+0410..U+042F) in set 0x4C; Ъ has none and prints as Ь.
const uint8_t RU_KOI7[32] = {
    0x61, 0x62, 0x77, 0x67, 0x64, 0x65, 0x76, 0x7A,  // А Б В Г Д Е Ж З
    0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,  // И Й К Л М Н О П
    0x72, 0x73, 0x74, 0x75, 0x66, 0x68, 0x63, 0x7E,  // Р С Т У Ф Х Ц Ч
    0x7B, 0x7D, 0x78, 0x79, 0x78, 0x7C, 0x60, 0x71,  // Ш Щ Ъ Ы Ь Э Ю Я
};

// Returns the KOI-7 byte for a Cyrillic letter, or 0.
uint8_t cyrillic(uint32_t cp) {
  if (cp == 0x401 || cp == 0x451) cp = 0x415;  // Ё ё -> Е
  if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;  // lowercase -> uppercase
  if (cp >= 0x410 && cp <= 0x42F) return RU_KOI7[cp - 0x410];
  return 0;
}

// Plain ASCII stand-ins for typography that turns up in real names.
const char *asciiFor(uint32_t cp) {
  switch (cp) {
    case 0x09: case 0xA0: return " ";
    case 0xAB: case 0xBB: case 0x201C: case 0x201D: case 0x201E: return "\"";
    case 0x2018: case 0x2019: return "'";
    case 0x2013: case 0x2014: case 0x2212: return "-";
    case 0x2026: return "...";
    case 0x2116: return "N";
    default: return "?";
  }
}

// Encodes one line of UTF-8 for the printer, without the line ending. Only
// 0x20..0x7E reach the printer as text: other bytes are commands, and in 7-bit
// mode the high bit is dropped (0x9F would become 0x1F, a reset).
String encodeText(const String &utf8) {
  String out, body;
  bool ru = false, anyRu = false;
  size_t i = 0, n = utf8.length();
  while (i < n) {
    uint8_t b = utf8[i];
    uint32_t cp;
    int len = b < 0x80 ? 1 : (b & 0xE0) == 0xC0 ? 2 : (b & 0xF0) == 0xE0 ? 3 : (b & 0xF8) == 0xF0 ? 4 : 0;
    if (len == 0 || i + len > n) { cp = 0xFFFD; len = 1; }
    else {
      cp = len == 1 ? b : b & (0x7F >> len);
      for (int k = 1; k < len; k++) {
        uint8_t c = utf8[i + k];
        if ((c & 0xC0) != 0x80) { cp = 0xFFFD; len = k; break; }
        cp = (cp << 6) | (c & 0x3F);
      }
    }
    i += len;

    uint8_t koi = cyrillic(cp);
    if (koi) {
      if (!ru) { body += (char)SO; ru = anyRu = true; }
      body += (char)koi;
      continue;
    }
    if (ru) { body += (char)SI; ru = false; }
    if (cp >= 0x20 && cp < 0x7F) body += (char)cp;
    else body += asciiFor(cp);
  }
  if (ru) body += (char)SI;
  // Russian set into G1 on every such line: after a printer reset G1 is the
  // graphics set again, and SO would then start drawing.
  if (anyRu) { out += (char)ESC; out += ')'; out += 'L'; }
  out += body;
  return out;
}

String feeds(int count) {
  String s;
  char c = lastQueued == '\n' ? '\n' : '\r';  // CR right after LF (and LF after CR) is ignored
  if (lastQueued != '\n' && lastQueued != '\r') s += '\r';
  for (int i = 0; i < count; i++) s += c;
  return s;
}

// ---- replies ----

void reply(const String &s) { Serial.print(s); Serial.print('\n'); }

// Queues bytes for the printer and replies, or refuses the whole command.
void queueOrRefuse(const String &bytes, const String &ok = "OK") {
  if (bytes.length() > queueFree()) {
    reply("ERR FULL " + String((unsigned)queueFree()) + " bytes free");
    return;
  }
  enqueueAll(bytes);
  reply(ok);
}

bool parseOnOff(String arg, bool &out) {
  arg.toUpperCase();
  if (arg == "ON" || arg == "1") { out = true; return true; }
  if (arg == "OFF" || arg == "0") { out = false; return true; }
  return false;
}

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

String statusLine() {
  char buf[200];
  snprintf(buf, sizeof buf,
           "OK queued=%u free=%u paused=%d sent=%lu rx=%lu xoff=%lu xon=%lu resets=%lu wide=%d inverse=%d align=%s",
           (unsigned)queueCount(), (unsigned)queueFree(), paused, sentBytes, rxBytes, xoffCount, xonCount, resetCount,
           wide, inverse, alignRight ? "RIGHT" : "LEFT");
  return buf;
}

void handleLine(String line) {
  if (line.endsWith("\r")) line.remove(line.length() - 1);
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);  // PRINT keeps its spaces
  cmd.toUpperCase();
  if (cmd.length() == 0) return;

  if (cmd == "PRINT") {
    String bytes = encodeText(arg);
    bytes += "\r\n";
    queueOrRefuse(bytes);
  } else if (cmd == "FEED") {
    int n = arg.length() ? arg.toInt() : 1;
    if (n < 1 || n > MAX_FEED) { reply("ERR ARG FEED 1.." + String(MAX_FEED)); return; }
    queueOrRefuse(feeds(n));
  } else if (cmd == "WIDE" || cmd == "INVERSE") {
    bool on;
    if (!parseOnOff(arg, on)) { reply("ERR ARG " + cmd + " ON|OFF"); return; }
    String b;
    b += (char)ESC;
    b += cmd == "WIDE" ? (on ? 'K' : 'L') : (on ? 'H' : 'I');
    if (b.length() > queueFree()) { reply("ERR FULL"); return; }
    enqueueAll(b);
    (cmd == "WIDE" ? wide : inverse) = on;
    reply("OK");
  } else if (cmd == "ALIGN") {
    String a = arg;
    a.toUpperCase();
    if (a != "LEFT" && a != "RIGHT") { reply("ERR ARG ALIGN LEFT|RIGHT"); return; }
    String b;
    b += (char)ESC; b += '&'; b += a == "RIGHT" ? '>' : '<';
    if (b.length() > queueFree()) { reply("ERR FULL"); return; }
    enqueueAll(b);
    alignRight = a == "RIGHT";
    reply("OK");
  } else if (cmd == "RESET") {
    // NSR runs in the printer's own queue, after the lines before it are printed.
    queueOrRefuse(String((char)NSR));
  } else if (cmd == "RAW") {
    String b;
    if (!parseHex(arg, b)) { reply("ERR ARG RAW 1B 4B"); return; }
    queueOrRefuse(b);
  } else if (cmd == "CLEAR") {
    queueTail = queueHead;
    reply("OK");
  } else if (cmd == "DRAIN") {
    drainPending = true;  // replied from loop() once the queue is empty
  } else if (cmd == "STATUS") {
    reply(statusLine());
  } else if (cmd == "HELLO") {
    reply(String("OK bk2318 ") + FW_VERSION + " 24");
  } else if (cmd == "HELP") {
    reply("OK commands: PRINT text, FEED n, WIDE ON|OFF, INVERSE ON|OFF, ALIGN LEFT|RIGHT, RESET, RAW hex, "
          "CLEAR, DRAIN, STATUS, HELLO");
  } else {
    reply("ERR UNKNOWN " + cmd);
  }
}

// ---- printer I/O ----

void pumpTx() {
  if (paused || queueCount() == 0) return;
  uint32_t now = micros();
  if (now - lastSendUs < CHAR_TIME_US) return;
  Serial1.write(queueBuf[queueTail]);
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  lastSendUs = now;
  sentBytes++;
}

void pumpRx() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    rxBytes++;
    // In 7-bit mode the 8th bit reads as 1: XOFF is 0x93, XON is 0x91.
    if ((b & 0x7F) == XOFF) {
      paused = true;
      xoffCount++;
      lastXoffMs = millis();
      reply("EVT XOFF");
    } else if ((b & 0x7F) == XON) {
      bool wasReset = paused && millis() - lastXoffMs < RESET_PAIR_MS;
      paused = false;
      xonCount++;
      reply("EVT XON");
      if (wasReset) {
        resetCount++;
        wide = inverse = alignRight = false;
        reply("EVT PRINTER_RESET");
      }
    } else {
      char buf[16];
      snprintf(buf, sizeof buf, "EVT RX %02X", b);
      reply(buf);
    }
  }
}

void updateLed() {
  static int state = -1;
  int want = paused ? 2 : (queueCount() ? 1 : 0);
  if (want == state) return;
  state = want;
  if (want == 2) rgbLedWrite(NEOPIXEL_PIN, 24, 0, 0);       // red: XOFF
  else if (want == 1) rgbLedWrite(NEOPIXEL_PIN, 0, 0, 24);  // blue: sending
  else rgbLedWrite(NEOPIXEL_PIN, 0, 8, 0);                  // green: idle
}

void setup() {
  Serial.setRxBufferSize(2048);
  Serial.begin(115200);
  Serial1.begin(PRN_BAUD, SERIAL_8N1, PRN_RX_PIN, PRN_TX_PIN);
  delay(1500);
  reply(String("EVT BOOT bk2318 ") + FW_VERSION);
}

void loop() {
  static String line;
  static bool tooLong = false;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      if (tooLong) reply("ERR TOOLONG max " + String((unsigned)MAX_LINE) + " bytes");
      else handleLine(line);
      line = "";
      tooLong = false;
    } else if (line.length() < MAX_LINE) {
      line += c;
    } else {
      tooLong = true;
    }
  }
  pumpRx();
  pumpTx();
  if (drainPending && queueCount() == 0) {
    drainPending = false;
    reply("OK");
  }
  updateLed();
}

// B&K 2318 / ZI 0054 print server for ESP32-C6, v2: print jobs over USB.
//
// Wiring is the same as v0/v1 (docs/wiring.md): GPIO2 TX -> SP3232 T1IN,
// GPIO3 RX <- R1OUT, module on 3V3; LEMO pins 5 (RD), 3 (TD), 4 (SG).
//
// Protocol: firmware/API_v2.md. One UTF-8 command per line; every command gets
// exactly one reply, "OK ..." or "ERR <code> ...". "EVT ..." lines may come at
// any time. Printing goes in jobs: JOB BEGIN collects the lines here, JOB END
// checks that the printer is on (NSR -> XOFF+XON), loads the glyphs the job
// uses and only then sends it. Cyrillic goes through G1 + SO/SI, own glyphs
// (DRC) through SS3, the full-width rule through PDI in G2 (docs/graphics.md).

#include <Arduino.h>

constexpr const char *FW_VERSION = "2";
constexpr int PRN_TX_PIN = 2;
constexpr int PRN_RX_PIN = 3;
constexpr int NEOPIXEL_PIN = 8;
constexpr uint32_t PRN_BAUD = 1200;  // ZI 0054 set-up on SN 1941478

constexpr uint8_t XON = 0x11, XOFF = 0x13, SO = 0x0E, SI = 0x0F, ESC = 0x1B, SS3 = 0x1D, NSR = 0x1F;

// One character per character time, so at most ~1 byte is in flight when XOFF comes.
constexpr uint32_t CHAR_TIME_US = 10UL * 1000000UL / PRN_BAUD + 200;
// XOFF followed by XON this fast means the printer has just reset (power-on or NSR).
constexpr uint32_t RESET_PAIR_MS = 50;
// The pair comes 10-11 ms after NSR on an idle printer (docs/protocol.md).
constexpr uint32_t CHECK_MS = 300;
constexpr uint32_t LINE_MS = 1250;      // printer speed, 0.8 lines/s
// Keep the printer at most this far ahead (by the estimate), so that CLEAR
// cancels a job within a couple of lines instead of after its whole buffer.
constexpr uint32_t LEAD_MS = 2500;
constexpr uint32_t JOB_IDLE_MS = 5000;  // a job with a longer pause between commands is dropped
constexpr uint32_t UNPLUG_MS = 500;     // USB gone this long drops the open job
constexpr size_t MAX_LINE = 1024;
constexpr int MAX_FEED = 20;
constexpr int WIDTH = 24, WIDE_WIDTH = 12;

constexpr size_t JOB_MAX = 6144;     // printer bytes; larger jobs were never tested against XOFF
constexpr size_t QUEUE_SIZE = 16384;  // two full jobs with their glyphs
uint8_t queueBuf[QUEUE_SIZE];
size_t queueHead = 0, queueTail = 0;
uint8_t lastSent = 0;

bool paused = false;
uint32_t lastSendUs = 0, lastXoffMs = 0;
uint32_t sentBytes = 0, rxBytes = 0, xoffCount = 0, xonCount = 0, resetCount = 0;
uint32_t busyUntilMs = 0;  // estimate: when the printer prints the last line sent so far
bool drainPending = false;

// Declared before any function: the Arduino build puts its prototypes there.
struct Encoded {
  String bytes;    // for the printer, without the line ending
  String echo;     // what it prints, as UTF-8
  int cells = 0;   // one per printed character
  uint32_t glyphs = 0;
};

// ---- own glyphs (DRC), glyphs/drc_approved.txt ----

struct Glyph {
  uint32_t cp;      // Unicode code point the host sends
  uint32_t alt;     // second code point printed with the same glyph, or 0
  const char *utf8; // what ECHO shows
  uint8_t def[17];  // ESC C idx <7 columns x 2 bytes> ESC E
};

const Glyph GLYPHS[] = {
    {0x2713, 0, "✓", {0x1B, 0x43, 0x21, 0x40, 0x40, 0x42, 0x40, 0x41, 0x60, 0x41, 0x40, 0x46, 0x40, 0x58, 0x40, 0x40, 0x40}},
    {0x2717, 0, "✗", {0x1B, 0x43, 0x22, 0x40, 0x40, 0x48, 0x60, 0x45, 0x40, 0x42, 0x40, 0x45, 0x40, 0x48, 0x60, 0x40, 0x40}},
    {0x042A, 0x044A, "Ъ", {0x1B, 0x43, 0x23, 0x40, 0x40, 0x60, 0x40, 0x7F, 0x60, 0x44, 0x60, 0x44, 0x60, 0x43, 0x40, 0x40, 0x40}},
    {0x2610, 0, "☐", {0x1B, 0x43, 0x24, 0x7F, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x7F, 0x60}},
    {0x2026, 0, "…", {0x1B, 0x43, 0x25, 0x40, 0x60, 0x40, 0x40, 0x40, 0x40, 0x40, 0x60, 0x40, 0x40, 0x40, 0x40, 0x40, 0x60}},
    {0x2192, 0, "→", {0x1B, 0x43, 0x26, 0x40, 0x40, 0x44, 0x40, 0x44, 0x40, 0x55, 0x40, 0x4E, 0x40, 0x44, 0x40, 0x40, 0x40}},
    {0x2500, 0, "─", {0x1B, 0x43, 0x27, 0x44, 0x40, 0x44, 0x40, 0x44, 0x40, 0x44, 0x40, 0x44, 0x40, 0x44, 0x40, 0x44, 0x40}},
};
constexpr int GLYPH_COUNT = sizeof GLYPHS / sizeof GLYPHS[0];

int glyphFor(uint32_t cp) {
  for (int i = 0; i < GLYPH_COUNT; i++)
    if (GLYPHS[i].cp == cp || (GLYPHS[i].alt && GLYPHS[i].alt == cp)) return i;
  return -1;
}

// ---- job ----

uint8_t jobBuf[JOB_MAX];
size_t jobLen = 0;
bool jobOpen = false, jobWide = false;
uint32_t jobLastMs = 0, jobLines = 0, jobGlyphs = 0;  // jobGlyphs: bit i = GLYPHS[i] used

// JOB END in progress: 0 none, 1 waiting to send NSR, 2 NSR queued, 3 NSR sent.
int endState = 0;
uint32_t endStartMs = 0, checkSentMs = 0;
bool resetPairSeen = false;

size_t queueCount() { return (queueHead + QUEUE_SIZE - queueTail) % QUEUE_SIZE; }
size_t queueFree() { return QUEUE_SIZE - 1 - queueCount(); }

void enqueue(uint8_t b) {  // callers check queueFree() first
  queueBuf[queueHead] = b;
  queueHead = (queueHead + 1) % QUEUE_SIZE;
}

void enqueueAll(const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) enqueue(p[i]);
}

bool printerBusy() { return queueCount() > 0 || (int32_t)(busyUntilMs - millis()) > 0; }

// ---- UTF-8 text -> printer bytes ----

// KOI-7 positions of А..Я (U+0410..U+042F) in set 0x4C; Ъ is an own glyph.
const uint8_t RU_KOI7[32] = {
    0x61, 0x62, 0x77, 0x67, 0x64, 0x65, 0x76, 0x7A,  // А Б В Г Д Е Ж З
    0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70,  // И Й К Л М Н О П
    0x72, 0x73, 0x74, 0x75, 0x66, 0x68, 0x63, 0x7E,  // Р С Т У Ф Х Ц Ч
    0x7B, 0x7D, 0x00, 0x79, 0x78, 0x7C, 0x60, 0x71,  // Ш Щ Ъ Ы Ь Э Ю Я
};

// Uppercase Cyrillic code point the printer has, or 0.
uint32_t cyrillicUpper(uint32_t cp) {
  if (cp == 0x401 || cp == 0x451) cp = 0x415;  // Ё ё -> Е
  if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;  // lowercase -> uppercase
  if (cp >= 0x410 && cp <= 0x42F && RU_KOI7[cp - 0x410]) return cp;
  return 0;
}

// Marks and invisible characters take no cell: combining accents (the host
// should send NFC), variation selectors, zero-width spaces and joiners, BOM.
bool zeroWidth(uint32_t cp) {
  return (cp >= 0x300 && cp <= 0x36F) || (cp >= 0x200B && cp <= 0x200F) || cp == 0x2060 ||
         (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF || cp == 0xAD;
}

// Plain ASCII stand-ins for typography that turns up in real names; one cell each.
char asciiFor(uint32_t cp) {
  switch (cp) {
    case 0x09: case 0xA0: return ' ';
    case 0xAB: case 0xBB: case 0x201C: case 0x201D: case 0x201E: return '"';
    case 0x2018: case 0x2019: return '\'';
    case 0x2013: case 0x2014: case 0x2212: return '-';
    case 0x2116: return 'N';
    default: return '?';
  }
}

void appendUtf8(String &s, uint32_t cp) {
  if (cp < 0x80) s += (char)cp;
  else if (cp < 0x800) { s += (char)(0xC0 | cp >> 6); s += (char)(0x80 | (cp & 0x3F)); }
  else { s += (char)(0xE0 | cp >> 12); s += (char)(0x80 | (cp >> 6 & 0x3F)); s += (char)(0x80 | (cp & 0x3F)); }
}

// Encodes one line of UTF-8 for the printer. Only 0x20..0x7E reach the printer
// as text: other bytes are commands, and in 7-bit mode the high bit is dropped
// (0x9F would become 0x1F, a reset).
Encoded encodeText(const String &utf8) {
  Encoded e;
  String body;
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
    if (zeroWidth(cp)) continue;
    e.cells++;

    uint32_t ruCp = cyrillicUpper(cp);
    if (ruCp) {
      if (!ru) { body += (char)SO; ru = anyRu = true; }
      body += (char)RU_KOI7[ruCp - 0x410];
      appendUtf8(e.echo, ruCp);
      continue;
    }
    if (ru) { body += (char)SI; ru = false; }
    int g = glyphFor(cp);
    if (g >= 0) {
      body += (char)SS3;
      body += (char)GLYPHS[g].def[2];
      e.echo += GLYPHS[g].utf8;
      e.glyphs |= 1u << g;
    } else {
      char c = cp >= 0x20 && cp < 0x7F ? (char)cp : asciiFor(cp);
      body += c;
      e.echo += c;
    }
  }
  if (ru) body += (char)SI;
  // Russian set into G1 on every such line: after a printer reset G1 is the
  // graphics set again, and SO would then start drawing.
  if (anyRu) { e.bytes += (char)ESC; e.bytes += ')'; e.bytes += 'L'; }
  e.bytes += body;
  return e;
}

// Every line in a job ends with CRLF, so LF here: LF right after CR would be
// ignored, LF after LF is an empty line.
String feeds(int count) {
  String s;
  for (int i = 0; i < count; i++) s += '\n';
  return s;
}

// Line ends in printer bytes, counted like pumpTx(): CR, LF and CRLF are one line each.
uint32_t countLines(const String &b) {
  uint32_t n = 0;
  char prev = 0;
  for (char c : b) {
    bool eol = c == '\r' || c == '\n';
    bool pairEnd = eol && (prev == '\r' || prev == '\n') && prev != c;
    if (eol && !pairEnd) n++;
    prev = pairEnd ? 0 : c;
  }
  return n;
}

// ---- replies ----

void reply(const String &s) { Serial.print(s); Serial.print('\n'); }

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
  char buf[240];
  int32_t busy = (int32_t)(busyUntilMs - millis());
  snprintf(buf, sizeof buf,
           "OK job=%s jobbytes=%u queued=%u free=%u paused=%d busy=%ld sent=%lu rx=%lu xoff=%lu xon=%lu resets=%lu",
           jobOpen ? "open" : "none", (unsigned)jobLen, (unsigned)queueCount(), (unsigned)queueFree(), paused,
           (long)(busy > 0 ? (busy + 999) / 1000 : 0), sentBytes, rxBytes, xoffCount, xonCount, resetCount);
  return buf;
}

// ---- job commands ----

void dropJob() {
  jobOpen = false;
  jobLen = 0;
}

void abortJob(const char *why) {
  dropJob();
  reply(String("EVT JOB_ABORTED ") + why);
}

// Adds printer bytes to the open job; a job that grows too big is dropped.
bool jobAppend(const String &bytes, uint32_t lines) {
  if (jobLen + bytes.length() > JOB_MAX) {
    dropJob();
    reply("ERR TOOBIG job over " + String((unsigned)JOB_MAX) + " bytes, dropped");
    return false;
  }
  memcpy(jobBuf + jobLen, bytes.c_str(), bytes.length());
  jobLen += bytes.length();
  jobLines += lines;
  return true;
}

// Glyph definitions the job needs, then the job itself.
size_t jobSendSize(const String &prefix) {
  size_t n = prefix.length() + jobLen;
  for (int i = 0; i < GLYPH_COUNT; i++)
    if (jobGlyphs & (1u << i)) n += sizeof GLYPHS[i].def + 2;
  return n;
}

void sendJob(const String &prefix, bool checked) {
  const uint8_t ENDDRC[2] = {ESC, 0x45};
  enqueueAll((const uint8_t *)prefix.c_str(), prefix.length());
  for (int i = 0; i < GLYPH_COUNT; i++) {
    if (!(jobGlyphs & (1u << i))) continue;
    enqueueAll(GLYPHS[i].def, sizeof GLYPHS[i].def);
    enqueueAll(ENDDRC, 2);
  }
  enqueueAll(jobBuf, jobLen);
  // Estimate: sending and printing overlap, whichever is slower decides.
  uint32_t sendMs = queueCount() * 1000UL / (PRN_BAUD / 10);
  int32_t busy = (int32_t)(busyUntilMs - millis());
  uint32_t printMs = (busy > 0 ? busy : 0) + jobLines * LINE_MS;
  uint32_t eta = ((sendMs > printMs ? sendMs : printMs) + 999) / 1000;
  char buf[96];
  snprintf(buf, sizeof buf, "OK lines=%lu bytes=%u eta=%lu check=%s", jobLines, (unsigned)jobLen, eta,
           checked ? "ok" : "busy");
  reply(buf);
  dropJob();
  endState = 0;
}

// Runs JOB END to completion across loop() passes.
void pumpEnd() {
  if (endState == 1) {
    if (printerBusy()) {
      // The printer is still printing the previous job, so it is on; NSR would
      // only run after that job (minutes). Reset the style by hand instead.
      String prefix;
      prefix += (char)SI;
      prefix += (char)ESC; prefix += 'L';               // normal width
      prefix += (char)ESC; prefix += 'I';               // normal video
      prefix += (char)ESC; prefix += '&'; prefix += '<';  // align left
      if (jobSendSize(prefix) <= queueFree()) sendJob(prefix, false);
      return;  // no room yet: the queue drains at 120 bytes/s
    }
    resetPairSeen = false;
    enqueue(NSR);
    endState = 2;
  } else if (endState == 2) {
    // NSR stuck behind an XOFF that never got its XON: the printer went away.
    if (millis() - endStartMs > 2000) {
      queueTail = queueHead;
      paused = false;
      dropJob();
      endState = 0;
      reply("ERR PRINTER_OFF reset not sent (XOFF), job dropped");
    }
  } else if (endState == 3) {
    if (resetPairSeen) sendJob("", true);
    else if (millis() - checkSentMs > CHECK_MS) {
      dropJob();
      endState = 0;
      reply("ERR PRINTER_OFF no reply to reset, job dropped");
    }
  }
}

// ---- command dispatch ----

bool needJob() {
  if (jobOpen) return true;
  reply("ERR NOJOB JOB BEGIN first");
  return false;
}

void handleLine(String line) {
  if (line.endsWith("\r")) line.remove(line.length() - 1);
  int sp = line.indexOf(' ');
  String cmd = sp < 0 ? line : line.substring(0, sp);
  String arg = sp < 0 ? "" : line.substring(sp + 1);  // PRINT keeps its spaces
  cmd.toUpperCase();
  if (cmd.length() == 0) return;
  if (jobOpen) jobLastMs = millis();

  if (cmd == "JOB") {
    String a = arg;
    a.toUpperCase();
    if (a == "BEGIN") {
      if (jobOpen) { reply("ERR INJOB job already open"); return; }
      jobOpen = true;
      jobLen = 0;
      jobLines = 0;
      jobGlyphs = 0;
      jobWide = false;
      jobLastMs = millis();
      reply("OK");
    } else if (a == "END") {
      if (!needJob()) return;
      jobOpen = false;  // the idle timeout no longer applies
      endState = 1;     // replied from pumpEnd()
      endStartMs = millis();
    } else if (a == "CANCEL") {
      if (!needJob()) return;
      dropJob();
      reply("OK");
    } else {
      reply("ERR ARG JOB BEGIN|END|CANCEL");
    }
  } else if (cmd == "PRINT") {
    if (!needJob()) return;
    Encoded e = encodeText(arg);
    int w = jobWide ? WIDE_WIDTH : WIDTH;
    uint32_t lines = e.cells ? (e.cells + w - 1) / w : 1;  // the printer wraps by itself
    if (jobAppend(e.bytes + "\r\n", lines)) {
      jobGlyphs |= e.glyphs;
      reply("OK");
    }
  } else if (cmd == "FEED") {
    if (!needJob()) return;
    int n = arg.length() ? arg.toInt() : 1;
    if (n < 1 || n > MAX_FEED) { reply("ERR ARG FEED 1.." + String(MAX_FEED)); return; }
    if (jobAppend(feeds(n), n)) reply("OK");
  } else if (cmd == "RULE") {
    if (!needJob()) return;
    String a = arg;
    a.toUpperCase();
    int type = a == "SOLID" ? 0 : a == "DOTS" ? 1 : a == "DASH" ? 2 : a == "DASHDOT" ? 3 : -1;
    if (type < 0) { reply("ERR ARG RULE SOLID|DOTS|DASH|DASHDOT"); return; }
    // PDI into G2 and locked in, LLINE in the middle row (4), back to G0.
    String b;
    b += (char)ESC; b += (char)0x2E; b += (char)0x57;
    b += (char)ESC; b += (char)0x6E;
    b += (char)0x28; b += (char)(0x60 | type << 3 | 4);
    b += (char)SI;
    b += "\r\n";
    if (jobAppend(b, 1)) reply("OK");
  } else if (cmd == "WIDE" || cmd == "INVERSE") {
    if (!needJob()) return;
    bool on;
    if (!parseOnOff(arg, on)) { reply("ERR ARG " + cmd + " ON|OFF"); return; }
    String b;
    b += (char)ESC;
    b += cmd == "WIDE" ? (on ? 'K' : 'L') : (on ? 'H' : 'I');
    if (jobAppend(b, 0)) {
      if (cmd == "WIDE") jobWide = on;
      reply("OK");
    }
  } else if (cmd == "ALIGN") {
    if (!needJob()) return;
    String a = arg;
    a.toUpperCase();
    if (a != "LEFT" && a != "RIGHT") { reply("ERR ARG ALIGN LEFT|RIGHT"); return; }
    String b;
    b += (char)ESC; b += '&'; b += a == "RIGHT" ? '>' : '<';
    if (jobAppend(b, 0)) reply("OK");
  } else if (cmd == "RAW") {
    String b;
    if (!parseHex(arg, b)) { reply("ERR ARG RAW 1B 4B"); return; }
    if (jobOpen) {
      if (jobAppend(b, countLines(b))) reply("OK");
    } else if (b.length() > queueFree()) {
      reply("ERR FULL " + String((unsigned)queueFree()) + " bytes free");
    } else {
      enqueueAll((const uint8_t *)b.c_str(), b.length());
      reply("OK");
    }
  } else if (cmd == "ECHO") {
    reply("OK " + encodeText(arg).echo);
  } else if (cmd == "CLEAR") {
    size_t dropped = queueCount();
    queueTail = queueHead;
    // Cut mid-line: end the line, or the next job's first line would join it.
    if (dropped && lastSent != 0 && lastSent != '\r' && lastSent != '\n') {
      enqueue(SI);
      enqueue('\r');
      enqueue('\n');
    }
    reply("OK dropped=" + String((unsigned)dropped));
  } else if (cmd == "DRAIN") {
    drainPending = true;  // replied from loop() once the queue is empty
  } else if (cmd == "STATUS") {
    reply(statusLine());
  } else if (cmd == "HELLO") {
    String s = String("OK bk2318 ") + FW_VERSION + " " + WIDTH + " glyphs=";
    for (int i = 0; i < GLYPH_COUNT; i++) s += GLYPHS[i].utf8;
    s += " rule=SOLID,DOTS,DASH,DASHDOT job=" + String((unsigned)JOB_MAX) +
         " lead=" + String((unsigned)LEAD_MS);
    reply(s);
  } else if (cmd == "HELP") {
    reply("OK commands: JOB BEGIN|END|CANCEL, PRINT text, FEED n, RULE style, WIDE ON|OFF, INVERSE ON|OFF, "
          "ALIGN LEFT|RIGHT, ECHO text, RAW hex, CLEAR, DRAIN, STATUS, HELLO");
  } else {
    reply("ERR UNKNOWN " + cmd);
  }
}

// ---- printer I/O ----

void pumpTx() {
  if (paused || queueCount() == 0) return;
  if ((int32_t)(busyUntilMs - millis()) > (int32_t)LEAD_MS) return;
  uint32_t now = micros();
  if (now - lastSendUs < CHAR_TIME_US) return;
  uint8_t b = queueBuf[queueTail];
  Serial1.write(b);
  queueTail = (queueTail + 1) % QUEUE_SIZE;
  lastSendUs = now;
  sentBytes++;
  // CR, LF and CRLF print one line each; CR right after LF (or LF after CR) is ignored.
  bool eol = b == '\r' || b == '\n';
  bool pairEnd = eol && (lastSent == '\r' || lastSent == '\n') && lastSent != b;
  if (eol && !pairEnd) {
    uint32_t ms = millis();
    if ((int32_t)(busyUntilMs - ms) < 0) busyUntilMs = ms;
    busyUntilMs += LINE_MS;
  }
  lastSent = pairEnd ? 0 : b;
  if (b == NSR && endState == 2) {
    endState = 3;
    checkSentMs = millis();
  }
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
        busyUntilMs = millis();  // the reset wipes the printer's line buffer
        if (endState == 3) resetPairSeen = true;  // our own check, not news
        else reply("EVT PRINTER_RESET");
      }
    } else {
      char buf[16];
      snprintf(buf, sizeof buf, "EVT RX %02X", b);
      reply(buf);
    }
  }
}

void watchJob() {
  static uint32_t pluggedMs = 0;
  if (HWCDC::isPlugged()) pluggedMs = millis();
  if (!jobOpen) return;
  if (millis() - pluggedMs > UNPLUG_MS) abortJob("USB");
  else if (millis() - jobLastMs > JOB_IDLE_MS) abortJob("TIMEOUT");
}

void updateLed() {
  static int state = -1;
  int want = paused ? 3 : queueCount() ? 2 : (jobOpen || endState) ? 1 : 0;
  if (want == state) return;
  state = want;
  if (want == 3) rgbLedWrite(NEOPIXEL_PIN, 24, 0, 0);       // red: XOFF
  else if (want == 2) rgbLedWrite(NEOPIXEL_PIN, 0, 0, 24);  // blue: sending
  else if (want == 1) rgbLedWrite(NEOPIXEL_PIN, 16, 12, 0); // yellow: job open or being checked
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
  // While JOB END or DRAIN waits, the next command stays in the USB buffer.
  while (!endState && !drainPending && Serial.available()) {
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
  pumpEnd();
  watchJob();
  if (drainPending && queueCount() == 0) {
    drainPending = false;
    reply("OK");
  }
  updateLed();
}

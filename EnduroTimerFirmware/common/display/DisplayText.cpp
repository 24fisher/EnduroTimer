#include "DisplayText.h"

namespace {
void appendLimited(String& out, const char* text, size_t maxLen) {
  for (const char* p = text; *p != '\0' && out.length() < maxLen; ++p) {
    out += *p;
  }
}

void appendTransliteratedCyrillic(String& out, uint16_t codepoint, size_t maxLen) {
  switch (codepoint) {
    case 0x0410: appendLimited(out, "A", maxLen); break; case 0x0430: appendLimited(out, "a", maxLen); break;
    case 0x0411: appendLimited(out, "B", maxLen); break; case 0x0431: appendLimited(out, "b", maxLen); break;
    case 0x0412: appendLimited(out, "V", maxLen); break; case 0x0432: appendLimited(out, "v", maxLen); break;
    case 0x0413: appendLimited(out, "G", maxLen); break; case 0x0433: appendLimited(out, "g", maxLen); break;
    case 0x0414: appendLimited(out, "D", maxLen); break; case 0x0434: appendLimited(out, "d", maxLen); break;
    case 0x0415: case 0x0401: appendLimited(out, "E", maxLen); break; case 0x0435: case 0x0451: appendLimited(out, "e", maxLen); break;
    case 0x0416: appendLimited(out, "Zh", maxLen); break; case 0x0436: appendLimited(out, "zh", maxLen); break;
    case 0x0417: appendLimited(out, "Z", maxLen); break; case 0x0437: appendLimited(out, "z", maxLen); break;
    case 0x0418: appendLimited(out, "I", maxLen); break; case 0x0438: appendLimited(out, "i", maxLen); break;
    case 0x0419: appendLimited(out, "Y", maxLen); break; case 0x0439: appendLimited(out, "y", maxLen); break;
    case 0x041A: appendLimited(out, "K", maxLen); break; case 0x043A: appendLimited(out, "k", maxLen); break;
    case 0x041B: appendLimited(out, "L", maxLen); break; case 0x043B: appendLimited(out, "l", maxLen); break;
    case 0x041C: appendLimited(out, "M", maxLen); break; case 0x043C: appendLimited(out, "m", maxLen); break;
    case 0x041D: appendLimited(out, "N", maxLen); break; case 0x043D: appendLimited(out, "n", maxLen); break;
    case 0x041E: appendLimited(out, "O", maxLen); break; case 0x043E: appendLimited(out, "o", maxLen); break;
    case 0x041F: appendLimited(out, "P", maxLen); break; case 0x043F: appendLimited(out, "p", maxLen); break;
    case 0x0420: appendLimited(out, "R", maxLen); break; case 0x0440: appendLimited(out, "r", maxLen); break;
    case 0x0421: appendLimited(out, "S", maxLen); break; case 0x0441: appendLimited(out, "s", maxLen); break;
    case 0x0422: appendLimited(out, "T", maxLen); break; case 0x0442: appendLimited(out, "t", maxLen); break;
    case 0x0423: appendLimited(out, "U", maxLen); break; case 0x0443: appendLimited(out, "u", maxLen); break;
    case 0x0424: appendLimited(out, "F", maxLen); break; case 0x0444: appendLimited(out, "f", maxLen); break;
    case 0x0425: appendLimited(out, "Kh", maxLen); break; case 0x0445: appendLimited(out, "kh", maxLen); break;
    case 0x0426: appendLimited(out, "Ts", maxLen); break; case 0x0446: appendLimited(out, "ts", maxLen); break;
    case 0x0427: appendLimited(out, "Ch", maxLen); break; case 0x0447: appendLimited(out, "ch", maxLen); break;
    case 0x0428: appendLimited(out, "Sh", maxLen); break; case 0x0448: appendLimited(out, "sh", maxLen); break;
    case 0x0429: appendLimited(out, "Sch", maxLen); break; case 0x0449: appendLimited(out, "sch", maxLen); break;
    case 0x042B: appendLimited(out, "Y", maxLen); break; case 0x044B: appendLimited(out, "y", maxLen); break;
    case 0x042D: appendLimited(out, "E", maxLen); break; case 0x044D: appendLimited(out, "e", maxLen); break;
    case 0x042E: appendLimited(out, "Yu", maxLen); break; case 0x044E: appendLimited(out, "yu", maxLen); break;
    case 0x042F: appendLimited(out, "Ya", maxLen); break; case 0x044F: appendLimited(out, "ya", maxLen); break;
    case 0x042C: case 0x044C: case 0x042A: case 0x044A: break;
    default: appendLimited(out, "?", maxLen); break;
  }
}
}

String toDisplayText(const String& utf8, size_t maxLen) {
  String out;
  for (size_t i = 0; i < utf8.length() && out.length() < maxLen;) {
    const uint8_t c = static_cast<uint8_t>(utf8[i]);
    if (c < 0x80) {
      out += (c >= 32 && c <= 126) ? static_cast<char>(c) : '?';
      i += 1;
      continue;
    }
    if ((c == 0xD0 || c == 0xD1) && i + 1 < utf8.length()) {
      const uint8_t next = static_cast<uint8_t>(utf8[i + 1]);
      const uint16_t codepoint = (c == 0xD0) ? (0x0400 + next - 0x80) : (0x0440 + next - 0x80);
      appendTransliteratedCyrillic(out, codepoint, maxLen);
      i += 2;
      continue;
    }
    appendLimited(out, "?", maxLen);
    i += 1;
  }
  return out;
}

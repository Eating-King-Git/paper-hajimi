#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
namespace pet {
struct Lines {
  char data[64] = {}; size_t length = 0; bool discard = false;
  uint32_t last = 0;
  const char *push(char c, uint32_t now) {
    if (uint32_t(now - last) > 500 && (length || discard)) { length = 0; discard = true; }
    last = now;
    if (c == '\r') return 0;
    if (c == '\n') {
      data[length] = 0; bool valid = !discard && length;
      length = 0; discard = false; return valid ? data : 0;
    }
    if (c < 32 || c > 126 || length >= sizeof(data) - 1) discard = true;
    if (!discard) data[length++] = c;
    return 0;
  }
};
inline bool number(const char *&s, uint32_t &value) {
  if (*s < '0' || *s > '9') return false;
  value = 0;
  do {
    uint32_t digit = uint32_t(*s++ - '0');
    if (value > (UINT32_MAX - digit) / 10) return false;
    value = value * 10 + digit;
  } while (*s >= '0' && *s <= '9');
  return true;
}
inline bool reply(const char *line, const char *prefix, uint32_t &seq) {
  size_t n = strlen(prefix);
  if (strncmp(line, prefix, n)) return false;
  const char *s = line + n;
  return number(s, seq) && !*s && seq != 0;
}
inline bool playRequest(const char *line, uint32_t &seq, uint16_t &id) {
  if (strncmp(line, "PLAY,", 5)) return false;
  const char *s = line + 5; uint32_t audio;
  if (!number(s, seq) || !seq || *s++ != ',' || !number(s, audio) || *s || audio > 65535) return false;
  id = uint16_t(audio); return true;
}
}

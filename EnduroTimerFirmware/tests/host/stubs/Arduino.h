#pragma once
#include <cstdint>
#include <string>
#include <cstdio>
#define HEX 16
class String : public std::string {
public:
  using std::string::string;
  String(const std::string& s) : std::string(s) {}
  String(uint32_t n, int base = 10) { char b[32]; std::snprintf(b, sizeof(b), base == 16 ? "%x" : "%u", n); assign(b); }
};
inline uint32_t fakeMillis = 0;
inline uint32_t millis() { return fakeMillis; }
struct SerialStub { template<class... Args> void printf(const char*, Args...) {} };
inline SerialStub Serial;

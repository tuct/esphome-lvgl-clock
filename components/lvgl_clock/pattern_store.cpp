#include "pattern_store.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"
#include "esphome/core/preferences.h"
#include <cstdio>
#include <cstring>

namespace esphome {
namespace lvgl_clock {

static const char *const TAG = "lvgl_clock.pattern";

// ---- persistence -----------------------------------------------------------
//
// Saved so the wall keeps its patterns with Home Assistant switched off. That
// matters more here than in most projects: the whole design rests on the seven
// slaves needing nothing but power and a wire, and a master that came up blank
// after a reboot would quietly undo that.
//
// Written only when something changes - NVS has a finite erase count, and a
// text entity that republishes on every HA restart would otherwise write every
// time.
static const uint32_t PATTERN_PREF_HASH = 0x1C247A77u;

void PatternStore::save() {
  PackedPrefs blob{};
  for (int slot = 0; slot < PATTERN_MAX_PER_NODE; slot++) {
    std::string t = this->to_text(slot);
    strncpy(blob.slots[slot], t.c_str(), sizeof(blob.slots[slot]) - 1);
  }
  if (!this->pref_.save(&blob))
    ESP_LOGW(TAG, "Could not save patterns to flash");
}

void PatternStore::load() {
  this->pref_ = global_preferences->make_preference<PackedPrefs>(PATTERN_PREF_HASH);
  PackedPrefs blob{};
  if (!this->pref_.load(&blob))
    return;                       // nothing saved yet: keep whatever codegen put here
  int n = 0;
  for (int slot = 0; slot < PATTERN_MAX_PER_NODE; slot++) {
    if (blob.slots[slot][0] == '\0')
      continue;
    if (this->from_text(slot, blob.slots[slot]).empty())
      n++;
  }
  if (n > 0)
    ESP_LOGI(TAG, "Restored %d pattern(s) from flash", n);
}

PatternStore &pattern_store() {
  static PatternStore store;
  return store;
}

void PatternStore::set_name(int slot, const char *name) {
  if (slot < 0 || slot >= PATTERN_MAX_PER_NODE)
    return;
  Pattern &p = this->slots_[slot];
  strncpy(p.name, name != nullptr ? name : "", sizeof(p.name) - 1);
  p.name[sizeof(p.name) - 1] = '\0';
  // A name always starts a fresh definition. Without this a re-send that
  // changed a pattern would leave clocks from the previous one in the slots it
  // no longer touches.
  p.got = 0;
  if (slot >= this->count_)
    this->count_ = slot + 1;
}

void PatternStore::set_clock(int slot, int clock, const PatternClock &c) {
  if (slot < 0 || slot >= PATTERN_MAX_PER_NODE || clock < 0 || clock >= PATTERN_CLOCKS)
    return;
  Pattern &p = this->slots_[slot];
  p.clocks[clock] = c;
  p.got |= 1u << clock;
  if (slot >= this->count_)
    this->count_ = slot + 1;
}

bool PatternStore::parse_line(const char *line) {
  if (strncmp(line, "CCPN ", 5) == 0) {
    int slot = -1;
    char name[16] = {0};
    // %15s stops at whitespace, which is why names may not contain spaces.
    if (sscanf(line + 5, "%d %15s", &slot, name) < 1)
      return false;
    this->set_name(slot, name);
    ESP_LOGD(TAG, "RX pattern %d '%s' (header)", slot, name);
    return true;
  }
  if (strncmp(line, "CCPC ", 5) == 0) {
    int slot, clock, h0, h1, d0, d1, v0, v1;
    if (sscanf(line + 5, "%d %d %d %d %d %d %d %d", &slot, &clock, &h0, &h1, &d0, &d1, &v0, &v1) != 8)
      return false;
    // Clamp rather than reject: a corrupted field should cost one odd-looking
    // clock, not a pattern that never completes and so never draws at all.
    PatternClock c;
    c.h0 = (uint16_t) (((h0 % 360) + 360) % 360);
    c.h1 = (uint16_t) (((h1 % 360) + 360) % 360);
    c.dir0 = (int8_t) (d0 > 0 ? 1 : (d0 < 0 ? -1 : 0));
    c.dir1 = (int8_t) (d1 > 0 ? 1 : (d1 < 0 ? -1 : 0));
    c.v0 = (uint8_t) (v0 < 0 ? 0 : (v0 > 100 ? 100 : v0));
    c.v1 = (uint8_t) (v1 < 0 ? 0 : (v1 > 100 ? 100 : v1));
    this->set_clock(slot, clock, c);
    if (slot >= 0 && slot < PATTERN_MAX_PER_NODE && this->slots_[slot].complete() &&
        clock == PATTERN_CLOCKS - 1)
      ESP_LOGI(TAG, "RX pattern %d '%s' complete", slot, this->slots_[slot].name);
    return true;
  }
  return false;
}

int PatternStore::format_name(int slot, char *out, size_t out_len) const {
  if (slot < 0 || slot >= this->count_)
    return 0;
  int n = snprintf(out, out_len, "CCPN %d %s\n", slot, this->slots_[slot].name);
  return (n > 0 && (size_t) n < out_len) ? n : 0;
}

int PatternStore::format_clock(int slot, int clock, char *out, size_t out_len) const {
  if (slot < 0 || slot >= this->count_ || clock < 0 || clock >= PATTERN_CLOCKS)
    return 0;
  const PatternClock &c = this->slots_[slot].clocks[clock];
  int n = snprintf(out, out_len, "CCPC %d %d %u %u %d %d %u %u\n", slot, clock, (unsigned) c.h0,
                   (unsigned) c.h1, (int) c.dir0, (int) c.dir1, (unsigned) c.v0, (unsigned) c.v1);
  return (n > 0 && (size_t) n < out_len) ? n : 0;
}

// Base64 is hand-rolled rather than pulled from esphome::helpers: the helper's
// name and signature have moved between releases, and this is twenty lines.
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string b64_encode(const uint8_t *in, size_t len) {
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    uint32_t v = (uint32_t) in[i] << 16;
    if (i + 1 < len)
      v |= (uint32_t) in[i + 1] << 8;
    if (i + 2 < len)
      v |= in[i + 2];
    out += B64[(v >> 18) & 63];
    out += B64[(v >> 12) & 63];
    out += (i + 1 < len) ? B64[(v >> 6) & 63] : '=';
    out += (i + 2 < len) ? B64[v & 63] : '=';
  }
  return out;
}

static int b64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

// Returns the number of bytes written, or -1 if the text is not base64.
static int b64_decode(const std::string &in, uint8_t *out, size_t out_len) {
  uint32_t acc = 0;
  int bits = 0;
  size_t n = 0;
  for (char c : in) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ')
      continue;
    int v = b64_value(c);
    if (v < 0)
      return -1;
    acc = (acc << 6) | (uint32_t) v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= out_len)
        return -1;
      out[n++] = (uint8_t) ((acc >> bits) & 0xFF);
    }
  }
  return (int) n;
}

std::string PatternStore::to_text(int slot) const {
  if (slot < 0 || slot >= PATTERN_MAX_PER_NODE || !this->slots_[slot].complete())
    return "";
  const Pattern &p = this->slots_[slot];
  uint8_t buf[PACKED_BYTES];
  for (int i = 0; i < PATTERN_CLOCKS; i++) {
    const PatternClock &c = p.clocks[i];
    uint8_t *b = buf + i * 5;
    // Round to the nearest 1.5 deg step; 240 steps wrap back to 0 at 360.
    b[0] = (uint8_t) ((int) ((c.h0 * 2 + 1) / 3) % 240);
    b[1] = (uint8_t) ((int) ((c.h1 * 2 + 1) / 3) % 240);
    b[2] = (uint8_t) (((c.dir0 + 1) << 2) | (c.dir1 + 1));
    b[3] = c.v0;
    b[4] = c.v1;
  }
  return std::string(p.name) + ":" + b64_encode(buf, sizeof(buf));
}

std::string PatternStore::from_text(int slot, const std::string &text) {
  if (slot < 0 || slot >= PATTERN_MAX_PER_NODE)
    return "slot out of range";
  // An empty write clears the slot rather than erroring - that is the obvious
  // way to say "I do not want this one" from a text field.
  if (text.empty()) {
    this->slots_[slot].got = 0;
    return "";
  }
  std::string name = "pattern";
  std::string data = text;
  size_t colon = text.find(':');
  if (colon != std::string::npos) {
    name = text.substr(0, colon);
    data = text.substr(colon + 1);
  }
  uint8_t buf[PACKED_BYTES];
  int n = b64_decode(data, buf, sizeof(buf));
  if (n < 0)
    return "not base64";
  if (n != (int) PACKED_BYTES)
    return str_sprintf("expected %u bytes, got %d", (unsigned) PACKED_BYTES, n);

  this->set_name(slot, name.c_str());
  for (int i = 0; i < PATTERN_CLOCKS; i++) {
    const uint8_t *b = buf + i * 5;
    PatternClock c;
    c.h0 = (uint16_t) ((b[0] * 3) / 2 % 360);
    c.h1 = (uint16_t) ((b[1] * 3) / 2 % 360);
    c.dir0 = (int8_t) (((b[2] >> 2) & 3) - 1);
    c.dir1 = (int8_t) ((b[2] & 3) - 1);
    c.v0 = b[3] > 100 ? 100 : b[3];
    c.v1 = b[4] > 100 ? 100 : b[4];
    this->set_clock(slot, i, c);
  }
  ESP_LOGI(TAG, "Pattern %d loaded from text: '%s'", slot, name.c_str());
  return "";
}

}  // namespace lvgl_clock
}  // namespace esphome

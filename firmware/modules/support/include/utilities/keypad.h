#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

// Instance-local multi-tap editor, independent of Slint and of its caller.
class Keypad {
public:
  const std::string &value() const {
    return text;
  }
  unsigned current_mode() const {
    return mode;
  }
  bool is_uppercase() const {
    return uppercase;
  }
  void reset(const std::string &initial, size_t max_bytes) {
    size_t end = std::min(initial.size(), max_bytes);
    while (end && end < initial.size() && (static_cast<unsigned char>(initial[end]) & 0xc0) == 0x80)
      --end;
    text = initial.substr(0, end);
    limit = max_bytes;
    mode = 0;
    uppercase = false;
    commit();
  }
  void commit() {
    pending = -1;
  }
  std::string label(int key) const {
    if (key < 0 || key > 11)
      return "";
    if (key == 11)
      return "Del";
    if (key == 9 && mode == 0)
      return uppercase ? "ABC" : "abc";
    auto result = std::string(groups()[key]);
    if (key == 10 && mode != 1)
      return "Space";
    if (uppercase && mode == 0)
      for (char &c : result)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return result;
  }
  void next_mode() {
    commit();
    mode = (mode + 1) % 3;
  }
  void press(int key, uint64_t now_ms) {
    if (key < 0 || key > 11)
      return;
    if (key == 11) {
      commit();
      if (!text.empty()) {
        size_t start = text.size() - 1;
        while (start && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80)
          --start;
        text.erase(start);
      }
      return;
    }
    if (key == 9 && mode == 0) {
      commit();
      uppercase = !uppercase;
      return;
    }
    const std::string_view group(groups()[key]);
    bool cycling = pending == key && now_ms >= last_ms && now_ms - last_ms < 1000;
    if (cycling) {
      step = (step + 1) % group.size();
      text.pop_back();
    }
    else {
      commit();
      step = 0;
      if (text.size() >= limit)
        return;
    }
    char c = group[step];
    if (uppercase && mode == 0)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    append(c);
    pending = mode == 1 || group.size() == 1 ? -1 : key;
    last_ms = now_ms;
  }

private:
  std::string text;
  unsigned mode = 0;
  bool uppercase = false;
  size_t limit = 32;
  int pending = -1;
  size_t step = 0;
  uint64_t last_ms = 0;
  void append(char c) {
    if (text.size() < limit)
      text += c;
  }
  const char *const *groups() const {
    static const char *const letters[] = {"abc", "def", "ghi", "jkl", "mno", "pqr", "stu", "vwx", "yz", "", " ", ""};
    static const char *const digits[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", ".", "0", ""};
    static const char *const symbols[] = {".,?",  "!@#",  "$%&",  "*+=", "-_/", "\\|~",
                                          "()[]", "{}<>", "'\"`", ":;^", " ",   ""};
    return mode == 0 ? letters : mode == 1 ? digits : symbols;
  }
};

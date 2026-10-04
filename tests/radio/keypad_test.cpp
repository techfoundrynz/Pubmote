#include "keypad.h"
#include <cassert>
#include <cctype>

int main() {
  Keypad a, b;
  a.reset("", 32);
  a.press(0, 100);
  a.press(0, 300);
  assert(a.value() == "b");
  a.press(0, 1300);
  assert(a.value() == "ba"); // expiry commits, including the exact boundary
  a.press(1, 1400);
  assert(a.value() == "bad");
  a.press(9, 1600);
  a.press(0, 1700);
  assert(a.value() == "badA" && a.label(0) == "ABC");
  a.press(10, 1800);
  a.press(10, 1900);
  assert(a.value() == "badA  "); // every space tap inserts a space
  a.press(11, 2000);
  assert(a.value() == "badA ");
  a.press(11, 2010);
  assert(a.value() == "badA");
  a.next_mode();
  a.press(1, 2100);
  a.press(1, 2200);
  assert(a.value() == "badA22"); // numbers never multi-tap
  a.next_mode();
  a.press(1, 2300);
  a.press(1, 2400);
  a.press(1, 2500);
  assert(a.value() == "badA22#");
  a.next_mode();
  assert(a.current_mode() == 0 && a.label(0) == "ABC");

  a.reset("", 32);
  std::string letters;
  for (int key = 0; key < 9; ++key)
    letters += a.label(key);
  assert(letters == "abcdefghijklmnopqrstuvwxyz");
  assert(a.label(10) == "Space");
  a.next_mode();
  assert(a.label(0) == "1" && a.label(10) == "0");
  a.next_mode();
  std::string symbols;
  for (int key = 0; key < 10; ++key)
    symbols += a.label(key);
  for (unsigned char c : symbols)
    assert(!std::isalnum(c));
  for (char c = '!'; c <= '~'; ++c)
    if (std::ispunct(static_cast<unsigned char>(c)))
      assert(symbols.find(c) != std::string::npos);
  assert(a.label(10) == "Space");

  a.reset("x", 2);
  a.press(0, 100);
  a.press(0, 200);
  assert(a.value() == "xb"); // cycling remains available at capacity
  a.press(1, 300);
  a.press(0, 400);
  assert(a.value() == "xb"); // overflow cannot replace an earlier character
  b.reset("", 64);
  b.press(0, 100);
  assert(b.value() == "a" && a.value() == "xb"); // no shared editor state
  b.reset("\xc3\xa9", 32);
  b.press(11, 200);
  assert(b.value().empty()); // delete a complete UTF-8 character
  b.press(11, 300);
  assert(b.value().empty());
  b.reset("\xc3\xa9", 1);
  assert(b.value().empty()); // byte limits do not split an existing UTF-8 sequence
  b.reset(std::string(64, 'a'), 64);
  b.press(1, 400);
  assert(b.value().size() == 64);
  b.reset("", 32);
  assert(b.current_mode() == 0 && !b.is_uppercase() && b.value().empty());
  b.press(12, 500);
  b.press(-1, 500);
  assert(b.value().empty());
  assert(b.label(-1).empty() && b.label(12).empty());
}
